#!/usr/bin/env python3
# ps5-native-app-boilerplate - Compose the controller extension with optional codec patches.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import re

root = pathlib.Path(__file__).resolve().parents[2]
common = root / 'third_party/moonlight-common-c/src'
# The tracked patch is the exact original-revision diff. Use named anchors here
# so this independent extension also composes with PyroWave's SDP/include edits.
patch = (root / 'tools/controllers/0001-vibepollo-haptics.patch').read_text()
header = patch.split('+++ b/src/ControllerHaptics.h\n', 1)[1]
header = ''.join(line[1:] for line in header.splitlines(True) if line.startswith('+'))
p = common / 'ControllerHaptics.h'
if p.exists() and p.read_text() != header:
    raise SystemExit('Conflicting ControllerHaptics.h; refusing to overwrite')
p.write_text(header)
for name in ('Limelight.h', 'ControlStream.c', 'SdpGenerator.c'):
    p = common / name
    s = p.read_text()
    if name == 'Limelight.h':
        if 'ConnListenerControllerHaptics' not in s:
            anchor = 'typedef struct _CONNECTION_LISTENER_CALLBACKS {'
            if s.count(anchor) != 1:
                raise SystemExit('Unrecognized Moonlight callback ABI')
            s = s.replace(anchor, '// Bounded Vibepollo PCM callback; receive thread must not block.\n'
                'typedef void(*ConnListenerControllerHaptics)(uint16_t controllerNumber, uint32_t sequence, const uint8_t* pcm, uint16_t frames);\n\n' + anchor)
            anchor = '    ConnListenerSetAdaptiveTriggers setAdaptiveTriggers;'
            if s.count(anchor) != 1:
                raise SystemExit('Missing adaptive trigger callback ABI')
            s = s.replace(anchor, anchor + '\n    ConnListenerControllerHaptics controllerHaptics;')
    else:
        if '#include "ControllerHaptics.h"' not in s:
            anchor = '#include "Limelight-internal.h"'
            if s.count(anchor) != 1:
                raise SystemExit('Unrecognized Moonlight include layout')
            s = s.replace(anchor, anchor + '\n#include "ControllerHaptics.h"')
        if name == 'SdpGenerator.c' and 'moonlightFeatureFlags |= ML_FF_HAPTICS_PCM' not in s:
            s, count = re.subn(r'(uint32_t moonlightFeatureFlags = [^;]+;)',
                r'\1\n        if (ListenerCallbacks.controllerHaptics) moonlightFeatureFlags |= ML_FF_HAPTICS_PCM;', s)
            if count != 1:
                raise SystemExit('Unrecognized SDP feature negotiation')
        if name == 'ControlStream.c' and 'ctlHdr->type == ML_HAPTICS_PACKET_TYPE' not in s:
            block = patch.split('+            if (ctlHdr->type == ML_HAPTICS_PACKET_TYPE) {', 1)[1]
            block = '+            if (ctlHdr->type == ML_HAPTICS_PACKET_TYPE) {' + block
            block = block.split('+            else if (needsAsyncCallback', 1)[0]
            block = ''.join(line[1:] for line in block.splitlines(True) if line.startswith('+'))
            anchor = '            if (needsAsyncCallback(ctlHdr->type)) {'
            if s.count(anchor) != 1:
                raise SystemExit('Unrecognized control receive dispatch')
            s = s.replace(anchor, block + '            else if (needsAsyncCallback(ctlHdr->type)) {')
    p.write_text(s)
