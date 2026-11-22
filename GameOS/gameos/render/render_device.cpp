// SPDX-License-Identifier: GPL-3.0-only
#include "render/render_device.h"

#include <string.h>

namespace gos_render {

namespace {
Device* g_device = nullptr;
Backend g_backend = Backend::GPU;
DeviceFactory g_factories[2] = {nullptr, &createGpuDevice}; // GL, GPU
} // namespace

void registerDeviceFactory(Backend b, DeviceFactory f) { g_factories[(int)b] = f; }

Device* createDevice(Backend b)
{
    DeviceFactory f = g_factories[(int)b];
    return f ? f() : nullptr;
}

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
