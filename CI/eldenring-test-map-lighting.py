#!/usr/bin/env python3
"""Render production lighting shaders with and without auxiliary-view point lights.

Requires desktop OpenGL 4.3 and EGL (Mesa's surfaceless platform is sufficient).
No game assets or Python OpenGL packages are needed. This is a shader regression,
not an OpenMW runtime or local-map screenshot test.
"""

import ctypes as C
from pathlib import Path
import re
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "files/shaders"


def shader_source(name, clustered):
    source = (SHADERS / name).read_text()
    source = re.sub(r'#include "([^"]+)"',
                    lambda match: shader_source(match[1], clustered), source)
    defines = {
        "useGPUShader4": 0, "lightingMethodClustered": int(clustered),
        "classicFalloff": 0, "simpleLighting": 0, "particle": 0,
        "particlePointLighting": 0, "maxLights": 2,
    }
    return re.sub(r"@(\w+)", lambda match: str(defines[match[1]]), source)


class Renderer:
    def __init__(self):
        self.egl = C.CDLL("libEGL.so.1")
        self.gl = C.CDLL("libGL.so.1")
        self.proc("eglGetPlatformDisplay", C.c_void_p, C.c_uint, C.c_void_p, C.c_void_p)
        self.proc("eglInitialize", C.c_uint, C.c_void_p, C.c_void_p, C.c_void_p)
        self.proc("eglBindAPI", C.c_uint, C.c_uint)
        self.proc("eglChooseConfig", C.c_uint, C.c_void_p, C.c_void_p, C.c_void_p, C.c_int, C.c_void_p)
        self.proc("eglCreateContext", C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)
        self.proc("eglMakeCurrent", C.c_uint, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)
        self.display = self.eglGetPlatformDisplay(0x31DD, None, None)  # EGL_PLATFORM_SURFACELESS_MESA
        if not self.eglInitialize(self.display, None, None):
            raise RuntimeError("EGL initialization failed")
        if not self.eglBindAPI(0x30A2):  # EGL_OPENGL_API
            raise RuntimeError("desktop OpenGL unavailable")
        attrs = (C.c_int * 5)(0x3040, 8, 0x3033, 1, 0x3038)
        config, count = C.c_void_p(), C.c_int()
        if not self.eglChooseConfig(self.display, attrs, C.byref(config), 1, C.byref(count)) or not count.value:
            raise RuntimeError("no OpenGL EGL configuration")
        context_attrs = (C.c_int * 5)(0x3098, 4, 0x30FB, 3, 0x3038)
        self.context = self.eglCreateContext(self.display, config, None, context_attrs)
        if not self.context or not self.eglMakeCurrent(self.display, None, None, self.context):
            raise RuntimeError("OpenGL 4.3 context unavailable")
        signatures = {
            "glGetString": (C.c_char_p, C.c_uint),
            "glCreateShader": (C.c_uint, C.c_uint),
            "glShaderSource": (None, C.c_uint, C.c_int, C.c_void_p, C.c_void_p),
            "glCompileShader": (None, C.c_uint),
            "glGetShaderiv": (None, C.c_uint, C.c_uint, C.c_void_p),
            "glGetShaderInfoLog": (None, C.c_uint, C.c_int, C.c_void_p, C.c_void_p),
            "glCreateProgram": (C.c_uint,),
            "glAttachShader": (None, C.c_uint, C.c_uint),
            "glLinkProgram": (None, C.c_uint),
            "glGetProgramiv": (None, C.c_uint, C.c_uint, C.c_void_p),
            "glGetProgramInfoLog": (None, C.c_uint, C.c_int, C.c_void_p, C.c_void_p),
            "glUseProgram": (None, C.c_uint),
            "glDeleteProgram": (None, C.c_uint),
            "glDeleteShader": (None, C.c_uint),
            "glGetUniformLocation": (C.c_int, C.c_uint, C.c_char_p),
            "glUniform1i": (None, C.c_int, C.c_int),
            "glUniform1f": (None, C.c_int, C.c_float),
            "glUniform2f": (None, C.c_int, C.c_float, C.c_float),
            "glUniform3f": (None, C.c_int, C.c_float, C.c_float, C.c_float),
            "glUniform4f": (None, C.c_int, C.c_float, C.c_float, C.c_float, C.c_float),
            "glUniformMatrix4fv": (None, C.c_int, C.c_int, C.c_ubyte, C.c_void_p),
            "glGenBuffers": (None, C.c_int, C.c_void_p),
            "glBindBuffer": (None, C.c_uint, C.c_uint),
            "glBufferData": (None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint),
            "glBindBufferBase": (None, C.c_uint, C.c_uint, C.c_uint),
            "glDeleteBuffers": (None, C.c_int, C.c_void_p),
            "glGenFramebuffers": (None, C.c_int, C.c_void_p),
            "glBindFramebuffer": (None, C.c_uint, C.c_uint),
            "glGenTextures": (None, C.c_int, C.c_void_p),
            "glBindTexture": (None, C.c_uint, C.c_uint),
            "glTexImage2D": (None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int,
                             C.c_int, C.c_uint, C.c_uint, C.c_void_p),
            "glFramebufferTexture2D": (None, C.c_uint, C.c_uint, C.c_uint, C.c_uint, C.c_int),
            "glCheckFramebufferStatus": (C.c_uint, C.c_uint),
            "glGenVertexArrays": (None, C.c_int, C.c_void_p),
            "glBindVertexArray": (None, C.c_uint),
            "glViewport": (None, C.c_int, C.c_int, C.c_int, C.c_int),
            "glDrawArrays": (None, C.c_uint, C.c_int, C.c_int),
            "glReadPixels": (None, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p),
        }
        for name, signature in signatures.items():
            self.proc(name, *signature)
        print("Shader renderer:", self.glGetString(0x1F01).decode(), flush=True)
        framebuffer, texture, vao = C.c_uint(), C.c_uint(), C.c_uint()
        self.glGenFramebuffers(1, C.byref(framebuffer))
        self.glBindFramebuffer(0x8D40, framebuffer)
        self.glGenTextures(1, C.byref(texture))
        self.glBindTexture(0x0DE1, texture)
        self.glTexImage2D(0x0DE1, 0, 0x8814, 1, 1, 0, 0x1908, 0x1406, None)
        self.glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, texture, 0)
        if self.glCheckFramebufferStatus(0x8D40) != 0x8CD5:
            raise RuntimeError("floating point framebuffer incomplete")
        self.glGenVertexArrays(1, C.byref(vao))
        self.glBindVertexArray(vao)
        self.glViewport(0, 0, 1, 1)

    def proc(self, name, result, *args):
        function = getattr(self.egl if name.startswith("egl") else self.gl, name)
        function.restype, function.argtypes = result, args
        setattr(self, name, function)

    def program(self, vertex, fragment):
        program = self.glCreateProgram()
        for kind, source in ((0x8B31, vertex), (0x8B30, fragment)):
            shader = self.glCreateShader(kind)
            text = C.c_char_p(("#version 430 core\n" + source).encode())
            self.glShaderSource(shader, 1, C.byref(text), None)
            self.glCompileShader(shader)
            status = C.c_int()
            self.glGetShaderiv(shader, 0x8B81, C.byref(status))
            if not status.value:
                log = C.create_string_buffer(8192)
                self.glGetShaderInfoLog(shader, len(log), None, log)
                raise AssertionError(log.value.decode())
            self.glAttachShader(program, shader)
            self.glDeleteShader(shader)
        self.glLinkProgram(program)
        status = C.c_int()
        self.glGetProgramiv(program, 0x8B82, C.byref(status))
        if not status.value:
            log = C.create_string_buffer(8192)
            self.glGetProgramInfoLog(program, len(log), None, log)
            raise AssertionError(log.value.decode())
        self.glUseProgram(program)
        return program

    def render(self, stage, clustered, enabled, overhead=False):
        position = "gl_Position=vec4(vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0,0,1);"
        lighting = shader_source(f"lib/core/lighting_{stage}_impl.glsl", clustered)
        if stage == "fragment":
            vertex = "void main(){" + position + "}"
            fragment = lighting + """
                uniform vec3 testViewPos;
                out vec4 color;
                void main() {
                    vec3 d,a,s;
                    doLighting(vec2(0.5),testViewPos,vec3(0,0,1),1,1,d,a,s);
                    color=vec4(d+a+s+doSpecularLighting(vec2(0.5),testViewPos,vec3(0,0,1)),1);
                }"""
        else:
            vertex = lighting + """
                uniform vec3 testViewPos;
                out vec3 lit;
                void main() {
                    vec3 d,a,s,pd,pa,ps;
                    directionalLighting(normalize(testViewPos),vec3(0,0,1),1,d,a,s);
                    pointLighting(vec2(0.5),normalize(testViewPos),testViewPos,vec3(0,0,1),1,pd,pa,ps);
                    lit=d+a+s+pd+pa+ps;
                """ + position + "}"
            fragment = "in vec3 lit;out vec4 color;void main(){color=vec4(lit,1);}"
        program = self.program(vertex, fragment)

        def uniform(name, *values):
            location = self.glGetUniformLocation(program, name.encode())
            getattr(self, f"glUniform{len(values)}f")(location, *values)

        self.glUniform1i(self.glGetUniformLocation(program, b"pointLightingEnabled"), enabled)
        uniform("sun.position", 0, 0, 1, 0)
        uniform("sun.ambient", 0.2, 0.3, 0.4, 1)
        uniform("sun.diffuse", 0.1, 0.1, 0.1, 1)
        uniform("near", 1)
        uniform("clusterFar", 10)
        uniform("screenRes", 1, 1)
        uniform("gridSize", 1, 1, 1)
        uniform("testViewPos", 0, 0, -500000 if overhead else -2)
        buffers = (C.c_uint * 3)()
        if clustered:
            self.glGenBuffers(3, buffers)
            # A stale main-view grid contains a bright point light. Disabled views
            # must retain only their map sun, even with these unrelated buffers bound.
            data = (struct.pack("20f", 0, 0, -1.5, 1, 0.5, 0.5, 0.5, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1),
                    struct.pack("2I", 0, 1), struct.pack("I", 0))
            for binding, buffer, content in zip((2, 3, 4), buffers, data):
                self.glBindBuffer(0x90D2, buffer)
                self.glBufferData(0x90D2, len(content), C.c_char_p(content), 0x88E4)
                self.glBindBufferBase(0x90D2, binding, buffer if not overhead else 0)
        else:
            self.glUniform1i(self.glGetUniformLocation(program, b"PointLightCount"), 1)
            # Column-major legacy layout from bindings-legacy.glsl.
            light = (C.c_float * 16)(0, 0, 0, 1, 0, 0, 0, 0, 0.5, 0.5, 0.5, 0, 0, 0, 0, 100)
            self.glUniformMatrix4fv(self.glGetUniformLocation(program, b"LightBuffer[0]"), 1, 0, light)
        self.glDrawArrays(4, 0, 3)
        pixel = (C.c_float * 4)()
        self.glReadPixels(0, 0, 1, 1, 0x1908, 0x1406, pixel)
        self.glDeleteProgram(program)
        if clustered:
            self.glDeleteBuffers(3, buffers)
        return tuple(pixel)


class MapLightingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.renderer = Renderer()

    def test_auxiliary_views_keep_sun_without_point_lights(self):
        for stage in ("vertex", "fragment"):
            for clustered in (False, True):
                for overhead in (False, True):
                    with self.subTest(stage=stage, clustered=clustered, overhead=overhead):
                        pixel = self.renderer.render(stage, clustered, False, overhead)
                        for actual, expected in zip(pixel, (0.3, 0.4, 0.5, 1)):
                            self.assertAlmostEqual(actual, expected, places=5)

    def test_main_and_inventory_views_keep_point_lights(self):
        for stage in ("vertex", "fragment"):
            for clustered in (False, True):
                with self.subTest(stage=stage, clustered=clustered):
                    pixel = self.renderer.render(stage, clustered, True)
                    self.assertGreater(pixel[0], 0.6)
                    self.assertAlmostEqual(pixel[3], 1, places=5)


if __name__ == "__main__":
    unittest.main()
