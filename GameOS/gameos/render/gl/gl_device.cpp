// SPDX-License-Identifier: GPL-3.0-only
// OpenGL implementation of gos_render::Device (render/render_device.h).
// This file and the rest of render/gl/ are the only places that call OpenGL.

#include "render/render_device.h"

#include "render/gl/gl_utils.h"
#include "render/gl/shader_builder.h"

#include "gameos.hpp"

#include <SDL3/SDL_video.h>
extern SDL_Window* g_sdl_window; // gos_render.cpp

#include <map>
#include <stdio.h>
#include <string>
#include <unordered_map>

namespace gos_render {

namespace {

GLenum glPrimitive(Primitive p)
{
    switch (p) {
    case Primitive::Points: return GL_POINTS;
    case Primitive::Lines: return GL_LINES;
    case Primitive::Triangles: return GL_TRIANGLES;
    }
    return GL_TRIANGLES;
}

GLenum glAttribType(AttribType t)
{
    switch (t) {
    case AttribType::Byte: return GL_BYTE;
    case AttribType::UByte: return GL_UNSIGNED_BYTE;
    case AttribType::Short: return GL_SHORT;
    case AttribType::UShort: return GL_UNSIGNED_SHORT;
    case AttribType::Int: return GL_INT;
    case AttribType::UInt: return GL_UNSIGNED_INT;
    case AttribType::Float: return GL_FLOAT;
    }
    return GL_FLOAT;
}

GLenum glBufferTarget(BufferKind k)
{
    switch (k) {
    case BufferKind::Vertex: return GL_ARRAY_BUFFER;
    case BufferKind::Index: return GL_ELEMENT_ARRAY_BUFFER;
    case BufferKind::Uniform: return GL_UNIFORM_BUFFER;
    }
    return GL_ARRAY_BUFFER;
}

GLenum glBufferUsage(BufferUsage u)
{
    switch (u) {
    case BufferUsage::Static: return GL_STATIC_DRAW;
    case BufferUsage::Dynamic: return GL_DYNAMIC_DRAW;
    case BufferUsage::Stream: return GL_STREAM_DRAW;
    }
    return GL_DYNAMIC_DRAW;
}

#ifdef PLATFORM_WINDOWS
void GLAPIENTRY debugLog(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei,
                         const GLchar* message, const void*)
#else
void GLAPIENTRY debugLog(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei,
                         const GLchar* message, GLvoid*)
#endif
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION || severity == GL_DEBUG_SEVERITY_LOW)
        return;
    printf("OpenGL debug: type 0x%x source 0x%x id %u severity 0x%x: %s\n", type, source, id,
           severity, message);
}

class GLShader : public Shader {
public:
    explicit GLShader(glsl_program* p) : program_(p) {}

    int attribLocation(const char* name) override { return program_->getAttribLocation(name); }

    bool setSampler(const std::string& name, int unit) override {
        auto it = program_->samplers_.find(name);
        if (it == program_->samplers_.end())
            return false;
        glUniform1i(it->second->index_, unit); // program must be current
        return true;
    }

    bool setUniformBlock(const std::string& name, int slot) override {
        auto it = program_->uniform_blocks_.find(name);
        if (it == program_->uniform_blocks_.end())
            return false;
        glUniformBlockBinding(program_->shp_, it->second->index_, slot);
        return true;
    }

    void setMat4(const std::string& name, const float m[16]) override { program_->setMat4(name, m); }
    void setFloat4(const std::string& name, const float v[4]) override { program_->setFloat4(name, v); }
    void apply() override { program_->apply(); }
    bool needsReload() override { return program_->needsReload(); }
    bool reload() override { return program_->reload(); }

private:
    glsl_program* program_;
};

static GLenum glTextureFormat(TexFormat f) {
    switch (f) {
    case TF_R8: case TF_R32F: return GL_RED;
    case TF_RG8: case TF_RG32F: return GL_RG;
    case TF_RGB8: case TF_RGB32F: return GL_RGB;
    default: return GL_RGBA;
    }
}
static GLenum glTextureChannelType(TexFormat f) { return f >= TF_R32F ? GL_FLOAT : GL_UNSIGNED_BYTE; }

class GLDevice : public Device {
public:
    bool init() override;
    void shutdown() override;
    const char* name() const override { return name_.c_str(); }

