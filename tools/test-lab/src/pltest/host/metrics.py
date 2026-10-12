# ps5-native-app-boilerplate - optional bounded host GPU utilization samples.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import time


class GpuSampler:
    def __init__(self):
        self.library = None
        self.reason = None
        try:
            import pynvml
            pynvml.nvmlInit()
            self.library = pynvml
        except Exception as exc:
            self.reason = type(exc).__name__

    def sample(self):
        result = {'host_monotonic_ns': time.monotonic_ns(), 'gpu_utilization_pct': None,
                  'encoder_utilization_pct': None, 'encoder_sample_period_us': None,
                  'memory_used_bytes': None, 'reason': self.reason,
                  'encoding_latency_ms': None, 'encoding_latency_reason': 'utilization is not per-frame latency'}
        if self.library:
            try:
                lib = self.library
                gpu = lib.nvmlDeviceGetHandleByIndex(0)
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
