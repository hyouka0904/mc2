#include "render/render_device.h"

#include <string.h>

namespace gos_render {

namespace {
Device* g_device = nullptr;
Backend g_backend = Backend::GL;
} // namespace

Device* device() { return g_device; }
void setDevice(Device* d) { g_device = d; }

Backend backend() { return g_backend; }
void setBackend(Backend b) { g_backend = b; }

const char* backendName(Backend b) { return b == Backend::GPU ? "gpu" : "gl"; }

bool parseBackend(const char* name, Backend* out)
{
    if (!name)
        return false;
    if (!strcmp(name, "gl")) {
        *out = Backend::GL;
        return true;
    }
    if (!strcmp(name, "gpu")) {
        *out = Backend::GPU;
        return true;
    }
    return false;
}

} // namespace gos_render