    void beginFrame() override {
        glBindVertexArray(vao_);
        stats_.draw_calls = 0;
        stats_.pipeline_changes = 0;
        have_state_ = false;
    }
    void setViewport(int x, int y, int w, int h) override { glViewport(x, y, w, h); }
    void clear(bool color, bool depth) override {
        // glClear honours the depth write mask
        if (depth)
            glDepthMask(GL_TRUE);
        glClear((color ? GL_COLOR_BUFFER_BIT : 0) | (depth ? GL_DEPTH_BUFFER_BIT : 0));
        have_state_ = false;
    }
    void present() override { SDL_GL_SwapWindow(g_sdl_window); }
    bool readPixels(int w, int h, uint8_t* rgba) override {
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return glGetError() == GL_NO_ERROR;
    }

    Shader* createShader(const char* name, const char* vs, const char* fs, const char* defines) override {
        glsl_program* p = glsl_program::makeProgram(name, vs, fs, defines);
        if (!p)
            return nullptr;
        GLShader* s = new GLShader(p);
        shaders_[name] = s;
        return s;
    }
    void destroyShader(const char* name) override {
        auto it = shaders_.find(name);
        if (it != shaders_.end()) {
            delete it->second;
            shaders_.erase(it);
        }
        glsl_program::deleteProgram(name);
    }
    void endShader() override { glUseProgram(0); }

    Texture createTexture2D(int w, int h, TexFormat fmt, const uint8_t* data) override {
        return create2DTexture(w, h, fmt, data);
    }
    void updateTexture(const Texture& t, const void* data, TexFormat fmt) override {
        ::updateTexture(t, const_cast<void*>(data), fmt);
    }
    void updateTextureRegion(const Texture& t, int x, int y, int w, int h, const void* data,
                             TexFormat fmt) override {
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, glTextureFormat(fmt), glTextureChannelType(fmt), data);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    bool readTexture(const Texture& t, uint8_t* out, TexFormat fmt) override {
        getTextureData(t, 0, out, fmt);
        return true;
    }
    void destroyTexture(Texture* t) override {
        sampler_of_texture_.erase(t->id);
        ::destroyTexture(t);
    }
    void bindTexture(int unit, const Texture* t, const SamplerDesc& s) override;

    BufferHandle createBuffer(BufferKind kind, BufferUsage usage, const void* data, size_t size) override {
        return makeBuffer(glBufferTarget(kind), data, (GLsizei)size, glBufferUsage(usage));
    }
    void updateBuffer(BufferHandle b, BufferKind kind, const void* data, size_t size) override {
        const GLenum target = glBufferTarget(kind);
        glBindBuffer(target, b);
        glBufferData(target, size, data, GL_DYNAMIC_DRAW); // orphan + refill
        glBindBuffer(target, 0);
    }
    void destroyBuffer(BufferHandle b) override {
        if (b)
            glDeleteBuffers(1, &b);
    }
    void bindUniformBuffer(int slot, BufferHandle b) override { glBindBufferBase(GL_UNIFORM_BUFFER, slot, b); }

    void applyPipeline(const PipelineDesc& d) override;

    void draw(Primitive p, BufferHandle vb, const VertexAttrib* a, int n, int first, int count) override {
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        enableAttribs(a, n);
        glDrawArrays(glPrimitive(p), first, count);
        disableAttribs(a, n);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        ++stats_.draw_calls;
    }
    void drawIndexed(Primitive p, BufferHandle vb, BufferHandle ib, int index_size,
                     const VertexAttrib* a, int n, int count) override {
        glBindBuffer(GL_ARRAY_BUFFER, vb);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib);
        enableAttribs(a, n);
        glDrawElements(glPrimitive(p), count, index_size == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, nullptr);
        disableAttribs(a, n);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        ++stats_.draw_calls;
    }

    Stats stats() const override {
        Stats s = stats_;
        s.pipeline_cache_size = (uint32_t)pipelines_seen_.size();
        return s;
    }

private:
    static void enableAttribs(const VertexAttrib* a, int n) {
        for (int i = 0; i < n; ++i) {
            if (a[i].location < 0)
                continue;
            glEnableVertexAttribArray(a[i].location);
            glVertexAttribPointer(a[i].location, a[i].components, glAttribType(a[i].type),
                                  a[i].normalized ? GL_TRUE : GL_FALSE, a[i].stride,
                                  BUFFER_OFFSET(a[i].offset));
        }
    }
    static void disableAttribs(const VertexAttrib* a, int n) {
        for (int i = 0; i < n; ++i)
            if (a[i].location >= 0)
                glDisableVertexAttribArray(a[i].location);
    }

    std::string name_ = "OpenGL";
    GLuint vao_ = 0;
    std::map<std::string, GLShader*> shaders_;
    std::unordered_map<uint32_t, SamplerKey> sampler_of_texture_; // GL keeps sampler state per texture
    PipelineCache<PipelineDesc> pipelines_seen_;
    PipelineDesc state_;
    bool have_state_ = false;
    Stats stats_;
};

