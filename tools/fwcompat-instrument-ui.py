#!/usr/bin/env python3
"""Generate diagnostic-only UI translation units without changing the pinned submodule."""
from pathlib import Path
import re
import sys

FILES = {
 'gfx/canvas.cpp': ('glGenTextures','glTexImage2D','glGenFramebuffers','glFramebufferTexture2D','glCheckFramebufferStatus','glRenderbufferStorageMultisample','glClear','glBlitFramebuffer'),
 'gfx/renderer.cpp': ('glGenVertexArrays',),
 'gfx/gl_program.cpp': ('glCompileShader','glLinkProgram'),
 'platform/ps5/display_egl.cpp': ('eglGetDisplay','eglSetDisplayModePS5','eglInitialize','eglBindAPI','eglChooseConfig','eglCreateWindowSurface','eglCreateContext','eglMakeCurrent','eglQuerySurface','eglSwapInterval'),
}

def instrument(source, names, prefix):
    pattern = re.compile(r'\b(' + '|'.join(names) + r')\s*\(')
    # Operate backwards: offsets belong to the original source. Calls are not nested here.
    matches = list(pattern.finditer(source))
    for match in reversed(matches):
        pos=match.end(); depth=1
        while depth and pos < len(source):
            if source[pos]=='(': depth+=1
            elif source[pos]==')': depth-=1
            pos+=1
        if depth: raise ValueError('Unterminated call')
        expression=source[match.start():pos]
        source=source[:match.start()] + f'FWCOMPAT_CALL("{prefix}.{match.group(1)}", {expression})' + source[pos:]
    return '#include "fwcompat.h"\n' + source

def generate(root):
    target=root/'build/fwcompat-ui'
    for name, calls in FILES.items():
        text=(root/'third_party/ps5-homebrew-ui'/name).read_text()
        if name=='gfx/renderer.cpp':
            old='return glass_a_.create(kGlassWidth, kGlassHeight, 1) &&\n           glass_b_.create(kGlassWidth, kGlassHeight, 1);'
            assert old in text
            text=text.replace(old, 'return FWCOMPAT_CALL("renderer.canvas_a", glass_a_.create(kGlassWidth, kGlassHeight, 1)) &&\n           FWCOMPAT_CALL("renderer.canvas_b", glass_b_.create(kGlassWidth, kGlassHeight, 1));')
        if name=='gfx/canvas.cpp':
            # Only startup allocation is instrumented; normal frame-loop clears/blits stay untouched.
            end=text.index('\nvoid Canvas::bind')
            text=instrument(text[:end], calls, 'canvas')+text[end:]
        else: text=instrument(text, calls, name.replace('/','.')[:-4])
        path=target/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)

if __name__=='__main__': generate(Path(sys.argv[1]).resolve())
