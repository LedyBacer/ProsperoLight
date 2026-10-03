# ps5-native-app-boilerplate - Host policy regression tests.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class EndpointPolicy(unittest.TestCase):
    def test_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "policy.c"
            source.write_text('#include <assert.h>\n#include "server_endpoint.h"\nint main(void) {\n char host[64]; uint16_t port;\n assert(server_endpoint_parse("192.168.1.5:48000",host,sizeof(host),47989,&port));\n assert(server_port_parse("65535",&port) && port==65535);\n assert(!server_port_parse("65536",&port));\n assert(!server_port_parse("-1",&port));\n assert(!server_port_parse("80junk",&port));\n assert(server_endpoint_parse("192.168.1.5:48000",host,sizeof(host),47989,&port));\n assert(port==48000 && !strcmp(host,"192.168.1.5"));\n assert(server_endpoint_parse("192.168.1.5",host,sizeof(host),47989,&port) && port==47989);\n const char *invalid[]={"","a:0","a:65536","a:99999999999999","a:","a:12x","a:1:2","a b:10","http://a:10"};\n for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) assert(!server_endpoint_parse(invalid[i],host,sizeof(host),47989,&port));\n return 0;\n}\n')
            binary = pathlib.Path(directory) / "policy"
            subprocess.run(['cc', "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "include"), str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
