// ps5-homebrew-ui - GLSL program compilation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/gl_program.hpp"

#include "platform/ps5/system.hpp"

namespace hui::gfx
{

namespace
{

const char *g_glsl_prefix = "#version 460 core\n";

} // namespace

void set_glsl_prefix(const char *prefix)
{
    g_glsl_prefix = prefix;
}

namespace
{

GLuint compile(const char *label, GLenum stage, const char *source)
{
    const char *sources[] = {g_glsl_prefix, source};
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 2, sources, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE)
        return shader;
    char info[512] = {};
    glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
    sys::log("[HUI] shader %s (%s) failed: %s", label,
             stage == GL_VERTEX_SHADER ? "vertex" : "fragment", info);
    glDeleteShader(shader);
    return 0;
}

} // namespace

GLuint build_program(const char *label, const char *vertex_source, const char *fragment_source)
{
    GLuint vertex = compile(label, GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = vertex != 0 ? compile(label, GL_FRAGMENT_SHADER, fragment_source) : 0;
    if (fragment == 0)
    {
        if (vertex != 0)
            glDeleteShader(vertex);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok == GL_TRUE)
        return program;
    char info[512] = {};
    glGetProgramInfoLog(program, sizeof(info), nullptr, info);
    sys::log("[HUI] program %s link failed: %s", label, info);
    glDeleteProgram(program);
    return 0;
}

} // namespace hui::gfx
