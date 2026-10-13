# ps5-native-app-boilerplate - bounded host-local log slice without credentials.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import math
import re
from pathlib import Path


def redact(text):
    text = re.sub(r'(?im)(authorization\s*[:=]\s*)[^\r\n]+', r'\1[REDACTED]', text)
    return re.sub(r'(?i)((?:password|token|secret|apikey|api_key|rikey)\s*[=:]\s*)[^\s,;&]+', r'\1[REDACTED]', text)


class LogSlice:
    def __init__(self, path=None):
        self.path = Path(path) if path else None
        self.start = self.path.stat().st_size if self.path and self.path.is_file() else None

    def finish(self, output):
        if self.start is None:
            return {'supported': False, 'reason': 'no readable host-local log configured'}
        size = self.path.stat().st_size
        rotated = size < self.start
        start = 0 if rotated else self.start
        truncated = size - start > 4 * 1024 * 1024
        if truncated:
            start = size - 4 * 1024 * 1024
        with self.path.open('rb') as source:
            source.seek(start)
            text = source.read(4 * 1024 * 1024).decode('utf-8', errors='replace')
        output.write_text(redact(text), encoding='utf-8')
        calls = []
        for match in re.finditer(r'Video encode call duration \(min/max/avg\): ([0-9.]+)ms/([0-9.]+)ms/([0-9.]+)ms', text):
            try:
                minimum, maximum, average = map(float, match.groups())
            except ValueError:
                continue
            if all(math.isfinite(value) for value in (minimum, maximum, average)) and 0 <= minimum <= average <= maximum:
                calls.append(dict(minimum_ms=minimum, maximum_ms=maximum, average_ms=average))
        return {'supported': True, 'rotated': rotated, 'truncated': truncated,
                'bytes': output.stat().st_size, 'credentials': 'host-local; omitted from report',
                'encode_cpu_call_summaries': calls or None,
                'encode_cpu_call_source': 'host debug log: Video encode call duration (min/max/avg)',
                'encode_cpu_call_reason': None if calls else 'host did not log encode CPU call summaries',
                'per_frame_capture_timestamps': None, 'per_frame_encode_timestamps': None,
                'per_frame_reason': 'aggregate encode call logs are not per-frame GPU or capture completion timestamps'}


def server_inventory():
    import json
    import platform
    import subprocess
    if platform.system() != 'Windows':
        return {'supported': False, 'reason': 'WINDOWS_REQUIRED', 'servers': []}
    script = """[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding;
@(Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^(sunshine|vibepollo|apollo)\\.exe$'} |
ForEach-Object { $p=$_.ExecutablePath; [pscustomobject]@{name=$_.Name;version=if($p){(Get-Item $p).VersionInfo.ProductVersion}else{$null};version_reason=if(!$p){'running executable path unavailable to interactive user'}else{$null};version_source='Windows executable ProductVersion';per_frame_metrics=$null} }) | ConvertTo-Json -Compress"""
    try:
        result = subprocess.run(['powershell', '-NoProfile', '-Command', script],
            capture_output=True, timeout=5, check=True)
        raw = result.stdout.decode('utf-8-sig').strip()
        servers = json.loads(raw) if raw else []
        if isinstance(servers, dict):
            servers = [servers]
        return {'supported': True, 'servers': servers,
                'per_frame_metrics_reason': 'no documented host per-frame timestamp adapter configured',
                'authenticated_api_logs': None, 'api_logs_reason': 'no host-local API credentials configured'}
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        return {'supported': False, 'reason': type(exc).__name__, 'servers': []}
