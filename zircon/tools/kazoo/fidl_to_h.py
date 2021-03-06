#!/usr/bin/env python3.8

# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys


def main():
    with open(sys.argv[1], 'rt') as f:
        json = f.read()
    name = sys.argv[3]
    if name.endswith('.test'):
        name = name[:-5]
    with open(sys.argv[2], 'wt') as f:
        f.write(
            '''// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is generated by //tools/kazoo/BUILD.gn and fidl_to_h.py.

static inline constexpr const char k_%s[] = R"(
%s
)";
''' % (name, json))


if __name__ == '__main__':
    main()
