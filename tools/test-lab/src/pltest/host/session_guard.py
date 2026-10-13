# ps5-native-app-boilerplate - authenticated host session exclusion.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import hashlib
import re
import ssl
import time
from urllib.parse import urlsplit


async def bounded_response_json(response):
    import json
    data = bytearray()
    async for chunk in response.content.iter_chunked(65536):
        data.extend(chunk)
        if len(data) > 1024*1024: raise ValueError('oversized host JSON response')
    value = json.loads(data)
    if not isinstance(value,dict): raise ValueError('host response must be an object')
    return value


class SessionGuard:
    def __init__(self, url, token, policy='reject', cafile=None, certificate_sha256=None):
        endpoint = urlsplit(url)
        if endpoint.scheme != 'https' or not endpoint.hostname or endpoint.username or endpoint.password or endpoint.query or endpoint.fragment or endpoint.path not in ('', '/'):
            raise ValueError('host API must be an HTTPS origin without credentials')
        if policy not in ('reject', 'disconnect') or not token or len(token) > 4096:
            raise ValueError('invalid host session policy/token')
        self.url, self.token, self.policy = url.rstrip('/'), token, policy
        self.certificate_sha256 = certificate_sha256
        if certificate_sha256 is not None:
            if not isinstance(certificate_sha256, str) or not re.fullmatch(r'[0-9a-f]{64}', certificate_sha256):
                raise ValueError('invalid trusted host certificate SHA256')
            from aiohttp import Fingerprint
            self.ssl = Fingerprint(bytes.fromhex(certificate_sha256))
        else:
            self.ssl = ssl.create_default_context(cafile=cafile)

    async def request(self, method, path, body=None):
        from aiohttp import ClientSession, ClientTimeout
        async with ClientSession(timeout=ClientTimeout(total=10)) as session:
            async with session.request(method, self.url+path, json=body, ssl=self.ssl,
                    headers={'Authorization':'Bearer '+self.token}, allow_redirects=False) as response:
                if response.status != 200:
                    raise RuntimeError('HOST_SESSION_API_UNAVAILABLE:'+str(response.status))
                return await bounded_response_json(response)

    async def inventory(self):
        clients = await self.request('GET','/api/clients/list')
        sessions = await self.request('GET','/api/rtsp/sessions')
        if clients.get('status') is not True or not isinstance(clients.get('named_certs'),list) or not isinstance(sessions.get('sessions'),list):
            raise ValueError('HOST_SESSION_SCHEMA_UNSUPPORTED')
        active = []
        for client in clients['named_certs']:
            if type(client.get('connected')) is not bool: raise ValueError('HOST_SESSION_SCHEMA_UNSUPPORTED')
            if client['connected']:
                uuid = client.get('uuid')
                if not isinstance(uuid,str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,127}',uuid): raise ValueError('invalid paired client identity')
                active.append(uuid)
        return sorted(set(active)), len(sessions['sessions'])

    async def sample_session(self):
        begin = time.monotonic_ns() // 1000
        value = await self.request('GET', '/api/rtsp/sessions')
        end = time.monotonic_ns() // 1000
        sessions = value.get('sessions')
        if not isinstance(sessions, list):
            raise ValueError('HOST_SESSION_SCHEMA_UNSUPPORTED')
        if not sessions:
            return None
        if len(sessions) != 1 or not isinstance(sessions[0], dict):
            raise RuntimeError('HOST_SESSION_EXCLUSIVITY_LOST')
        identity = sessions[0].get('uuid')
        if not isinstance(identity, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,127}', identity):
            raise ValueError('HOST_SESSION_IDENTITY_UNVERIFIED')
        fields = ('uuid', 'width', 'height', 'fps', 'encoder_bitrate_kbps', 'requested_bitrate_kbps',
                  'video_format', 'codec', 'hdr', 'yuv444', 'state', 'frames_sent', 'bytes_sent',
                  'encode_latency_ms', 'last_frame_index', 'stream_gpu_model')
        return {'source': 'Vibepollo /api/rtsp/sessions', 'verified': True,
                'coordinator_start_us': begin, 'coordinator_end_us': end,
                'session': {key: sessions[0].get(key) for key in fields},
                'latency_semantics': 'host processing start to packetization observation; not isolated GPU encode time',
                'gpu_semantics': 'capture display adapter model; not verified encode adapter identity'}

    async def metadata(self):
        value = await self.request('GET', '/api/metadata')
        return {'source':'Vibepollo /api/metadata', 'verified':True,
                'tls_peer_sha256':self.certificate_sha256,
                'tls_identity_source':'TLS fingerprint enforced on this authenticated request' if self.certificate_sha256 else None,
                'version':value.get('version'), 'commit':value.get('commit'),
                'encoder_status':value.get('encoder_status'),
                'semantics':'advertised readiness; not selected encoder or effective session configuration'}

    async def log_bytes(self):
        from aiohttp import ClientSession, ClientTimeout
        data = bytearray()
        async with ClientSession(timeout=ClientTimeout(total=10)) as session:
            async with session.get(self.url+'/api/logs', ssl=self.ssl,
                    headers={'Authorization':'Bearer '+self.token}, allow_redirects=False) as response:
                if response.status != 200: raise RuntimeError('HOST_LOG_API_UNAVAILABLE')
                async for chunk in response.content.iter_chunked(65536):
                    data.extend(chunk)
                    if len(data) > 16*1024*1024: raise ValueError('HOST_LOG_SIZE_LIMIT')
        return bytes(data)

    async def capture_since(self, snapshot):
        data = await self.log_bytes()
        begin = snapshot['bytes']
        if len(data) < begin or hashlib.sha256(data[:begin]).hexdigest() != snapshot['sha256']:
            return {'verified':False, 'reason':'HOST_LOG_ROTATED_OR_REPLACED'}
        text = data[begin:].decode('utf-8', errors='replace')
        outputs = re.findall(r"Capture worker selecting source=(\d+) output='([^'\r\n]+)'", text)
        sizes = re.findall(r'Capture size\s*:\s*(\d+)x(\d+)',text)
        encoders = re.findall(r'Creating encoder \[([A-Za-z0-9_-]+)\]',text)
        return {'verified': bool(outputs) and len(set(outputs)) == 1,
                'source':'Vibepollo authenticated current-session log after idle snapshot',
                'output':outputs[-1][1] if outputs else None,
                'capture_source':int(outputs[-1][0]) if outputs else None,
                'capture_sizes': [list(size) for size in sorted(set((int(w),int(h)) for w,h in sizes))],
                'encoders':sorted(set(encoders)), 'selection_events':len(outputs),
                'reason':None if outputs and len(set(outputs)) == 1 else 'HOST_CAPTURE_OUTPUT_UNVERIFIED'}

    async def ensure_idle(self, cancel):
        active, count = await self.inventory()
        if (active or count) and self.policy == 'reject': raise RuntimeError('HOST_STREAM_BUSY')
        if count and not active: raise RuntimeError('HOST_STREAM_OWNER_UNVERIFIED')
        disconnected = []
        for uuid in active:
            if cancel.is_set(): raise asyncio.CancelledError
            # Paired client UUID, never the distinct RTSP history session UUID.
            response = await self.request('POST','/api/clients/disconnect',{'uuid':uuid})
            if response.get('status') is not True: raise RuntimeError('HOST_DISCONNECT_REJECTED')
            disconnected.append(uuid)
        for _ in range(30):
            if cancel.is_set(): raise asyncio.CancelledError
            active, count = await self.inventory()
            if not active and count == 0:
                return {'verified':True,'disconnected_clients':disconnected,'scope':'paired Moonlight RTSP sessions'}
            await asyncio.sleep(.5)
        raise RuntimeError('HOST_STREAM_STOP_UNCONFIRMED')
