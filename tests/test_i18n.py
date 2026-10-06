# ps5-native-app-boilerplate - Launcher localization and layout.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class I18nTests(unittest.TestCase):
    def test_catalogs_and_font_coverage(self):
        base=json.loads((ROOT/'assets/locales/en.json').read_text())
        catalogs=list((ROOT/'assets/locales').glob('*.json'))
        self.assertEqual(len(catalogs),31)
        tokens=re.compile(r'%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z%]')
        hashes=json.loads((ROOT/"tools/i18n/catalog-hashes.json").read_text())
        required=set()
        for path in catalogs:
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),hashes[path.name],"regenerate rendered catalogs after editing "+path.name)
            source=json.loads(path.read_text())
            self.assertEqual(source.keys(),base.keys(),path.name)
            rendered=json.loads((path.parent/'rendered'/path.name).read_text())
            for key,value in source.items():
                self.assertTrue(value,path.name+': '+key)
                self.assertEqual(tokens.findall(key),tokens.findall(value),(path.name,key))
                self.assertEqual(tokens.findall(key),tokens.findall(rendered[key]),(path.name,key))
            required.update(ord(c) for value in rendered.values() for c in value if not c.isspace())
        for font in ['inter-regular','inter-semibold','montserrat-medium','dejavu-sans-mono']:
            data=(ROOT/'assets/fonts'/(font+'.huifont')).read_bytes()
            count=struct.unpack_from('<I',data,32)[0]
            glyphs={struct.unpack_from('<I',data,40+24*i)[0] for i in range(count)}
            self.assertFalse(required-glyphs,(font,sorted(required-glyphs)))

    def test_parser_selection_and_fallback(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        code=r'''
#include "i18n.hpp"
#include <cassert>
#include <cstring>
int main(int argc,char **argv) {
 std::map<std::string,std::string> data;
 assert(i18n::parse(R"({"x":"\u042f","a":"\ud83d\ude00"})",data));
 assert(data["x"]=="Я" && data["a"]=="😀");
 assert(!i18n::parse(R"({"x":"a","x":"b"})",data));
 assert(!i18n::parse(R"({"x":"\ud800"})",data));
 assert(!i18n::parse(R"({"x":"\u0000"})",data));
 assert(!i18n::parse(R"({"x":1})",data));
 assert(!i18n::parse(R"({"x":"a"} garbage)",data));
 assert(std::strcmp(i18n::console_language(8),"ru")==0);
 assert(std::strcmp(i18n::console_language(13),"sv")==0);
 assert(std::strcmp(i18n::console_language(25),"el")==0);
 assert(std::strcmp(i18n::console_language(27),"th")==0);
 assert(std::strcmp(i18n::console_language(21),"en")==0);
 assert(std::strcmp(i18n::console_language(999),"en")==0);
 std::snprintf(storage::g_paths.config,sizeof(storage::g_paths.config),"%s/config.bin",argv[2]);
 i18n::initialize(argv[1]);
 assert(std::strcmp(i18n::tr("Settings"),"Settings")==0);
 assert(i18n::select(static_cast<int>(i18n::index("ru"))+1));
 assert(std::strcmp(i18n::tr("Settings"),"Настройки")==0);
 assert(std::strcmp(i18n::tr("Missing key"),"Missing key")==0);
 i18n::catalogs[i18n::active.load()]["%u saved"]="%n";
 assert(std::strcmp(i18n::tr("%u saved"),"%u saved")==0);
 assert(i18n::select(0));
 assert(i18n::selected()==0);
 assert(!i18n::select(-1));
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder);(path/'check.cpp').write_text(code)
            subprocess.run([compiler,'-std=c++20','-pthread','-I'+str(ROOT/'include'),str(path/'check.cpp'),'-o',str(path/'check')],check=True)
            subprocess.run([str(path/'check'),str(ROOT/'assets/locales'),folder],check=True)

if __name__=='__main__': unittest.main()
