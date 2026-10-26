#pragma once
// Single place that pulls in the OpenGL loader (glad, GL 3.3 core).
#include <glad/gl.h>

namespace nd {
// False once the OpenGL context is destroyed. GPU objects that outlive the context
// (static caches) check it so they don't call into a dead driver at exit.
extern bool g_GLContextAlive;
}
