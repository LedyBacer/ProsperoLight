# ps5-native-app-boilerplate - single launch request, bounded matching receipt.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import time
import re
from ftplib import error_perm
from launch_spike import ROOT, request_bytes, stage, stage_profile, read_receipt, elf_bytes


def helper(ftp, loader, elf, run_id, pc, expected_route, timeout=20):
    if re.findall(rb'PLTEST_HELPER_ROUTE=([0-4])(?:\x00|\n)', elf_bytes(elf)) != [str(expected_route).encode()]:
        raise ValueError('helper route marker mismatch; refusing execution')
    stage(ftp, request_bytes(run_id, pc))
    loader.execute(ftp, elf)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            receipt = read_receipt(ftp, ROOT + '/' + run_id + '.json')
        except error_perm as exc:
            if not str(exc).startswith('550'):
                raise
        else:
            if receipt.get('schema') != 'prosperolight-launch-spike/1' or receipt.get('request_id') != run_id:
                raise ValueError('helper receipt identity mismatch')
            if receipt.get('route') != expected_route:
                raise ValueError('helper compiled for wrong route')
            if receipt.get('state') != 'preflight':
                return receipt
        time.sleep(.2)
    raise TimeoutError('helper receipt absent; launch status unknown; do not retry launch')


def launch(ftp, loader, elf, profile):
    stage_profile(ftp, profile, profile['run_id'])
    result = helper(ftp, loader, elf, profile['run_id'], profile['client']['host'], 4)
    if result.get('state') != 'LAUNCH_ACCEPTED':
        raise RuntimeError('title launch refused or unverified: ' + str(result.get('state')))
    return result
