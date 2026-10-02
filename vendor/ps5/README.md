# PS5 import descriptions

`sdk/stubs/` holds linker-only import descriptions for public PS5 system
modules that the bundled SDK stub set lacks (PNG decoder, VideoDec2, mouse,
VideoOut). `tools/build.sh` compiles them during the host build; they are not
packaged and contain no Sony implementation.

The launcher's graphics libraries are not kept here: the build downloads the
pinned ps5-opengl SDK into `.deps/ps5-opengl/` (`tools/fetch-opengl-sdk.sh`).
