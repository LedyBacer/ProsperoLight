# ps5-native-app-boilerplate - one-shot loader transport, never launch proof.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
from ftplib import error_perm
from urllib.parse import quote
from urllib.request import urlopen
from launch_spike import elf_bytes, send_elf
from .ftp import upload


class ElfLoader:
    def __init__(self, host, port=9021, manager_port=None):
        self.host, self.port, self.manager_port = host, port, manager_port

    def execute(self, ftp, path):
        data = elf_bytes(path)
        if self.manager_port is None:
            send_elf(self.host, self.port, data)
            return
        root = '/data/pldmgr/payloads/prosperolight-test-launcher-pr0'
        try:
            ftp.mkd(root)
        except error_perm:
            ftp.cwd(root)
        remote = root + '/prosperolight-test-launcher-pr0.elf'
        upload(ftp, remote, data)
        url = f'http://{self.host}:{self.manager_port}/loadpayload:' + quote(remote, safe='/')
        with urlopen(url, timeout=10) as response:
            if response.status != 200:
                raise RuntimeError('loader refused one-shot helper')
