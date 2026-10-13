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
        self.reader = None
        self.pending = {}
        self.reader_error = None

    async def __aenter__(self):
        from aiohttp import ClientSession, ClientTimeout
        self.session = ClientSession(timeout=ClientTimeout(total=10))
        try:
            self.socket = await self.session.ws_connect(self.url + '/ws',
                headers={'Authorization': 'Bearer ' + self.token}, heartbeat=5, max_msg_size=16 * 1024 * 1024)
        except BaseException:
            await self.session.close()
            raise
        self.reader = asyncio.create_task(self._read(), name="pltest-worker-reader")
        return self

    async def __aexit__(self, *args):
        if self.socket:
            await self.socket.close()
        if self.reader:
            self.reader.cancel()
            await asyncio.gather(self.reader, return_exceptions=True)
        if self.session:
            await self.session.close()

    async def _read(self):
        from aiohttp import WSMsgType
        error = ConnectionError('worker connection closed')
        try:
            async for message in self.socket:
                if message.type != WSMsgType.TEXT:
                    continue
                value = message.json()
                if not isinstance(value, dict) or value.get('schema') != SCHEMA:
                    raise ValueError('worker protocol mismatch')
                identifier = value.get('command_id')
                if isinstance(identifier, str):
                    future = self.pending.get(identifier)
                    if future and not future.done():
                        future.set_result(value)
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            error = exc
        finally:
            self.reader_error = error
            for future in self.pending.values():
                if not future.done():
                    future.set_exception(error)

    async def command(self, run_id, action, settings=None, timeout=90):
        identifier = str(uuid.uuid4())
        message = {'schema': SCHEMA, 'run_id': run_id, 'command_id': identifier, 'action': action}
        if settings is not None:
            message['settings'] = settings
        if self.reader_error:
            raise self.reader_error
        if len(self.pending) >= 64:
            raise RuntimeError('worker pending command quota exceeded')
        future = asyncio.get_running_loop().create_future()
        self.pending[identifier] = future
        try:
            await self.socket.send_json(message)
            response = await asyncio.wait_for(future, timeout=timeout)
            if response.get('ack') != 'accepted' or response.get('run_id') != run_id:
                if response.get('reason_code') == 'TimeoutError':
                    raise TimeoutError('HOST_WORKER_COMMAND_TIMEOUT:' + action)
                if response.get('reason') in ('UNSUPPORTED_WEBGL2', 'UNSUPPORTED_WEBGL2_SHADER', 'UNSUPPORTED_WEBGL2_PROGRAM',
                    'UNSUPPORTED_HDR_DISPLAY','UNSUPPORTED_HDR_CURRENT_DISPLAY_MODE','UNSUPPORTED_DISPLAY_RESOLUTION'):
                    raise RuntimeError(response['reason'])
                raise RuntimeError('worker command rejected: ' + str(response.get('reason')))
            return response
        finally:
            self.pending.pop(identifier, None)
            if not future.done():
                future.cancel()


    async def wait_report(self, run_id, timeout=120):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            result = await self.command(run_id, 'status', timeout=10)
            if result.get('state') in ('COMPLETED', 'INCONCLUSIVE'):
                return result['report']
            await asyncio.sleep(1)
        raise TimeoutError('host report absent')
