/* SPDX-License-Identifier: GPL-3.0-or-later
 * Shared scanout semantics from ProsperoLight's hardware-validated presenter.
 * SDR: BGRA8; HDR10: packed R10 G10 B10 A2 (red in the low bits), BT.2020/PQ output tagging.
 */
#pragma once
#include <stdint.h>
#define VIDEO_OUT_PIXEL_FORMAT_SDR UINT64_C(0x8000000000000000)
#define VIDEO_OUT_PIXEL_FORMAT_HDR UINT64_C(0x8100070422000000)
