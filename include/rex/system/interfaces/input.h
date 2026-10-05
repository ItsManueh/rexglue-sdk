/**
 * @file        system/interfaces/input.h
 * @brief       Abstract input system interface for dependency injection
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

#include <cstdint>
#include <functional>

#include <rex/system/xtypes.h>

namespace rex::system {

class IInputSystem {
 public:
  virtual ~IInputSystem() = default;
  virtual X_STATUS Setup() = 0;
  virtual void Shutdown() = 0;

  /// Bit N set = guest user N has a controller (user 0: any device, including keyboard/mouse).
  virtual uint32_t ConnectedUserMask() const { return 1; }
  /// Called with the new mask whenever it changes (from the thread that polls input).
  virtual void SetUsersChangedCallback(std::function<void(uint32_t)> callback) { (void)callback; }
};

}  // namespace rex::system
