# Launcher localization

`assets/locales/<language>.json` contains human-readable UTF-8 translations,
keyed by the original English text. English is the fallback for a missing,
empty or incompatible entry. The language preference is stored atomically in
`config/prosperolight-language.json`, independently of codec settings and pairing.

The language list matches Moonlight Qt's 30 translation catalogs plus English:
`bg ckb cs de el eo es et fr he hi hu it ja ko lt nb_NO nl pl pt pt_BR ru sv ta th tr uk vi zh_CN zh_TW`.
Both Portuguese variants and both Chinese variants remain separate.

Matching finished translations were adapted from Moonlight Qt at commit
`de2467e433821664cdd2224aad8c89a625be1ad9`:
https://github.com/moonlight-stream/moonlight-qt/tree/de2467e433821664cdd2224aad8c89a625be1ad9/app/languages
The reused entries are recorded in `moonlight-provenance.json`. Moonlight Qt
and this application are GPL-3.0 software. Please retain the translation credit.
Remaining copy was authored for ProsperoLight. Some longer descriptions use
compact localized wording/technical notation to fit the console UI. These
translations should receive native-speaker review; do not describe them as
professionally reviewed translations.

Automatic reads `sceSystemServiceParamGetInt(1, &language)` at application start.
Regional English maps to English, Canadian French to French, Latin American
Spanish to Spanish. Console languages absent from Moonlight Qt fall back to
English. Manual selection takes effect immediately; Automatic is re-evaluated
on the next application start. Unknown/error results fall back to English.

## Updating translations and fonts

The UI toolkit lays out baked glyphs; it has no runtime shaping engine. A
build-host preparation step shapes immutable translated runs (Hindi, Tamil,
Thai, Hebrew, Central Kurdish), with RTL visual ordering where needed, into
private-use glyphs. Runtime host names and printf parameters remain dynamic.
This does not provide a general bidirectional layout engine for arbitrary host
names or paragraphs: mixed-direction text and long RTL wrapping need visual
review. The language picker and core copy use short strings to limit wrapping.

Readable catalogs remain the source of truth. `assets/locales/rendered/*.json`,
font subsets, `codepoints.txt`, `glyphs.tsv`, and the four `.huifont` files are
committed generated assets. A normal PS5 build needs no Qt, HarfBuzz or Python
shaping packages. To update them after changing a translation:

1. Obtain the Noto font source files listed in `fonts-provenance.json`; retain
   their SIL Open Font License files. Supply them as a directory to `--fonts`.
2. On the build host install `fonttools`, `uharfbuzz`, `python-bidi`, and
   `arabic-reshaper` in an isolated Python environment.
3. Run `python tools/i18n/prepare_fonts.py --fonts /path/to/noto-fonts`.
4. Run `bash tools/bake-fonts.sh`.
5. Run `python3 -m unittest discover -s tests -p test_i18n.py`.

Noto fonts: https://github.com/google/fonts/tree/main/ofl . Source checksums are
recorded; small generated subsets and OFL licenses are kept in
`third_party/fonts/i18n`. Original Inter/Montserrat/DejaVu Latin styling remains
in use; missing glyphs use the Noto subsets. Complex-script prepared glyphs use
regular Noto styling in each of the four font roles.

`common.tsv`, `phrases.tsv` and `complete_catalogs.py` document the bootstrap
terminology/compact copy. Edit JSON directly for subsequent translation review;
do not run the bootstrap script over reviewed catalogs. Formatting, protocol
identifiers, units and runtime numbers must preserve their argument types and
order. The runtime also validates printf signatures before accepting a value.

## Automated layout checks

`make launcher-layout-check` runs the actual launcher with software OpenGL and
real baked font metrics for all 31 languages. It visits every settings row,
the language dropdown, PC actions, connection errors, and About, and fails on
horizontal text escaping the canvas/current clip without readable scrolling.
It also rejects drawn truncation markers (`...` and the ellipsis glyph).
Screenshots are written to `build/layout-pictures`. Animated offscreen content
is excluded when it is completely outside the vertical viewport. This is not
a proof for arbitrary host/game names, every modal state, or every theme; keep
manual review for mixed RTL text and runtime content.

Long bounded labels retain their complete text and scroll at 24 virtual pixels
per second, with two-second pauses at either end. Limited paragraphs retain
the remaining lines and scroll vertically. `tests/test_text_layout.py` checks
pause timing, speed, full travel and font metrics; catalog checks reject stale
rendered translations after source JSON edits. Prefer concise copy or more room
first; use scrolling for content that cannot be shortened.
