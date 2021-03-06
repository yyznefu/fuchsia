// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_METADATA_SPI_H_
#define SRC_LIB_DDK_INCLUDE_DDK_METADATA_SPI_H_

#include <inttypes.h>

typedef struct {
  uint32_t bus_id;
  uint32_t cs;
  uint32_t vid;
  uint32_t pid;
  uint32_t did;
} spi_channel_t;

#endif  // SRC_LIB_DDK_INCLUDE_DDK_METADATA_SPI_H_
