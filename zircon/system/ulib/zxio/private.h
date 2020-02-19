// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZXIO_PRIVATE_H_
#define ZIRCON_SYSTEM_ULIB_ZXIO_PRIVATE_H_

#include <lib/zxio/extensions.h>
#include <lib/zxio/zxio.h>
#include <lib/zx/event.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

#include <algorithm>

namespace {

template <typename F>
zx_status_t zxio_do_vector(const zx_iovec_t* vector, size_t vector_count, size_t* out_actual,
                           F fn) {
  size_t total = 0;
  for (size_t i = 0; i < vector_count; ++i) {
    size_t actual;
    zx_status_t status = fn(vector[i].buffer, vector[i].capacity, &actual);
    if (status != ZX_OK) {
      // This can't be `i > 0` because the first buffer supplied by the caller
      // might've been of length zero, and we may never have attempted an I/O
      // operation with it.
      if (total > 0) {
        break;
      }
      return status;
    }
    total += actual;
    if (actual != vector[i].capacity) {
      // Short.
      break;
    }
  }
  *out_actual = total;
  return ZX_OK;
}

template <typename F>
zx_status_t zxio_vmo_do_vector(size_t start, size_t length, size_t* offset,
                               const zx_iovec_t* vector, size_t vector_count, size_t* out_actual,
                               F fn) {
  if (*offset > length) {
    return ZX_ERR_INVALID_ARGS;
  }
  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* buffer, size_t capacity, size_t* out_actual) {
                          capacity = std::min(capacity, length - *offset);
                          zx_status_t status = fn(buffer, start + *offset, capacity);
                          if (status != ZX_OK) {
                            return status;
                          }
                          *offset += capacity;
                          *out_actual = capacity;
                          return ZX_OK;
                        });
}

}  // namespace

bool zxio_is_valid(const zxio_t* io);

zx_status_t zxio_datagram_pipe_read_vector(zxio_t* io, const zx_iovec_t* vector,
                                           size_t vector_count, zxio_flags_t flags,
                                           size_t* out_actual);

zx_status_t zxio_datagram_pipe_write_vector(zxio_t* io, const zx_iovec_t* vector,
                                            size_t vector_count, zxio_flags_t flags,
                                            size_t* out_actual);

zx_status_t zxio_vmo_seek(zxio_t* io, zxio_seek_origin_t start, int64_t offset, size_t* out_offset);

void zxio_node_init(zxio_node_t* node, zx_handle_t control, const zxio_extension_ops_t* ops);

// Common functionalities shared by the fuchsia.io v1 |node| and |remote| transports.
// These operate on the raw FIDL channel directly, as |node| and |remote|
// have different object layouts.

// Send a |fuchsia.io/Node.Close| message on |control|. Note: does not close the channel.
zx_status_t zxio_raw_remote_close(zx::unowned_channel control);

zx_status_t zxio_raw_remote_clone(zx::unowned_channel source, zx_handle_t* out_handle);

zx_status_t zxio_raw_remote_attr_get(zx::unowned_channel control, zxio_node_attr_t* out_attr);

zx_status_t zxio_raw_remote_attr_set(zx::unowned_channel control, const zxio_node_attr_t* attr);

#endif  // ZIRCON_SYSTEM_ULIB_ZXIO_PRIVATE_H_