bool GLDevice::init()
{
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        SPEW(("GLEW", "Error: %s\n", glewGetErrorString(err)));
        return false;
    }
    // glewInit() probes extensions with glGetString(GL_EXTENSIONS), which
    // raises GL_INVALID_ENUM on core profile contexts. Drain that error so it
    // is not reported later by an unrelated CHECK_GL_ERROR.
    while (glGetError() != GL_NO_ERROR) {}

    if (!glewIsSupported("GL_VERSION_3_0")) {
        SPEW(("GRAPHICS", "Minimum required OpenGL version is 3.0\n"));
        return false;
    }

    const char* glsl_version = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    int glsl_maj = 0, glsl_min = 0;
    sscanf(glsl_version ? glsl_version : "0.0", "%d.%d", &glsl_maj, &glsl_min);
    if (glsl_maj < 3 || (glsl_maj == 3 && glsl_min < 30)) {
        SPEW(("GRAPHICS", "Minimum required GLSL version is 3.30, current: %d.%d\n", glsl_maj, glsl_min));
        return false;
    }

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
    glDebugMessageCallbackARB((GLDEBUGPROC)&debugLog, NULL);

    name_ = std::string("OpenGL ") + (const char*)glGetString(GL_VERSION) + " (" +
            (const char*)glGetString(GL_RENDERER) + ")";
    GLint num_exts = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_exts);
    printf("Renderer   : %s\nGLSL       : %s\nExtensions : %d\n", name_.c_str(), glsl_version, num_exts);

    glGenVertexArrays(1, &vao_);
    return true;
}

void GLDevice::shutdown()
{
    for (auto& kv : shaders_)
        delete kv.second;
    shaders_.clear();
    if (vao_)
        glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
}

void GLDevice::applyPipeline(const PipelineDesc& d)
{
    pipelines_seen_.get(packPipelineKey(d), [&](PipelineKey) { return d; });
    if (have_state_ && d == state_)
        return;
    const bool all = !have_state_;
    ++stats_.pipeline_changes;

    if (all || d.cull != state_.cull) {
        if (d.cull == CullMode::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(d.cull == CullMode::Back ? GL_BACK : GL_FRONT);
        }
    }
    if (all || d.depth_write != state_.depth_write)
        glDepthMask(d.depth_write ? GL_TRUE : GL_FALSE);
    if (all || d.depth_test != state_.depth_test) {
        if (d.depth_test == DepthTest::Off) {
            // the original renderer also set GL_ALWAYS here
            glDisable(GL_DEPTH_TEST);
            glDepthFunc(GL_ALWAYS);
        } else {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(d.depth_test == DepthTest::LessEqual ? GL_LEQUAL : GL_LESS);
        }
    }
    if (all || d.blend != state_.blend) {
        switch (d.blend) {
        case BlendMode::Opaque:        glDisable(GL_BLEND); glBlendFunc(GL_ONE, GL_ZERO); break;
        case BlendMode::Additive:      glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); break;
        case BlendMode::AlphaBlend:    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
        case BlendMode::Premultiplied: glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
        case BlendMode::AlphaAdditive: glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
        default: break;
        }
    }
    // alpha_test selects a shader variant; no GL state (core profile)
    state_ = d;
    have_state_ = true;
}

void GLDevice::bindTexture(int unit, const Texture* t, const SamplerDesc& s)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    if (!t) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }
    glBindTexture(GL_TEXTURE_2D, t->id);
    // Sampler parameters live in the texture object: only touch them when
    // this texture was last used with different ones.
    const SamplerKey key = packSamplerKey(s);
    auto it = sampler_of_texture_.find(t->id);
    if (it != sampler_of_texture_.end() && it->second == key)
        return;
    sampler_of_texture_[t->id] = key;
    setSamplerParams(t->type_, s.address == AddressMode::Wrap ? TAM_REPEAT : TAM_CLAMP_TO_EDGE,
                     s.filter == FilterMode::Linear ? TFM_LINEAR : TFM_NEAREST);
}

} // namespace

static Device* createGLDevice() { return new GLDevice(); }
void registerGLBackend() { registerDeviceFactory(Backend::GL, &createGLDevice); }

} // namespace gos_render
