# ps5-native-app-boilerplate - Box art corruption and colour regression.
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
#include <turbojpeg.h>
#include <webp/encode.h>
#include <fstream>
#include <iterator>
#include <cassert>
#include <cstdlib>
using namespace launcher;

// A 600x800 picture of one colour, as a host's JPEG or WebP box art would be.
static std::vector<unsigned char> Flat() {
    std::vector<unsigned char> bgra(600 * 800 * 4);
    for (std::size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i] = 40; bgra[i + 1] = 120; bgra[i + 2] = 200; bgra[i + 3] = 255;
    }
    return bgra;
}

static void ExpectFlat(const std::vector<unsigned char> &bytes, int tolerance) {
    ArtworkImage image;
    assert(DecodePoster(bytes.data(), bytes.size(), &image));
    assert(image.width == 504 && image.height == 672);
    assert(std::abs(image.rgba[0] - 200) <= tolerance && std::abs(image.rgba[1] - 120) <= tolerance &&
           std::abs(image.rgba[2] - 40) <= tolerance && image.rgba[3] == 255);
    std::vector<unsigned char> cut(bytes.begin(), bytes.begin() + bytes.size() / 2);
    assert(!DecodePoster(cut.data(), cut.size(), &image));
}

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

    const std::vector<unsigned char> flat = Flat();
    tjhandle encoder = tjInitCompress();
    unsigned char *jpeg = nullptr;
    unsigned long jpeg_size = 0;
    assert(tjCompress2(encoder, flat.data(), 600, 0, 800, TJPF_BGRA, &jpeg, &jpeg_size,
                       TJSAMP_444, 95, 0) == 0);
    ExpectFlat(std::vector<unsigned char>(jpeg, jpeg + jpeg_size), 4);
    tjFree(jpeg);
    tjDestroy(encoder);

    uint8_t *webp = nullptr;
    const size_t webp_size = WebPEncodeLosslessBGRA(flat.data(), 600, 800, 600 * 4, &webp);
    assert(webp_size);
    ExpectFlat(std::vector<unsigned char>(webp, webp + webp_size), 0);
    WebPFree(webp);
}
''')
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs',
                                             'libpng', 'libturbojpeg', 'libwebp'], text=True).split()
            subprocess.run([os.environ.get('HOST_CXX', 'clang++'), '-std=c++20', '-O2',
                            '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'include'),
                            str(out / 'check.cpp'), str(ROOT / 'src/launcher/launcher_artwork.cpp'),
                            *flags, '-o', str(out / 'check')], check=True)
            subprocess.run([str(out / 'check'), *[str(out / n) for n in
                            ['valid.png', 'tall.png', 'huge.png', 'bad.png', 'truncated.png']]], check=True)
