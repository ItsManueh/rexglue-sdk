/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/assert.h>
#include <rex/dbg.h>
#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/deferred_command_list.h>

#include <fmt/format.h>
#include <rex/graphics/flags.h>
#include <rex/math.h>

namespace rex::graphics::d3d12 {

DeferredCommandList::DeferredCommandList(const D3D12CommandProcessor& command_processor,
                                         size_t initial_size)
    : command_processor_(command_processor) {
  command_stream_.reserve(initial_size / sizeof(uintmax_t));
}

void DeferredCommandList::Reset() {
  command_stream_.clear();
}

void DeferredCommandList::Execute(ID3D12GraphicsCommandList* command_list,
                                  ID3D12GraphicsCommandList1* command_list_1,
                                  size_t command_limit) {
  size_t commands_executed = 0;
#if XE_GPU_FINE_GRAINED_DRAW_SCOPES
  SCOPE_profile_cpu_f("gpu");
#endif  // XE_GPU_FINE_GRAINED_DRAW_SCOPES
  const uintmax_t* stream = command_stream_.data();
  size_t stream_remaining = command_stream_.size();
  ID3D12PipelineState* current_pipeline_state = nullptr;
  while (stream_remaining != 0) {
    if (commands_executed++ >= command_limit) {
      break;
    }
    const CommandHeader& header = *reinterpret_cast<const CommandHeader*>(stream);
    stream += kCommandHeaderSizeElements;
    stream_remaining -= kCommandHeaderSizeElements;
    switch (header.command) {
      case Command::kD3DClearDepthStencilView: {
        auto& args = *reinterpret_cast<const ClearDepthStencilViewHeader*>(stream);
        command_list->ClearDepthStencilView(
            args.depth_stencil_view, args.clear_flags, args.depth, args.stencil, args.num_rects,
            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);
      } break;
      case Command::kD3DClearRenderTargetView: {
        auto& args = *reinterpret_cast<const ClearRenderTargetViewHeader*>(stream);
        command_list->ClearRenderTargetView(
            args.render_target_view, args.color_rgba, args.num_rects,
            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);
      } break;
      case Command::kD3DClearUnorderedAccessViewUint: {
        auto& args = *reinterpret_cast<const ClearUnorderedAccessViewHeader*>(stream);
        command_list->ClearUnorderedAccessViewUint(
            args.view_gpu_handle_in_current_heap, args.view_cpu_handle, args.resource,
            args.values_uint, args.num_rects,
            args.num_rects ? reinterpret_cast<const D3D12_RECT*>(&args + 1) : nullptr);
      } break;
      case Command::kD3DCopyBufferRegion: {
        auto& args = *reinterpret_cast<const D3DCopyBufferRegionArguments*>(stream);
        command_list->CopyBufferRegion(args.dst_buffer, args.dst_offset, args.src_buffer,
                                       args.src_offset, args.num_bytes);
      } break;
      case Command::kD3DCopyResource: {
        auto& args = *reinterpret_cast<const D3DCopyResourceArguments*>(stream);
        command_list->CopyResource(args.dst_resource, args.src_resource);
      } break;
      case Command::kCopyTexture: {
        auto& args = *reinterpret_cast<const CopyTextureArguments*>(stream);
        command_list->CopyTextureRegion(&args.dst, 0, 0, 0, &args.src, nullptr);
      } break;
      case Command::kD3DCopyTextureRegion: {
        auto& args = *reinterpret_cast<const D3DCopyTextureRegionArguments*>(stream);
        command_list->CopyTextureRegion(&args.dst, args.dst_x, args.dst_y, args.dst_z, &args.src,
                                        args.has_src_box ? &args.src_box : nullptr);
      } break;
      case Command::kD3DDispatch: {
        if (current_pipeline_state != nullptr) {
          auto& args = *reinterpret_cast<const D3DDispatchArguments*>(stream);
          command_list->Dispatch(args.thread_group_count_x, args.thread_group_count_y,
                                 args.thread_group_count_z);
        }
      } break;
      case Command::kD3DDrawIndexedInstanced: {
        if (current_pipeline_state != nullptr) {
          auto& args = *reinterpret_cast<const D3DDrawIndexedInstancedArguments*>(stream);
          command_list->DrawIndexedInstanced(args.index_count_per_instance, args.instance_count,
                                             args.start_index_location, args.base_vertex_location,
                                             args.start_instance_location);
        }
      } break;
      case Command::kD3DDrawInstanced: {
        if (current_pipeline_state != nullptr) {
          auto& args = *reinterpret_cast<const D3DDrawInstancedArguments*>(stream);
          command_list->DrawInstanced(args.vertex_count_per_instance, args.instance_count,
                                      args.start_vertex_location, args.start_instance_location);
        }
      } break;
      case Command::kD3DBeginQuery: {
        auto& args = *reinterpret_cast<const D3DQueryArguments*>(stream);
        command_list->BeginQuery(args.query_heap, args.type, args.index);
      } break;
      case Command::kD3DEndQuery: {
        auto& args = *reinterpret_cast<const D3DQueryArguments*>(stream);
        command_list->EndQuery(args.query_heap, args.type, args.index);
      } break;
      case Command::kD3DResolveQueryData: {
        auto& args = *reinterpret_cast<const D3DResolveQueryDataArguments*>(stream);
        command_list->ResolveQueryData(args.query_heap, args.type, args.start_index,
                                       args.num_queries, args.destination_buffer,
                                       args.aligned_destination_buffer_offset);
      } break;
      case Command::kD3DIASetIndexBuffer: {
        auto view = reinterpret_cast<const D3D12_INDEX_BUFFER_VIEW*>(stream);
        command_list->IASetIndexBuffer(view->Format != DXGI_FORMAT_UNKNOWN ? view : nullptr);
      } break;
      case Command::kD3DIASetPrimitiveTopology: {
        command_list->IASetPrimitiveTopology(
            *reinterpret_cast<const D3D12_PRIMITIVE_TOPOLOGY*>(stream));
      } break;
      case Command::kD3DIASetVertexBuffers: {
        static_assert(alignof(D3D12_VERTEX_BUFFER_VIEW) <= alignof(uintmax_t));
        auto& args = *reinterpret_cast<const D3DIASetVertexBuffersHeader*>(stream);
        command_list->IASetVertexBuffers(args.start_slot, args.num_views,
                                         reinterpret_cast<const D3D12_VERTEX_BUFFER_VIEW*>(
                                             reinterpret_cast<const uint8_t*>(stream) +
                                             rex::align(sizeof(D3DIASetVertexBuffersHeader),
                                                        alignof(D3D12_VERTEX_BUFFER_VIEW))));
      } break;
      case Command::kD3DOMSetBlendFactor: {
        command_list->OMSetBlendFactor(reinterpret_cast<const FLOAT*>(stream));
      } break;
      case Command::kD3DOMSetRenderTargets: {
        auto& args = *reinterpret_cast<const D3DOMSetRenderTargetsArguments*>(stream);
        command_list->OMSetRenderTargets(
            args.num_render_target_descriptors, args.render_target_descriptors,
            args.rts_single_handle_to_descriptor_range ? TRUE : FALSE,
            args.depth_stencil ? &args.depth_stencil_descriptor : nullptr);
      } break;
      case Command::kD3DOMSetStencilRef: {
        command_list->OMSetStencilRef(*reinterpret_cast<const UINT*>(stream));
      } break;
      case Command::kD3DResourceBarrier: {
        static_assert(alignof(D3D12_RESOURCE_BARRIER) <= alignof(uintmax_t));
        command_list->ResourceBarrier(
            *reinterpret_cast<const UINT*>(stream),
            reinterpret_cast<const D3D12_RESOURCE_BARRIER*>(
                reinterpret_cast<const uint8_t*>(stream) +
                rex::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER))));
      } break;
      case Command::kRSSetScissorRect: {
        command_list->RSSetScissorRects(1, reinterpret_cast<const D3D12_RECT*>(stream));
      } break;
      case Command::kRSSetViewport: {
        command_list->RSSetViewports(1, reinterpret_cast<const D3D12_VIEWPORT*>(stream));
      } break;
      case Command::kD3DSetComputeRoot32BitConstants: {
        auto args = reinterpret_cast<const SetRoot32BitConstantsHeader*>(stream);
        command_list->SetComputeRoot32BitConstants(args->root_parameter_index,
                                                   args->num_32bit_values_to_set, args + 1,
                                                   args->dest_offset_in_32bit_values);
      } break;
      case Command::kD3DSetGraphicsRoot32BitConstants: {
        auto args = reinterpret_cast<const SetRoot32BitConstantsHeader*>(stream);
        command_list->SetGraphicsRoot32BitConstants(args->root_parameter_index,
                                                    args->num_32bit_values_to_set, args + 1,
                                                    args->dest_offset_in_32bit_values);
      } break;
      case Command::kD3DSetComputeRootConstantBufferView: {
        auto& args = *reinterpret_cast<const SetRootConstantBufferViewArguments*>(stream);
        command_list->SetComputeRootConstantBufferView(args.root_parameter_index,
                                                       args.buffer_location);
      } break;
      case Command::kD3DSetGraphicsRootConstantBufferView: {
        auto& args = *reinterpret_cast<const SetRootConstantBufferViewArguments*>(stream);
        command_list->SetGraphicsRootConstantBufferView(args.root_parameter_index,
                                                        args.buffer_location);
      } break;
      case Command::kD3DSetComputeRootDescriptorTable: {
        auto& args = *reinterpret_cast<const SetRootDescriptorTableArguments*>(stream);
        command_list->SetComputeRootDescriptorTable(args.root_parameter_index,
                                                    args.base_descriptor);
      } break;
      case Command::kD3DSetGraphicsRootDescriptorTable: {
        auto& args = *reinterpret_cast<const SetRootDescriptorTableArguments*>(stream);
        command_list->SetGraphicsRootDescriptorTable(args.root_parameter_index,
                                                     args.base_descriptor);
      } break;
      case Command::kD3DSetComputeRootShaderResourceView: {
        auto& args = *reinterpret_cast<const SetRootConstantBufferViewArguments*>(stream);
        command_list->SetComputeRootShaderResourceView(args.root_parameter_index,
                                                       args.buffer_location);
      } break;
      case Command::kD3DSetGraphicsRootShaderResourceView: {
        auto& args = *reinterpret_cast<const SetRootConstantBufferViewArguments*>(stream);
        command_list->SetGraphicsRootShaderResourceView(args.root_parameter_index,
                                                        args.buffer_location);
      } break;
      case Command::kD3DSetComputeRootSignature: {
        command_list->SetComputeRootSignature(
            *reinterpret_cast<ID3D12RootSignature* const*>(stream));
      } break;
      case Command::kD3DSetGraphicsRootSignature: {
        command_list->SetGraphicsRootSignature(
            *reinterpret_cast<ID3D12RootSignature* const*>(stream));
      } break;
      case Command::kD3DSetComputeRootUnorderedAccessView: {
        auto& args = *reinterpret_cast<const SetRootConstantBufferViewArguments*>(stream);
        command_list->SetComputeRootUnorderedAccessView(args.root_parameter_index,
                                                        args.buffer_location);
      } break;
      case Command::kD3DSetGraphicsRootUnorderedAccessView: {
        auto& args = *reinterpret_cast<const SetRootConstantBufferViewArguments*>(stream);
        command_list->SetGraphicsRootUnorderedAccessView(args.root_parameter_index,
                                                         args.buffer_location);
      } break;
      case Command::kSetDescriptorHeaps: {
        auto& args = *reinterpret_cast<const SetDescriptorHeapsArguments*>(stream);
        UINT num_descriptor_heaps = 0;
        ID3D12DescriptorHeap* descriptor_heaps[2];
        if (args.cbv_srv_uav_descriptor_heap != nullptr) {
          descriptor_heaps[num_descriptor_heaps++] = args.cbv_srv_uav_descriptor_heap;
        }
        if (args.sampler_descriptor_heap != nullptr) {
          descriptor_heaps[num_descriptor_heaps++] = args.sampler_descriptor_heap;
        }
        command_list->SetDescriptorHeaps(num_descriptor_heaps, descriptor_heaps);
      } break;
      case Command::kD3DSetPipelineState: {
        current_pipeline_state = *reinterpret_cast<ID3D12PipelineState* const*>(stream);
        if (current_pipeline_state) {
          command_list->SetPipelineState(current_pipeline_state);
        }
      } break;
      case Command::kSetPipelineStateHandle: {
        current_pipeline_state =
            command_processor_.GetD3D12PipelineByHandle(*reinterpret_cast<void* const*>(stream));
        if (current_pipeline_state) {
          command_list->SetPipelineState(current_pipeline_state);
        }
      } break;
      case Command::kD3DSetSamplePositions: {
        if (command_list_1 != nullptr) {
          auto& args = *reinterpret_cast<const D3DSetSamplePositionsArguments*>(stream);
          command_list_1->SetSamplePositions(
              args.num_samples_per_pixel, args.num_pixels,
              (args.num_samples_per_pixel && args.num_pixels)
                  ? const_cast<D3D12_SAMPLE_POSITION*>(args.sample_positions)
                  : nullptr);
        }
      } break;
      case Command::kBeginDebugMarker: {
        auto& args = *reinterpret_cast<const DebugMarkerHeader*>(stream);
        const char* label_name = reinterpret_cast<const char*>(
            reinterpret_cast<const uint8_t*>(stream) + sizeof(DebugMarkerHeader));
        command_list->BeginEvent(1, label_name, static_cast<UINT>(args.label_length + 1));
      } break;
      case Command::kEndDebugMarker: {
        command_list->EndEvent();
      } break;
      case Command::kInsertDebugMarker: {
        auto& args = *reinterpret_cast<const DebugMarkerHeader*>(stream);
        const char* label_name = reinterpret_cast<const char*>(
            reinterpret_cast<const uint8_t*>(stream) + sizeof(DebugMarkerHeader));
        command_list->SetMarker(1, label_name, static_cast<UINT>(args.label_length + 1));
      } break;
      default:
        assert_unhandled_case(header.command);
        break;
    }
    stream += header.arguments_size_elements;
    stream_remaining -= header.arguments_size_elements;
  }
}

