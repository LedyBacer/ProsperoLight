# ps5-native-app-boilerplate - PNG artwork corruption and colour regression.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]


def png(width, height, pixels):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


class ArtworkDecoderTests(unittest.TestCase):
    def test_pixels_scaling_and_bad_input(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / 'valid.png').write_bytes(png(2, 1, b'\x00\xff\x00\x00\xff\x00\x00\xff\x80'))
            (out / 'tall.png').write_bytes(png(1, 1344, b'\x00\x10\x20\x30\xff' * 1344))
            (out / 'huge.png').write_bytes(png(100000, 100000, b''))
            (out / 'bad.png').write_bytes(b'not a PNG')
            (out / 'truncated.png').write_bytes((out / 'valid.png').read_bytes()[:40])
            (out / 'check.cpp').write_text(r'''
#include "launcher/launcher_artwork.hpp"
#include "launcher/launcher_model.hpp"
#include <fstream>
#include <iterator>
#include <cassert>
using namespace launcher;
int main(int argc, char **argv) {
    for (int n = 1; n < argc; ++n) {
        std::ifstream in(argv[n], std::ios::binary);
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), {}};
        ArtworkImage image;
        bool ok = DecodePoster(bytes.data(), bytes.size(), &image);
        if (n == 1) {
            assert(ok && image.width == 2 && image.height == 1);
            assert((image.rgba == std::vector<unsigned char>{255,0,0,255,0,0,255,128}));
        } else if (n == 2) {
            assert(ok && image.width == 1 && image.height == 672);
            assert(image.rgba[0] == 16 && image.rgba[1] == 32 && image.rgba[2] == 48);
        } else assert(!ok);
    }
    assert(!DecodePoster(nullptr, 0, nullptr));
}
''')
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libpng'], text=True).split()
            subprocess.run([os.environ.get('HOST_CXX', 'clang++'), '-std=c++20', '-O2',
                            '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'include'),
                            str(out / 'check.cpp'), str(ROOT / 'src/launcher/launcher_artwork.cpp'),
                            *flags, '-o', str(out / 'check')], check=True)
            subprocess.run([str(out / 'check'), *[str(out / n) for n in
                            ['valid.png', 'tall.png', 'huge.png', 'bad.png', 'truncated.png']]], check=True)
