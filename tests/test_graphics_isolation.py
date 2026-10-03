#!/usr/bin/env python3
# ProsperoLight - Independent graphics dependency implementations in one title.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@unittest.skipUnless(all(shutil.which(tool) for tool in
                        ("clang-18", "llvm-ar-18", "llvm-nm-18", "llvm-objcopy-18", "llvm-ranlib-18")),
                     "requires the LLVM 18 build tools")
class GraphicsIsolation(unittest.TestCase):
    def test_linked_backends_use_their_own_private_dependencies(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            sdk = root / "sdk"
            (sdk / "lib").mkdir(parents=True)

            def archive(path, source):
                c = path.with_suffix(".c")
                obj = path.with_suffix(".o")
                c.write_text(source)
                subprocess.run(["clang-18", "-c", "-O0", str(c), "-o", str(obj)], check=True)
                subprocess.run(["llvm-ar-18", "rcs", str(path), str(obj)], check=True)

            archive(sdk / "lib/libgraphics.a",
                    "int mesa_private(void){return 3;} int glSample(void){return mesa_private();}")
            (sdk / "lib/libPS5OpenGL.a").write_text("GROUP (libgraphics.a)\n")
            radv = root / "radv.a"
            archive(radv, "int mesa_private(void){return 7;} int zlib_private(void){return 5;} "
                          "int vkSample(void){return mesa_private()+zlib_private();}")
            archive(root / "libz.a", "int zlib_private(void){return 9;}")
            derived = root / "derived"
            result = subprocess.run([
                sys.executable, str(ROOT / "tools/pyrowave/isolate-opengl.py"),
                str(sdk), str(radv), str(derived), "-L" + str(root), "-lz"
            ], capture_output=True, text=True, check=True)
            gl, vk = result.stdout.strip().splitlines()
            main = root / "main.c"
            main.write_text("int glSample(void); int vkSample(void); int zlib_private(void); "
                            "int main(void){return !(glSample()==3 && vkSample()==12 && zlib_private()==9);}")
            executable = root / "check"
            subprocess.run(["clang-18", str(main), str(pathlib.Path(gl) / "libgraphics.a"),
                            vk, str(root / "libz.a"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
            # Verified original SDK and RADV inputs remain untouched.
            listing = subprocess.check_output(["llvm-nm-18", "--defined-only", str(radv)], text=True)
            self.assertIn("mesa_private", listing)
            self.assertNotIn("pl_radv_", listing)


if __name__ == "__main__":
    unittest.main()
