#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
task test
python3 scripts/quality.py format-check
python3 scripts/quality.py tidy --build build/dev
cmake --build build/dev --target qml-lint
python3 scripts/check-qml-import-policy.py qml
python3 scripts/quality.py qmltypes --build build/dev --module Holonight.FileBrowser --module Holonight.FileBrowser.Preview --module Holonight.FileBrowser.Markdown
reuse lint