size_t DeferredCommandList::GetCommandCount() const {
  size_t count = 0;
  const uintmax_t* stream = command_stream_.data();
  size_t stream_remaining = command_stream_.size();
  while (stream_remaining != 0) {
    const CommandHeader& header = *reinterpret_cast<const CommandHeader*>(stream);
    size_t size = kCommandHeaderSizeElements + header.arguments_size_elements;
    stream += size;
    stream_remaining -= size;
    ++count;
  }
  return count;
}

namespace {
std::string DescribeTextureCopyLocation(const D3D12_TEXTURE_COPY_LOCATION& location) {
  if (!location.pResource) {
    return "null";
  }
  D3D12_RESOURCE_DESC desc = location.pResource->GetDesc();
  std::string resource = fmt::format(
      "{} {}x{}x{} fmt {} mips {}",
      desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? "buffer" : "texture", desc.Width,
      desc.Height, desc.DepthOrArraySize, int(desc.Format), desc.MipLevels);
  if (location.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX) {
    return fmt::format("[{} subresource {}]", resource, location.SubresourceIndex);
  }
  const auto& f = location.PlacedFootprint;
  return fmt::format("[{} footprint offset {} {}x{}x{} pitch {} fmt {}]", resource, f.Offset,
                     f.Footprint.Width, f.Footprint.Height, f.Footprint.Depth,
                     f.Footprint.RowPitch, int(f.Footprint.Format));
}

uint64_t ResourceWidth(ID3D12Resource* resource) {
  return resource ? resource->GetDesc().Width : 0;
}
}  // namespace

