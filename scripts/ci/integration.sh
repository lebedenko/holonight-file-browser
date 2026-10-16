#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DBUILD_FILE_BROWSER_QUICK=OFF -DBUILD_FILE_BROWSER_SEARCH=OFF \
  -DBUILD_FILE_BROWSER_STORAGE=OFF -DBUILD_FILE_BROWSER_PREVIEW=OFF \
  -DBUILD_FILE_BROWSER_PREVIEW_QUICK=OFF -DBUILD_FILE_BROWSER_MARKDOWN=OFF
cmake --build build/core
ctest --test-dir build/core --output-on-failure --no-tests=error
