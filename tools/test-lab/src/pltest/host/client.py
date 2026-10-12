# ps5-native-app-boilerplate - bounded authenticated Windows worker client.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import asyncio
import time
import uuid
from .protocol import SCHEMA


class HostWorkerAdapter:
    def __init__(self, url, token):
        if not url.startswith(('http://', 'https://')):
            raise ValueError('expected worker HTTP origin')
        self.url, self.token = url.rstrip('/'), token
        self.session = self.socket = None

    async def __aenter__(self):
        from aiohttp import ClientSession, ClientTimeout
        self.session = ClientSession(timeout=ClientTimeout(total=10))
        try:
            self.socket = await self.session.ws_connect(self.url + '/ws',
                headers={'Authorization': 'Bearer ' + self.token}, heartbeat=5, max_msg_size=16 * 1024 * 1024)
        except BaseException:
            await self.session.close()
            raise
        return self

    async def __aexit__(self, *args):
        if self.socket:
            await self.socket.close()
        if self.session:
            await self.session.close()

    async def command(self, run_id, action, settings=None, timeout=90):
        identifier = str(uuid.uuid4())
        message = {'schema': SCHEMA, 'run_id': run_id, 'command_id': identifier, 'action': action}
        if settings is not None:
            message['settings'] = settings
        await self.socket.send_json(message)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            response = await self.socket.receive_json(timeout=max(.1, deadline - time.monotonic()))
            if not isinstance(response, dict) or response.get('schema') != SCHEMA:
                raise ValueError('worker protocol mismatch')
            if response.get('command_id') == identifier:
                if response.get('ack') != 'accepted' or response.get('run_id') != run_id:
                    raise RuntimeError('worker command rejected: ' + str(response.get('reason')))
                return response
        raise TimeoutError('worker ACK absent')

    async def wait_report(self, run_id, timeout=120):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = await self.command(run_id, 'status', timeout=10)
            if result.get('state') in ('COMPLETED', 'INCONCLUSIVE'):
                return result['report']
            await asyncio.sleep(1)
        raise TimeoutError('host report absent')
