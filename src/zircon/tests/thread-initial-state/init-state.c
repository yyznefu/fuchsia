// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

extern void thread_entry(uintptr_t arg);

int print_fail(void) {
  EXPECT_TRUE(false, "Failed");
  zx_thread_exit();
  return 1;  // Not reached
}

// create a thread using the raw zircon api.
// cannot use a higher level api because they'll use trampoline functions that'll trash
// registers on entry.
zx_status_t raw_thread_create(void (*thread_entry)(uintptr_t arg), uintptr_t arg,
                              zx_handle_t* out) {
  // preallocated stack to satisfy the thread we create
  static uint8_t stack[1024] __ALIGNED(16);

  zx_handle_t handle;
  zx_status_t status = zx_thread_create(zx_process_self(), "", 0, 0, &handle);
  if (status < 0)
    return status;

  status =
      zx_thread_start(handle, (uintptr_t)thread_entry, (uintptr_t)stack + sizeof(stack), arg, 0);
  if (status < 0) {
    zx_handle_close(handle);
    return status;
  }

  *out = handle;
  return ZX_OK;
}

TEST(TisTests, tis_test) {
  uintptr_t arg = 0x1234567890abcdef;
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_status_t status = raw_thread_create(thread_entry, arg, &handle);
  ASSERT_EQ(status, ZX_OK, "Error while thread creation");

  status = zx_object_wait_one(handle, ZX_THREAD_TERMINATED, ZX_TIME_INFINITE, NULL);
  ASSERT_GE(status, 0, "Error while thread wait");
}
