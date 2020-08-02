// Copyright 2020- The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX227_MIPI_CCS_REGS_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX227_MIPI_CCS_REGS_H_

// Definitions of sensor registers described by the MIPI CCS specification.

#include <zircon/types.h>

namespace camera {

constexpr uint16_t kSensorModelIdReg = 0x0016;
constexpr uint16_t kModeSelectReg = 0x0100;
constexpr uint16_t kFrameLengthLinesReg = 0x0340;
constexpr uint16_t kLineLengthPckReg = 0x0342;

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX227_MIPI_CCS_REGS_H_
