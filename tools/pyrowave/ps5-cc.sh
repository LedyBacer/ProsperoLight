#!/bin/sh
# Upstream's proven compiler wrapper is intentionally invoked through sh.
: "${PS5_VULKAN_ROOT:?PS5_VULKAN_ROOT is required}"
exec sh "$PS5_VULKAN_ROOT/tooling/prospero-clang18" "$@"
