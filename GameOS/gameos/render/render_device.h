// SPDX-License-Identifier: GPL-3.0-only
#ifndef GOS_RENDER_DEVICE_H
#define GOS_RENDER_DEVICE_H

// The interface between the GameOS renderer (gameos_graphics.cpp) and a
// graphics API. Everything API specific lives behind it, under render/<api>/:
//   render/gl/gl_device.cpp    OpenGL 4 core profile (-renderer gl)
//   render/gpu/gpu_device.cpp  SDL3 GPU: Vulkan, D3D12, Metal (-renderer gpu)

#include <stdint.h>
#include <string>

#include "render/render_state.h"
#include "render/render_types.h"
#include "utils/vec.h" // vec4, mat4

namespace gos_render {

enum class Primitive { Points, Lines, Triangles };
enum class BufferKind { Vertex, Index, Uniform };
enum class BufferUsage { Static, Dynamic, Stream };
enum class AttribType { Byte, UByte, Short, UShort, Int, UInt, Float };

typedef uint32_t BufferHandle; // 0 = none

struct VertexAttrib {
    int location;      // shader input location (-1 = unused, skipped)
    int components;    // 1..4
    AttribType type;
    bool normalized;
    int stride;
    int offset;
};

// A compiled vertex + fragment shader pair with named uniforms.
class Shader {
public:
    virtual ~Shader() {}
    virtual int attribLocation(const char* name) = 0;
    virtual bool setSampler(const std::string& name, int unit) = 0;
    virtual bool setUniformBlock(const std::string& name, int slot) = 0;
    virtual void setMat4(const std::string& name, const float m[16]) = 0;  // mat4 converts
    virtual void setFloat4(const std::string& name, const float v[4]) = 0; // vec4 converts
    // Makes the shader current and uploads pending uniforms.
    virtual void apply() = 0;
    // Development hot reload of the shader sources.
    virtual bool needsReload() = 0;
    virtual bool reload() = 0;
};

class Device {
public:
    virtual ~Device() {}

    // Called once the window and its context exist. False = unusable.
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual const char* name() const = 0;     // "OpenGL 4.5 (llvmpipe ...)"

    // Frame
    virtual void beginFrame() = 0;
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void clear(bool color, bool depth) = 0;
    // Reads back the frame just drawn (before present), RGBA8, bottom row first.
    virtual bool readPixels(int w, int h, uint8_t* rgba) = 0;
    // Shows the frame (swap buffers) and starts the next one.
    virtual void present() = 0;

    // Shaders: `name` identifies the variant, `defines` is prepended.
    virtual Shader* createShader(const char* name, const char* vs_path, const char* fs_path,
                                 const char* defines) = 0;
    virtual void destroyShader(const char* name) = 0;
    virtual void endShader() = 0; // no shader bound

    // Textures (RGBA8 / RGB8 data, tightly packed)
    virtual Texture createTexture2D(int w, int h, TexFormat fmt, const uint8_t* data) = 0;
    virtual void updateTexture(const Texture& t, const void* data, TexFormat data_fmt) = 0;
    // Replaces a w x h rectangle at (x, y); `data` is tightly packed.
    virtual void updateTextureRegion(const Texture& t, int x, int y, int w, int h, const void* data,
                                     TexFormat data_fmt) = 0;
    virtual bool readTexture(const Texture& t, uint8_t* out, TexFormat out_fmt) = 0;
    virtual void destroyTexture(Texture* t) = 0;
    virtual void bindTexture(int unit, const Texture* t, const SamplerDesc& sampler) = 0;

    // Buffers
    virtual BufferHandle createBuffer(BufferKind kind, BufferUsage usage, const void* data,
                                      size_t size) = 0;
    virtual void updateBuffer(BufferHandle b, BufferKind kind, const void* data, size_t size) = 0;
    virtual void destroyBuffer(BufferHandle b) = 0;
    virtual void bindUniformBuffer(int slot, BufferHandle b) = 0;

    // Pipeline state; implementations skip redundant changes.
    virtual void applyPipeline(const PipelineDesc& d) = 0;

    // Draws. Vertex attributes refer to `vb`; indices are 2 or 4 bytes.
    virtual void draw(Primitive p, BufferHandle vb, const VertexAttrib* attribs, int num_attribs,
                      int first, int count) = 0;
    virtual void drawIndexed(Primitive p, BufferHandle vb, BufferHandle ib, int index_size,
                             const VertexAttrib* attribs, int num_attribs, int count) = 0;

    // Statistics for the current frame (perf CSV, tests).
    struct Stats {
        uint32_t draw_calls = 0;
        uint32_t pipeline_changes = 0;
        uint32_t pipeline_cache_size = 0;
    };
    virtual Stats stats() const = 0;
};

// The active device (created by gos_CreateRenderer).
Device* device();
void setDevice(Device* d);

// Which device to create; chosen before the window exists (-renderer).
enum class Backend { GL, GPU };
Backend backend();
void setBackend(Backend b);
const char* backendName(Backend b); // "gl", "gpu"
bool parseBackend(const char* name, Backend* out);

// Creates the device of a backend; nullptr if that backend is not linked
// in. The SDL3 GPU backend is part of the gameos library; the OpenGL one is
// in gameos_gl and registers itself through registerGLBackend(), which the
// game's main() calls (the data tools never link OpenGL).
typedef Device* (*DeviceFactory)();
void registerDeviceFactory(Backend b, DeviceFactory f);
Device* createDevice(Backend b);
void registerGLBackend(); // render/gl/gl_device.cpp

Device* createGpuDevice(); // render/gpu/gpu_device.cpp

} // namespace gos_render

#endif
