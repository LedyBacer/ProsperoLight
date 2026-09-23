#!/usr/bin/env bash
# ps5-native-app-boilerplate / ProsperoLight - Shared LLVM selection (source this file).
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# The SDK otherwise picks the newest LLVM on the host, independently of the app.
export LLVM_CONFIG
LLVM_CONFIG=$(command -v "${LLVM_CONFIG:-llvm-config-18}") || {
    echo 'llvm-config-18 is required (install llvm-18-dev).' >&2
    return 2
}
[[ $("$LLVM_CONFIG" --version) == 18.* ]] || {
    echo 'Native builds require LLVM 18 for both the app and dependencies.' >&2
    return 2
}
export PS5_CLANG
PS5_CLANG=$(command -v "${PS5_CLANG:-$("$LLVM_CONFIG" --bindir)/clang}") || return 2
[[ $("$PS5_CLANG" -dumpversion) == 18.* ]] || {
    echo 'PS5_CLANG must also select Clang 18.' >&2
    return 2
}
