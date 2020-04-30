// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/shadertoy/service/view_shadertoy.h"

namespace shadertoy {

ShadertoyStateForView::ShadertoyStateForView(App* app, zx::eventpair view_token,
                                             bool handle_input_events)
    : ShadertoyState(app) {
  FX_CHECK(false) << "not implemented";
}

void ShadertoyStateForView::OnSetResolution() { FX_CHECK(false) << "not implemented"; }

}  // namespace shadertoy