std::string DeferredCommandList::DescribeCommand(size_t command_index) const {
  const uintmax_t* stream = command_stream_.data();
  size_t stream_remaining = command_stream_.size();
  for (size_t i = 0; stream_remaining != 0; ++i) {
    const CommandHeader& header = *reinterpret_cast<const CommandHeader*>(stream);
    const uintmax_t* args_ptr = stream + kCommandHeaderSizeElements;
    if (i == command_index) {
      std::string text = fmt::format("command type {}", int(header.command));
      switch (header.command) {
        case Command::kD3DCopyBufferRegion: {
          auto& a = *reinterpret_cast<const D3DCopyBufferRegionArguments*>(args_ptr);
          text += fmt::format(
              " CopyBufferRegion dst {} (size {}) +{} <- src {} (size {}) +{}, {} bytes",
              static_cast<void*>(a.dst_buffer), ResourceWidth(a.dst_buffer), a.dst_offset,
              static_cast<void*>(a.src_buffer), ResourceWidth(a.src_buffer), a.src_offset,
              a.num_bytes);
        } break;
        case Command::kD3DCopyResource: {
          auto& a = *reinterpret_cast<const D3DCopyResourceArguments*>(args_ptr);
          text += fmt::format(" CopyResource dst {} <- src {}", static_cast<void*>(a.dst_resource),
                              static_cast<void*>(a.src_resource));
        } break;
        case Command::kCopyTexture: {
          auto& a = *reinterpret_cast<const CopyTextureArguments*>(args_ptr);
          text += " CopyTexture dst " + DescribeTextureCopyLocation(a.dst) + " <- src " +
                  DescribeTextureCopyLocation(a.src);
        } break;
        case Command::kD3DCopyTextureRegion: {
          auto& a = *reinterpret_cast<const D3DCopyTextureRegionArguments*>(args_ptr);
          text += fmt::format(" CopyTextureRegion dst {} at ({}, {}, {}) <- src {}",
                              DescribeTextureCopyLocation(a.dst), a.dst_x, a.dst_y, a.dst_z,
                              DescribeTextureCopyLocation(a.src));
          if (a.has_src_box) {
            text += fmt::format(" box ({}, {}, {})-({}, {}, {})", a.src_box.left, a.src_box.top,
                                a.src_box.front, a.src_box.right, a.src_box.bottom,
                                a.src_box.back);
          }
        } break;
        case Command::kD3DDispatch: {
          auto& a = *reinterpret_cast<const D3DDispatchArguments*>(args_ptr);
          text += fmt::format(" Dispatch {}x{}x{}", a.thread_group_count_x,
                              a.thread_group_count_y, a.thread_group_count_z);
        } break;
        case Command::kD3DResourceBarrier: {
          UINT count = *reinterpret_cast<const UINT*>(args_ptr);
          auto barriers = reinterpret_cast<const D3D12_RESOURCE_BARRIER*>(
              reinterpret_cast<const uint8_t*>(args_ptr) +
              rex::align(sizeof(UINT), alignof(D3D12_RESOURCE_BARRIER)));
          text += fmt::format(" ResourceBarrier x{}:", count);
          for (UINT b = 0; b < count; ++b) {
            const D3D12_RESOURCE_BARRIER& barrier = barriers[b];
            if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION) {
              text += fmt::format(" [transition {} sub {} 0x{:X}->0x{:X} flags {}]",
                                  static_cast<void*>(barrier.Transition.pResource),
                                  barrier.Transition.Subresource,
                                  unsigned(barrier.Transition.StateBefore),
                                  unsigned(barrier.Transition.StateAfter),
                                  unsigned(barrier.Flags));
            } else if (barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV) {
              text += fmt::format(" [uav {}]", static_cast<void*>(barrier.UAV.pResource));
            } else {
              text += fmt::format(" [aliasing {} -> {}]",
                                  static_cast<void*>(barrier.Aliasing.pResourceBefore),
                                  static_cast<void*>(barrier.Aliasing.pResourceAfter));
            }
          }
        } break;
        case Command::kRSSetScissorRect: {
          auto& r = *reinterpret_cast<const D3D12_RECT*>(args_ptr);
          text += fmt::format(" RSSetScissorRect ({}, {})-({}, {})", r.left, r.top, r.right,
                              r.bottom);
        } break;
        case Command::kRSSetViewport: {
          auto& v = *reinterpret_cast<const D3D12_VIEWPORT*>(args_ptr);
          text += fmt::format(" RSSetViewport ({}, {}) {}x{} depth {}..{}", v.TopLeftX,
                              v.TopLeftY, v.Width, v.Height, v.MinDepth, v.MaxDepth);
        } break;
        case Command::kD3DClearRenderTargetView: {
          auto& a = *reinterpret_cast<const ClearRenderTargetViewHeader*>(args_ptr);
          auto rects = reinterpret_cast<const D3D12_RECT*>(&a + 1);
          text += " ClearRenderTargetView";
          for (UINT r = 0; r < a.num_rects && r < 8; ++r) {
            text += fmt::format(" ({}, {})-({}, {})", rects[r].left, rects[r].top,
                                rects[r].right, rects[r].bottom);
          }
        } break;
        case Command::kD3DClearDepthStencilView: {
          auto& a = *reinterpret_cast<const ClearDepthStencilViewHeader*>(args_ptr);
          auto rects = reinterpret_cast<const D3D12_RECT*>(&a + 1);
          text += " ClearDepthStencilView";
          for (UINT r = 0; r < a.num_rects && r < 8; ++r) {
            text += fmt::format(" ({}, {})-({}, {})", rects[r].left, rects[r].top,
                                rects[r].right, rects[r].bottom);
          }
        } break;
        case Command::kD3DSetSamplePositions: {
          auto& a = *reinterpret_cast<const D3DSetSamplePositionsArguments*>(args_ptr);
          text += fmt::format(" SetSamplePositions {} samples x {} pixels:",
                              a.num_samples_per_pixel, a.num_pixels);
          for (UINT i = 0; i < std::min(a.num_samples_per_pixel * a.num_pixels, 16u); ++i) {
            text += fmt::format(" ({}, {})", int(a.sample_positions[i].X),
                                int(a.sample_positions[i].Y));
          }
        } break;
        case Command::kD3DOMSetRenderTargets: {
          auto& a = *reinterpret_cast<const D3DOMSetRenderTargetsArguments*>(args_ptr);
          text += fmt::format(" OMSetRenderTargets {} RTVs (single range {}), depth {}",
                              a.num_render_target_descriptors,
                              a.rts_single_handle_to_descriptor_range, a.depth_stencil);
        } break;
        case Command::kD3DDrawInstanced: {
          auto& a = *reinterpret_cast<const D3DDrawInstancedArguments*>(args_ptr);
          text += fmt::format(" DrawInstanced {} vertices x {} from {}", a.vertex_count_per_instance,
                              a.instance_count, a.start_vertex_location);
        } break;
        case Command::kD3DDrawIndexedInstanced: {
          auto& a = *reinterpret_cast<const D3DDrawIndexedInstancedArguments*>(args_ptr);
          text += fmt::format(" DrawIndexedInstanced {} indices x {} from {} base {}",
                              a.index_count_per_instance, a.instance_count,
                              a.start_index_location, a.base_vertex_location);
        } break;
        case Command::kSetDescriptorHeaps: {
          auto& a = *reinterpret_cast<const SetDescriptorHeapsArguments*>(args_ptr);
          text += fmt::format(" SetDescriptorHeaps view {} sampler {}",
                              static_cast<void*>(a.cbv_srv_uav_descriptor_heap),
                              static_cast<void*>(a.sampler_descriptor_heap));
          for (ID3D12DescriptorHeap* heap :
               {a.cbv_srv_uav_descriptor_heap, a.sampler_descriptor_heap}) {
            if (heap) {
              D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
              text += fmt::format(" [type {} count {} flags {}]", int(desc.Type),
                                  desc.NumDescriptors, int(desc.Flags));
            }
          }
        } break;
        default:
          break;
      }
      return text;
    }
    size_t size = kCommandHeaderSizeElements + header.arguments_size_elements;
    stream += size;
    stream_remaining -= size;
  }
  return "out of range";
}

void* DeferredCommandList::WriteCommand(Command command, size_t arguments_size_bytes) {
  size_t arguments_size_elements =
      (arguments_size_bytes + sizeof(uintmax_t) - 1) / sizeof(uintmax_t);
  size_t offset = command_stream_.size();
  command_stream_.resize(offset + kCommandHeaderSizeElements + arguments_size_elements);
  CommandHeader& header = *reinterpret_cast<CommandHeader*>(command_stream_.data() + offset);
  header.command = command;
  header.arguments_size_elements = uint32_t(arguments_size_elements);
  return command_stream_.data() + (offset + kCommandHeaderSizeElements);
}

}  // namespace rex::graphics::d3d12
