#!/usr/bin/env python3
# ps5-native-app-boilerplate - Launcher localization and layout.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bake-time shaping for immutable translated text. No shaping dependency on PS5.
Requires fonttools, uharfbuzz, python-bidi, arabic-reshaper on the build host.
Inputs: readable UTF-8 catalogs and Noto font sources supplied as --fonts.
Outputs: rendered catalogs, font subsets, glyph map and codepoint list.
"""
import argparse, json, re, hashlib
from pathlib import Path
import uharfbuzz as hb
from fontTools.ttLib import TTFont
from fontTools import subset
from fontTools.varLib.instancer import instantiateVariableFont
from bidi.algorithm import get_display
import arabic_reshaper

parser=argparse.ArgumentParser();parser.add_argument('--fonts',type=Path,required=True);parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);args=parser.parse_args()
root=args.root;names=['NotoSans','NotoSansArabic','NotoSansHebrew','NotoSansDevanagari','NotoSansTamil','NotoSansThai','NotoSansSC','NotoSansTC','NotoSansJP','NotoSansKR']
out=root/'third_party/fonts/i18n';out.mkdir(parents=True,exist_ok=True)
faces={};cmaps={};raw={}
for name in names:
 f=TTFont(args.fonts/(name+'.ttf'))
 if 'fvar' in f:
  axes={a.axisTag:(400 if a.axisTag=='wght' else a.defaultValue) for a in f['fvar'].axes}
  f=instantiateVariableFont(f,axes,inplace=True)
 f.save(out/(name+'.ttf'));raw[name]=(out/(name+'.ttf')).read_bytes();faces[name]=f;cmaps[name]=f.getBestCmap()
font_objects={n:hb.Font(hb.Face(raw[n])) for n in names}
shaped={};records=[];used={n:set() for n in names};characters=set(range(32,127));next_cp=0xF0000
name_labels={}
# Native language labels always need to remain legible, even on the English screen.
language_array=(root/'include/i18n.hpp').read_text().split('languages[] = {',1)[1].split('};',1)[0]
for code,name in re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}',language_array):
 name_labels[code]=name
assert len(name_labels)==31

def face_for(text):
 for pattern,name in [(r'[\u0600-\u06ff]','NotoSansArabic'),(r'[\u0590-\u05ff]','NotoSansHebrew'),(r'[\u0900-\u097f]','NotoSansDevanagari'),(r'[\u0b80-\u0bff]','NotoSansTamil'),(r'[\u0e00-\u0e7f]','NotoSansThai')]:
  if re.search(pattern,text):return name
 return None

def shape_piece(text):
 global next_cp
 name=face_for(text)
 if not name:return text
 if name=='NotoSansArabic':text=arabic_reshaper.reshape(text)
 if name in ('NotoSansArabic','NotoSansHebrew'):text=get_display(text)
 buf=hb.Buffer();buf.add_str(text);buf.guess_segment_properties();buf.direction='ltr'
 hb.shape(font_objects[name],buf,{'kern':True})
 output=[]
 for info,pos in zip(buf.glyph_infos,buf.glyph_positions):
  if info.codepoint==0:
   char=text[info.cluster]
   if not any(ord(char) in cmap for cmap in cmaps.values()):
    raise ValueError(f'Missing U+{ord(char):04X} in {name}: {text}')
   output.append(char);continue
  if text[info.cluster:info.cluster+1]==' ' and pos.x_offset==0 and pos.y_offset==0:
   output.append(' ');continue
  key=(name,info.codepoint,pos.x_advance,pos.x_offset,pos.y_offset)
  if key not in shaped:
   shaped[key]=next_cp;records.append((next_cp,names.index(name)+1,info.codepoint,pos.x_advance,pos.x_offset,pos.y_offset));next_cp+=1
  output.append(chr(shaped[key]));used[name].add(info.codepoint)
 return ''.join(output)

# Keep printf tokens byte-exact and in their original argument order. The
# surrounding immutable runs are shaped independently; host names/numbers
# remain runtime values rather than part of the baked translation.
format_token=re.compile(r'(%(?:\d+\$)?[-+0 #]*\d*(?:\.\d+)?[a-zA-Z%]|\n)')
def render(text):return ''.join(piece if format_token.fullmatch(piece) else shape_piece(piece) for piece in format_token.split(text))
rendered=root/'assets/locales/rendered';rendered.mkdir(exist_ok=True)
for path in sorted((root/'assets/locales').glob('*.json')):
 catalog=json.loads(path.read_text());catalog.update({v:v for v in name_labels.values()})
 logical=dict(catalog);result={k:render(v) for k,v in catalog.items()}
 for v in logical.values():characters.update(map(ord,v))
 for v in result.values():characters.update(map(ord,v))
 (rendered/path.name).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
# Retain glyph IDs because the SDF baker refers to shaped IDs explicitly.
for name,f in faces.items():
 opts=subset.Options();opts.retain_gids=True;opts.layout_features=['*']
 sub=subset.Subsetter(options=opts);sub.populate(unicodes=[c for c in characters if c in cmaps[name]],gids=used[name]);sub.subset(f);f.save(out/(name+'.ttf'))
 (out/(name+'-LICENSE.txt')).write_bytes((args.fonts/(name+'-LICENSE.txt')).read_bytes())
(root/'tools/i18n/codepoints.txt').write_text(''.join(f'{c:x}\n' for c in sorted(characters)))
(root/'tools/i18n/glyphs.tsv').write_text(''.join(' '.join(map(str,row))+'\n' for row in records))
provenance={'fonts':{name:hashlib.sha256(raw[name]).hexdigest() for name in names},'shaped_glyphs':len(records),'codepoints':len(characters)}
(root/'tools/i18n/fonts-provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
print(provenance['shaped_glyphs'],'shaped glyphs;',len(characters),'codepoints')

(root/'tools/i18n/catalog-hashes.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'assets/locales').glob('*.json'))},indent=2)+'\n')
