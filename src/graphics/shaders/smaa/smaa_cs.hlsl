// SMAA 1x (Jimenez et al., see SMAA_LICENSE.txt) as compute shaders for the swap post effect.
//
// Compiled with fxc for cs_5_1 (/O3), once per pass:
//   SMAA_PASS=1  edge detection:            t0 = color (gamma)            -> u0 = edges (RG8)
//   SMAA_PASS=2  blending weights:          t0 = edges, t1 = area, t2 = search -> u0 = weights
//   SMAA_PASS=3  neighborhood blending:     t0 = color, t1 = weights      -> u0 = output
// The vertex shader helpers of SMAA.hlsl are evaluated per pixel here, and the pixel shader
// "discard" of the edge detection becomes "no edge", because every edge texel is written.
// XE_SMAA_ULTRA=1 builds the ultra variant of passes 1 and 2: color edge detection (also finds the
// edges between colors of similar brightness, which luma detection misses), lower threshold, more
// search steps and corner rounding. Pass 3 is the same for both.

#define SMAA_CUSTOM_SL 1
SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASamplePoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#define SMAAGather(tex, coord) tex.Gather(LinearSampler, coord, 0)

cbuffer XeSmaaConstants : register(b0) {
  // (1 / width, 1 / height, width, height)
  float4 xe_smaa_rt_metrics;
  uint2 xe_smaa_size;
};
#define SMAA_RT_METRICS xe_smaa_rt_metrics
#if XE_SMAA_ULTRA
#define SMAA_PRESET_ULTRA 1
#else
#define SMAA_PRESET_HIGH 1
#endif

#define discard return float2(0.0, 0.0)
#include "SMAA.hlsl"
#undef discard

RWTexture2D<unorm float4> xe_smaa_dest : register(u0);

#if SMAA_PASS == 1
Texture2D xe_smaa_color : register(t0);
#elif SMAA_PASS == 2
Texture2D xe_smaa_edges : register(t0);
Texture2D xe_smaa_area : register(t1);
Texture2D xe_smaa_search : register(t2);
#else
Texture2D xe_smaa_color : register(t0);
Texture2D xe_smaa_weights : register(t1);
#endif

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  [branch] if (any(xe_thread_id.xy >= xe_smaa_size)) {
    return;
  }
  float2 texcoord = (float2(xe_thread_id.xy) + 0.5) * SMAA_RT_METRICS.xy;
#if SMAA_PASS == 1
  float4 offset[3];
  SMAAEdgeDetectionVS(texcoord, offset);
#if XE_SMAA_ULTRA
  float2 edges = SMAAColorEdgeDetectionPS(texcoord, offset, xe_smaa_color);
#else
  float2 edges = SMAALumaEdgeDetectionPS(texcoord, offset, xe_smaa_color);
#endif
  xe_smaa_dest[xe_thread_id.xy] = float4(edges, 0.0, 0.0);
#elif SMAA_PASS == 2
  float2 pixcoord;
  float4 offset[3];
  SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
  xe_smaa_dest[xe_thread_id.xy] = SMAABlendingWeightCalculationPS(
      texcoord, pixcoord, offset, xe_smaa_edges, xe_smaa_area, xe_smaa_search, float4(0.0, 0.0, 0.0, 0.0));
#else
  float4 offset;
  SMAANeighborhoodBlendingVS(texcoord, offset);
  float4 color = SMAANeighborhoodBlendingPS(texcoord, offset, xe_smaa_color, xe_smaa_weights);
  xe_smaa_dest[xe_thread_id.xy] = float4(color.rgb, 1.0);
#endif
}
