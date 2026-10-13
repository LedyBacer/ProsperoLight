# ps5-native-app-boilerplate - optional bounded host GPU utilization samples.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import time


class GpuSampler:
    def __init__(self, gpu_uuid=None, library=None):
        self.library = None
        self.reason = None
        self.previous_ns = None
        self.selected_uuid = gpu_uuid or os.environ.get('PLTEST_GPU_UUID')
        self.inventory = []
        try:
            if library is None:
                import pynvml
                library = pynvml
            library.nvmlInit()
            self.library = library
            for index in range(library.nvmlDeviceGetCount()):
                gpu = library.nvmlDeviceGetHandleByIndex(index)
                uuid = library.nvmlDeviceGetUUID(gpu)
                name = library.nvmlDeviceGetName(gpu)
                self.inventory.append({'uuid': uuid.decode() if isinstance(uuid, bytes) else uuid,
                    'name': name.decode() if isinstance(name, bytes) else name})
            if self.selected_uuid and not any(g['uuid'] == self.selected_uuid for g in self.inventory):
                self.reason = 'SELECTED_GPU_UUID_NOT_FOUND'
            elif not self.selected_uuid:
                self.reason = 'ENCODE_ADAPTER_UNVERIFIED: configure PLTEST_GPU_UUID; device order is not adapter identity'
        except Exception as exc:
            self.reason = type(exc).__name__

    def sample(self):
        now = time.monotonic_ns()
        result = {'host_monotonic_ns': now, 'gpu_utilization_pct': None,
                  'sampler_period_us': (now-self.previous_ns)/1000 if self.previous_ns is not None else None,
                  'encoder_utilization_pct': None, 'encoder_sample_period_us': None,
                  'memory_used_bytes': None, 'reason': self.reason,
                  'gpu_inventory': self.inventory, 'selected_gpu_uuid': self.selected_uuid,
                  'selection_source': 'explicit operator UUID' if self.selected_uuid else None,
                  'encode_adapter_verified': False,
                  'encode_adapter_reason': 'explicit selection does not prove the host encoder used this adapter',
                  'encoding_latency_ms': None, 'encoding_latency_reason': 'utilization is not per-frame latency'}
        self.previous_ns = now
        if self.library and self.selected_uuid and self.reason is None:
            try:
                lib = self.library
                gpu = lib.nvmlDeviceGetHandleByUUID(self.selected_uuid)
                result['gpu_utilization_pct'] = lib.nvmlDeviceGetUtilizationRates(gpu).gpu
                result['memory_used_bytes'] = lib.nvmlDeviceGetMemoryInfo(gpu).used
                try:
                    util, period = lib.nvmlDeviceGetEncoderUtilization(gpu)
                    result.update(encoder_utilization_pct=util, encoder_sample_period_us=period)
                except Exception as exc:
                    result['encoder_reason'] = type(exc).__name__
            except Exception as exc:
                result['reason'] = type(exc).__name__
        return result

    def close(self):
        if self.library:
            self.library.nvmlShutdown()
            self.library = None
