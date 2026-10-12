# ps5-native-app-boilerplate - authenticated host session exclusion.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import re
import ssl
from urllib.parse import urlsplit


class SessionGuard:
    def __init__(self, url, token, policy='reject', cafile=None):
        endpoint = urlsplit(url)
        if endpoint.scheme != 'https' or not endpoint.hostname or endpoint.username or endpoint.password or endpoint.query or endpoint.fragment or endpoint.path not in ('', '/'):
            raise ValueError('host API must be an HTTPS origin without credentials')
        if policy not in ('reject', 'disconnect') or not token or len(token) > 4096:
            raise ValueError('invalid host session policy/token')
        self.url, self.token, self.policy = url.rstrip('/'), token, policy
        self.ssl = ssl.create_default_context(cafile=cafile)

    async def request(self, method, path, body=None):
        from aiohttp import ClientSession, ClientTimeout
        async with ClientSession(timeout=ClientTimeout(total=10)) as session:
            async with session.request(method, self.url+path, json=body, ssl=self.ssl,
                    headers={'Authorization':'Bearer '+self.token}, allow_redirects=False) as response:
                if response.status != 200:
                    raise RuntimeError('HOST_SESSION_API_UNAVAILABLE:'+str(response.status))
                raw = await response.content.read(1024*1024+1)
                if len(raw) > 1024*1024: raise ValueError('oversized host session inventory')
                import json
                return json.loads(raw)

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
