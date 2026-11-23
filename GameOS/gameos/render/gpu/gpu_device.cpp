// SPDX-License-Identifier: GPL-3.0-only
// SDL3 GPU implementation of gos_render::Device (Vulkan, D3D12 or Metal).
//
// GameOS is an immediate mode API: the game changes state, updates a vertex
// buffer and draws, hundreds of times per frame, reusing the same buffers.
// SDL3 GPU wants uploads in copy passes outside render passes and all state
// baked into pipeline objects. This device therefore records the frame:
//
//   * every draw snapshots what it uses: the pipeline (created on first use
//     per shader variant + PipelineDesc + vertex layout + primitive), the
//     textures and samplers, the shader parameters (PerDraw, and for the
//     lit shaders the selected LightsData entry and SceneData), and the
//     vertex / index data of buffers changed during the frame, which is
//     appended to a per-frame upload ring;
//   * texture updates are recorded in order between the draws;
//   * flush() uploads the ring in one copy pass and replays the commands
//     into render passes on an offscreen colour + depth target, breaking the
//     pass only where a texture update or a clear needs it;
//   * present() blits the target to the swapchain; readPixels() downloads it.
//
// Static buffers that stay unchanged are copied to their own GPU buffer at
// the end of the frame and drawn from there afterwards.

#include "render/render_device.h"
#include "render/shader_params.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <map>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

extern SDL_Window* g_sdl_window; // gos_render.cpp

namespace gos_render {
namespace {

// std140 sizes of the blocks in shaders/include/lighting.hglsl, scene.hglsl
const uint32_t kObjectLightsSize = 1280; // ObjectLights in shaders/include/lighting.hglsl
const uint32_t kSceneDataSize = 64;
const uint32_t kMaxTextureUnits = 4;
// GL's window origin is bottom left, SDL GPU's top left. Edges and lines
// that fall exactly on pixel centres / boundaries are rounded the other way
// once y is flipped; shifting the viewport by a hundredth of a pixel makes
// the rasteriser pick the same pixels as GL (the screenshot tests use it).
const float kRasterBias = 0.01f;

uint32_t alignUp(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

// ------------------------------------------------------------------ shaders

struct StageInfo {
    uint32_t samplers = 0;
    uint32_t uniform_buffers = 0;
};

struct ParamInfo {
    const char* name;
    uint32_t offset, size;
};

const ParamInfo kParams[] = {
    {"mvp", offsetof(PerDraw, mvp), 64},
    {"projection_", offsetof(PerDraw, projection_), 64},
    {"world_", offsetof(PerDraw, world_), 64},
    {"view_", offsetof(PerDraw, view_), 64},
    {"wvp_", offsetof(PerDraw, wvp_), 64},
    {"vp", offsetof(PerDraw, vp), 16},
    {"light_offset_", offsetof(PerDraw, light_offset_), 16},
    {"fog_color", offsetof(PerDraw, fog_color), 16},
    {"Foreground", offsetof(PerDraw, Foreground), 16},
};

const ParamInfo* findParam(const std::string& name)
{
    for (const ParamInfo& p : kParams)
        if (name == p.name)
            return &p;
    return nullptr;
}

class GpuDevice;

class GpuShader : public Shader {
public:
    GpuShader(GpuDevice* dev, SDL_GPUShader* vs, SDL_GPUShader* fs, StageInfo vsi, StageInfo fsi)
        : dev_(dev), vs_(vs), fs_(fs), vs_info_(vsi), fs_info_(fsi)
    {
        memset(&params_, 0, sizeof(params_));
    }

    // Input locations are explicit in the shaders (layout(location = N))
    int attribLocation(const char* name) override
    {
        static const struct { const char* name; int loc; } kAttribs[] = {
            {"pos", 0}, {"color", 1}, {"normal", 1}, {"fog", 2}, {"aRGBLight", 2}, {"texcoord", 3},
        };
        for (const auto& a : kAttribs)
            if (!strcmp(a.name, name))
                return a.loc;
        return -1;
    }
    // Bindings are fixed in the shaders (GOS_SAMPLER / GOS_UBO)
    bool setSampler(const std::string&, int) override { return true; }
    bool setUniformBlock(const std::string&, int) override { return true; }
    void setMat4(const std::string& name, const float m[16]) override { set(name, m, 64); }
    void setFloat4(const std::string& name, const float v[4]) override { set(name, v, 16); }
    void apply() override;
    bool needsReload() override { return false; }
    bool reload() override { return false; }

    SDL_GPUShader* vs_;
    SDL_GPUShader* fs_;
    StageInfo vs_info_, fs_info_;
    PerDraw params_; // values set since creation; snapshotted by apply()

private:
    void set(const std::string& name, const float* v, uint32_t size)
    {
        const ParamInfo* p = findParam(name);
        if (p && p->size == size)
            memcpy((char*)&params_ + p->offset, v, size);
    }
    GpuDevice* dev_;
};

// ---------------------------------------------------------------- resources

struct GpuTexture {
    SDL_GPUTexture* tex = nullptr;
    int w = 0, h = 0;
    TexFormat fmt = TF_NONE;     // format the game asked for
    TexFormat storage = TF_NONE; // format stored on the GPU (RGB8 -> RGBA8)
};

struct GpuBuffer {
    BufferKind kind = BufferKind::Vertex;
    BufferUsage usage = BufferUsage::Dynamic;
    std::vector<uint8_t> data; // current contents
    uint32_t version = 1;
    // Own GPU copy (static buffers once unchanged for a frame)
    SDL_GPUBuffer* gpu = nullptr;
    uint32_t gpu_size = 0;
    uint32_t gpu_version = 0;
    // Snapshot in the upload ring of the current flush
    uint32_t ring_flush = 0;
    uint32_t ring_version = 0;
    uint32_t ring_offset = 0;
    bool queued_for_promotion = false; // in GpuDevice::promote_queue_
};

enum class CmdType : uint8_t { Clear, Viewport, TexUpload, Draw };

struct Cmd {
    CmdType type;
    bool clear_color = false, clear_depth = false;
    SDL_GPUViewport viewport;
    // texture upload
    SDL_GPUTexture* tex = nullptr;
    uint32_t staging_offset = 0;
    int x = 0, y = 0, w = 0, h = 0;
    // draw
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* vb = nullptr; // nullptr = ring
    uint32_t vb_offset = 0;
    SDL_GPUBuffer* ib = nullptr; // nullptr + indexed = ring
    uint32_t ib_offset = 0;
    bool indexed = false, index32 = false;
    uint32_t first = 0, count = 0;
    uint32_t num_samplers = 0;
    SDL_GPUTextureSamplerBinding samplers[kMaxTextureUnits];
    uint32_t vs_ubos = 0, fs_ubos = 0;
    uint32_t uniforms = 0; // offset in the uniform arena: PerDraw, lights, scene
};

// ------------------------------------------------------------------ helpers

SDL_GPUTextureFormat gpuFormat(TexFormat f)
{
    switch (f) {
    case TF_R8: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case TF_RG8: return SDL_GPU_TEXTUREFORMAT_R8G8_UNORM;
    case TF_RGB8:
    case TF_RGBA8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case TF_R32F: return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    case TF_RG32F: return SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
    case TF_RGB32F:
    case TF_RGBA32F: return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    default: return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

TexFormat storageFormat(TexFormat f)
{
    if (f == TF_RGB8)
        return TF_RGBA8;
    if (f == TF_RGB32F)
        return TF_RGBA32F;
    return f;
}

int channels(TexFormat f)
{
    switch (f) {
    case TF_R8: case TF_R32F: return 1;
    case TF_RG8: case TF_RG32F: return 2;
    case TF_RGB8: case TF_RGB32F: return 3;
    case TF_RGBA8: case TF_RGBA32F: return 4;
    default: return 0;
    }
}

bool isFloat(TexFormat f) { return f >= TF_R32F && f <= TF_RGBA32F; }

// Converts between channel counts of the same component type; missing
// colour channels become 0, missing alpha 1 (what GL does when sampling).
void convertPixels(const void* src, TexFormat sf, void* dst, TexFormat df, size_t n)
{
    const int sc = channels(sf), dc = channels(df);
    if (isFloat(sf) != isFloat(df) || !sc || !dc)
        return;
    if (sc == dc) {
        memcpy(dst, src, n * sc * (isFloat(sf) ? 4 : 1));
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        for (int c = 0; c < dc; ++c) {
            if (isFloat(sf)) {
                const float* s = (const float*)src + i * sc;
                ((float*)dst)[i * dc + c] = c < sc ? s[c] : (c == 3 ? 1.0f : 0.0f);
            } else {
                const uint8_t* s = (const uint8_t*)src + i * sc;
                ((uint8_t*)dst)[i * dc + c] = c < sc ? s[c] : (c == 3 ? 255 : 0);
            }
        }
    }
}

SDL_GPUVertexElementFormat vertexFormat(const VertexAttrib& a)
{
    const int n = a.components;
    switch (a.type) {
    case AttribType::Float:
        return n == 1 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT : n == 2 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2
             : n == 3 ? SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3 : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case AttribType::UByte:
        if (n == 4)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4;
        if (n == 2)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2;
        break;
    case AttribType::Byte:
        if (n == 4)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM : SDL_GPU_VERTEXELEMENTFORMAT_BYTE4;
        if (n == 2)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_BYTE2;
        break;
    case AttribType::Short:
        if (n == 4)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM : SDL_GPU_VERTEXELEMENTFORMAT_SHORT4;
        if (n == 2)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_SHORT2;
        break;
    case AttribType::UShort:
        if (n == 4)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM : SDL_GPU_VERTEXELEMENTFORMAT_USHORT4;
        if (n == 2)
            return a.normalized ? SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_USHORT2;
        break;
    case AttribType::Int:
        return n == 1 ? SDL_GPU_VERTEXELEMENTFORMAT_INT : n == 2 ? SDL_GPU_VERTEXELEMENTFORMAT_INT2
             : n == 3 ? SDL_GPU_VERTEXELEMENTFORMAT_INT3 : SDL_GPU_VERTEXELEMENTFORMAT_INT4;
    case AttribType::UInt:
        return n == 1 ? SDL_GPU_VERTEXELEMENTFORMAT_UINT : n == 2 ? SDL_GPU_VERTEXELEMENTFORMAT_UINT2
             : n == 3 ? SDL_GPU_VERTEXELEMENTFORMAT_UINT3 : SDL_GPU_VERTEXELEMENTFORMAT_UINT4;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

SDL_GPUColorTargetBlendState blendState(BlendMode m)
{
    SDL_GPUColorTargetBlendState b;
    SDL_zero(b);
    b.color_blend_op = b.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    b.enable_blend = m != BlendMode::Opaque;
    SDL_GPUBlendFactor src = SDL_GPU_BLENDFACTOR_ONE, dst = SDL_GPU_BLENDFACTOR_ZERO;
    switch (m) {
    case BlendMode::Additive: dst = SDL_GPU_BLENDFACTOR_ONE; break;
    case BlendMode::AlphaBlend:
        src = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        dst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::Premultiplied: dst = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA; break;
    case BlendMode::AlphaAdditive:
        src = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        dst = SDL_GPU_BLENDFACTOR_ONE;
        break;
    default: break;
    }
    // glBlendFunc applies the same factors to colour and alpha
    b.src_color_blendfactor = b.src_alpha_blendfactor = src;
    b.dst_color_blendfactor = b.dst_alpha_blendfactor = dst;
    return b;
}

std::vector<uint8_t> readFile(const std::string& path)
{
    std::vector<uint8_t> out;
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (data) {
        out.assign((uint8_t*)data, (uint8_t*)data + size);
        SDL_free(data);
    }
    return out;
}

// --------------------------------------------------------------------- device

class GpuDevice : public Device {
public:
    bool init() override;
    // Any shader every build ships; its files tell which formats are there
    static constexpr const char* kProbeShader = "gos_vertex.base";
    static std::string shaderPath(const std::string& dir, const std::string& base, bool vertex,
                                  SDL_GPUShaderFormat format);
    bool shaderFileExists(const std::string& dir, SDL_GPUShaderFormat format) const;
    void shutdown() override;
    const char* name() const override { return name_.c_str(); }

    void beginFrame() override
    {
        stats_.draw_calls = 0;
        stats_.pipeline_changes = 0;
        have_desc_ = false;
    }
    void setViewport(int x, int y, int w, int h) override
    {
        ensureTargets();
        Cmd c;
        c.type = CmdType::Viewport;
        // GL's origin is the bottom left corner, SDL GPU's the top left
        c.viewport = {(float)x, (float)(target_h_ - y - h) + kRasterBias, (float)w, (float)h, 0.0f, 1.0f};
        cmds_.push_back(c);
    }
    void clear(bool color, bool depth) override
    {
        ensureTargets();
        Cmd c;
        c.type = CmdType::Clear;
        c.clear_color = color;
        c.clear_depth = depth;
        cmds_.push_back(c);
    }
    bool readPixels(int w, int h, uint8_t* rgba) override;
    void present() override;

    Shader* createShader(const char* name, const char* vs, const char* fs, const char* defines) override;
    void destroyShader(const char* name) override;
    void endShader() override { current_ = nullptr; }

    Texture createTexture2D(int w, int h, TexFormat fmt, const uint8_t* data) override;
    void updateTexture(const Texture& t, const void* data, TexFormat fmt) override;
    void updateTextureRegion(const Texture& t, int x, int y, int w, int h, const void* data, TexFormat fmt) override;
    bool readTexture(const Texture& t, uint8_t* out, TexFormat fmt) override;
    void destroyTexture(Texture* t) override;
    void bindTexture(int unit, const Texture* t, const SamplerDesc& s) override;

    BufferHandle createBuffer(BufferKind kind, BufferUsage usage, const void* data, size_t size) override;
    void updateBuffer(BufferHandle b, BufferKind kind, const void* data, size_t size) override;
    void destroyBuffer(BufferHandle b) override;
    void bindUniformBuffer(int slot, BufferHandle b) override
    {
        if (slot >= 0 && slot < 4)
            uniform_slots_[slot] = b;
    }

    void applyPipeline(const PipelineDesc& d) override
    {
        if (have_desc_ && d == desc_)
            return;
        ++stats_.pipeline_changes;
        desc_ = d;
        have_desc_ = true;
    }

    void draw(Primitive p, BufferHandle vb, const VertexAttrib* a, int n, int first, int count) override
    {
        recordDraw(p, vb, 0, 0, a, n, first, count);
    }
    void drawIndexed(Primitive p, BufferHandle vb, BufferHandle ib, int index_size, const VertexAttrib* a,
                     int n, int count) override
    {
        recordDraw(p, vb, ib, index_size, a, n, 0, count);
    }

    Stats stats() const override
    {
        Stats s = stats_;
        s.pipeline_cache_size = (uint32_t)pipelines_.size();
        return s;
    }

    // GpuShader::apply
    void setCurrent(GpuShader* s)
    {
        current_ = s;
        current_params_ = s->params_;
    }

private:
    bool loadManifest(const std::string& dir);
    SDL_GPUShader* loadStage(const std::string& dir, const std::string& base, bool vertex, StageInfo* info);
    void ensureTargets();
    SDL_GPUSampler* sampler(const SamplerDesc& s);
    SDL_GPUGraphicsPipeline* pipeline(Primitive p, const VertexAttrib* a, int n);
    // Location of a buffer's current contents for a draw recorded now
    bool locate(GpuBuffer* b, SDL_GPUBuffer** buf, uint32_t* offset);
    void recordDraw(Primitive p, BufferHandle vb, BufferHandle ib, int index_size, const VertexAttrib* a,
                    int n, int first, int count);
    uint32_t appendStaging(std::vector<uint8_t>& v, const void* data, size_t size, uint32_t align);
    // Submits the recorded commands; optionally downloads the target into
    // `download` (w x h RGBA8, top row first) and/or presents it.
    bool flush(bool present, int dl_w, int dl_h, std::vector<uint8_t>* download);
    void submitUpload(SDL_GPUTexture* tex, int w, int h, const void* pixels, size_t size);

    SDL_GPUDevice* dev_ = nullptr;
    std::string name_ = "SDL3 GPU";
    SDL_GPUShaderFormat format_ = SDL_GPU_SHADERFORMAT_INVALID;
    std::map<std::string, StageInfo> manifest_; // "<program>.<variant> <stage>"
    std::map<std::string, GpuShader*> shaders_;
    GpuShader* current_ = nullptr;
    PerDraw current_params_;

    SDL_GPUTexture* color_ = nullptr;
    SDL_GPUTexture* depth_ = nullptr;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    int target_w_ = 0, target_h_ = 0;
    bool target_valid_ = false; // the target holds this frame's earlier passes

    uint32_t next_texture_id_ = 1;
    std::unordered_map<uint32_t, GpuTexture> textures_;
    GpuTexture* bound_[kMaxTextureUnits] = {};
    SamplerDesc bound_sampler_[kMaxTextureUnits];
    SDL_GPUTexture* black_ = nullptr; // sampled where no texture is bound
    std::unordered_map<SamplerKey, SDL_GPUSampler*> samplers_;

    uint32_t next_buffer_id_ = 1;
    std::unordered_map<uint32_t, std::unique_ptr<GpuBuffer> > buffers_;
    // Static buffers created or changed since the last flush: the candidates
    // for their own GPU copy (walking all of buffers_ every frame was ~10% of
    // the main thread with ~1700 buffers)
    std::vector<uint32_t> promote_queue_;
    void queuePromotion(uint32_t id, GpuBuffer* b) {
        if (b->usage == BufferUsage::Static && b->kind != BufferKind::Uniform && !b->queued_for_promotion) {
            b->queued_for_promotion = true;
            promote_queue_.push_back(id);
        }
    }
    BufferHandle uniform_slots_[4] = {};

    PipelineDesc desc_;
    bool have_desc_ = false;
    std::unordered_map<std::string, SDL_GPUGraphicsPipeline*> pipelines_;

    // The frame being recorded
    std::vector<Cmd> cmds_;
    std::vector<uint8_t> ring_;        // vertex / index snapshots
    std::vector<uint8_t> tex_staging_; // texture update pixels
    std::vector<uint8_t> uniforms_;    // per draw uniform data
    uint32_t flush_id_ = 1;            // invalidates ring snapshots
    std::vector<SDL_GPUTexture*> release_textures_; // after the next submit
    std::vector<SDL_GPUBuffer*> release_buffers_;

    SDL_GPUBuffer* ring_gpu_ = nullptr;
    uint32_t ring_gpu_size_ = 0;
    SDL_GPUTransferBuffer* upload_ = nullptr;
    uint32_t upload_size_ = 0;

    Stats stats_;
};

void GpuShader::apply() { dev_->setCurrent(this); }

bool GpuDevice::init()
{
    const bool debug = SDL_getenv("MC2_GPU_DEBUG") != nullptr;
    if (!loadManifest("shaders/compiled")) {
        fprintf(stderr, "SDL3 GPU: shaders/compiled/shaders.txt is missing: build the project with "
                        "glslangValidator and spirv-val installed (see DEVELOPMENT.md)\n");
        return false;
    }
    // Ask only for the shader formats that were shipped. Offering DXIL
    // without the files made SDL pick D3D12 on Windows, every shader then
    // failed to load and the game crashed instead of using Vulkan.
    SDL_GPUShaderFormat shipped = 0;
    if (shaderFileExists("shaders/compiled", SDL_GPU_SHADERFORMAT_SPIRV))
        shipped |= SDL_GPU_SHADERFORMAT_SPIRV;
    if (shaderFileExists("shaders/compiled", SDL_GPU_SHADERFORMAT_DXIL))
        shipped |= SDL_GPU_SHADERFORMAT_DXIL;
    if (shaderFileExists("shaders/compiled", SDL_GPU_SHADERFORMAT_MSL))
        shipped |= SDL_GPU_SHADERFORMAT_MSL;
    if (!shipped) {
        fprintf(stderr, "SDL3 GPU: no compiled shaders in shaders/compiled\n");
        return false;
    }
    dev_ = SDL_CreateGPUDevice(shipped, debug, nullptr);
    if (!dev_) {
        fprintf(stderr, "SDL_CreateGPUDevice: %s\n", SDL_GetError());
        return false;
    }
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(dev_) & shipped;
    format_ = (formats & SDL_GPU_SHADERFORMAT_SPIRV) ? SDL_GPU_SHADERFORMAT_SPIRV
            : (formats & SDL_GPU_SHADERFORMAT_DXIL) ? SDL_GPU_SHADERFORMAT_DXIL
            : SDL_GPU_SHADERFORMAT_MSL;
    if (!SDL_ClaimWindowForGPUDevice(dev_, g_sdl_window)) {
        fprintf(stderr, "SDL_ClaimWindowForGPUDevice: %s\n", SDL_GetError());
        SDL_DestroyGPUDevice(dev_);
        dev_ = nullptr;
        return false;
    }
    // Fail here (and let the caller fall back to OpenGL) rather than later
    // with shaders that cannot be created
    {
        StageInfo info;
        SDL_GPUShader* probe = loadStage("shaders/compiled", kProbeShader, true, &info);
        if (!probe) {
            fprintf(stderr, "SDL3 GPU: the %s driver cannot create the shipped shaders\n",
                    SDL_GetGPUDeviceDriver(dev_));
            shutdown();
            return false;
        }
        SDL_ReleaseGPUShader(dev_, probe);
    }

    const SDL_GPUTextureFormat depth_candidates[] = {SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                                                     SDL_GPU_TEXTUREFORMAT_D32_FLOAT};
    for (SDL_GPUTextureFormat f : depth_candidates) {
        if (SDL_GPUTextureSupportsFormat(dev_, f, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            depth_format_ = f;
            break;
        }
    }

    // GL samples an unbound texture unit as opaque black
    SDL_GPUTextureCreateInfo ti;
    SDL_zero(ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = ti.height = 1;
    ti.layer_count_or_depth = ti.num_levels = 1;
    black_ = SDL_CreateGPUTexture(dev_, &ti);
    const uint8_t black[4] = {0, 0, 0, 255};
    submitUpload(black_, 1, 1, black, 4);

    name_ = std::string("SDL3 GPU ") + SDL_GetGPUDeviceDriver(dev_); // fits the options screen
    printf("Renderer   : %s%s\n", name_.c_str(), debug ? ", debug layers on" : "");
    ensureTargets();
    return true;
}

void GpuDevice::shutdown()
{
    if (!dev_)
        return;
    SDL_WaitForGPUIdle(dev_);
    for (auto& kv : shaders_) {
        SDL_ReleaseGPUShader(dev_, kv.second->vs_);
        SDL_ReleaseGPUShader(dev_, kv.second->fs_);
        delete kv.second;
    }
    shaders_.clear();
    for (auto& kv : pipelines_)
        SDL_ReleaseGPUGraphicsPipeline(dev_, kv.second);
    pipelines_.clear();
    for (auto& kv : samplers_)
        SDL_ReleaseGPUSampler(dev_, kv.second);
    samplers_.clear();
    for (auto& kv : textures_)
        SDL_ReleaseGPUTexture(dev_, kv.second.tex);
    textures_.clear();
    for (auto& kv : buffers_)
        if (kv.second->gpu)
            SDL_ReleaseGPUBuffer(dev_, kv.second->gpu);
    buffers_.clear();
    for (SDL_GPUTexture* t : release_textures_)
        SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUBuffer* b : release_buffers_)
        SDL_ReleaseGPUBuffer(dev_, b);
    release_textures_.clear();
    release_buffers_.clear();
    SDL_ReleaseGPUTexture(dev_, black_);
    SDL_ReleaseGPUTexture(dev_, color_);
    SDL_ReleaseGPUTexture(dev_, depth_);
    SDL_ReleaseGPUBuffer(dev_, ring_gpu_);
    SDL_ReleaseGPUTransferBuffer(dev_, upload_);
    SDL_ReleaseWindowFromGPUDevice(dev_, g_sdl_window);
    SDL_DestroyGPUDevice(dev_);
    dev_ = nullptr;
}

bool GpuDevice::shaderFileExists(const std::string& dir, SDL_GPUShaderFormat format) const
{
    return SDL_GetPathInfo(shaderPath(dir, kProbeShader, true, format).c_str(), nullptr);
}

std::string GpuDevice::shaderPath(const std::string& dir, const std::string& base, bool vertex,
                                  SDL_GPUShaderFormat format)
{
    const char* ext = vertex ? "vert" : "frag";
    if (format == SDL_GPU_SHADERFORMAT_SPIRV)
        return dir + "/spirv/" + base + "." + ext + ".spv";
    if (format == SDL_GPU_SHADERFORMAT_DXIL)
        return dir + "/dxil/" + base + "." + ext + ".dxil";
    return dir + "/msl/" + base + "." + ext + ".metal";
}

bool GpuDevice::loadManifest(const std::string& dir)
{
    std::vector<uint8_t> text = readFile(dir + "/shaders.txt");
    if (text.empty())
        return false;
    text.push_back(0);
    const char* p = (const char*)text.data();
    while (*p) {
        char prog[256], stage[32];
        unsigned samplers = 0, ubos = 0;
        int used = 0;
        if (sscanf(p, "%255s %31s %u %u%n", prog, stage, &samplers, &ubos, &used) == 4) {
            StageInfo info;
            info.samplers = samplers;
            info.uniform_buffers = ubos;
            manifest_[std::string(prog) + " " + stage] = info;
        }
        const char* nl = strchr(p, '\n');
        if (!nl)
            break;
        p = nl + 1;
    }
    return !manifest_.empty();
}

SDL_GPUShader* GpuDevice::loadStage(const std::string& dir, const std::string& base, bool vertex, StageInfo* info)
{
    auto it = manifest_.find(base + (vertex ? " vertex" : " fragment"));
    if (it == manifest_.end()) {
        fprintf(stderr, "SDL3 GPU: %s is not in shaders.txt\n", base.c_str());
        return nullptr;
    }
    *info = it->second;
    const std::string path = shaderPath(dir, base, vertex, format_);
    // spirv-cross renames main for Metal
    const char* entry = format_ == SDL_GPU_SHADERFORMAT_MSL ? "main0" : "main";
    std::vector<uint8_t> code = readFile(path);
    if (code.empty()) {
        fprintf(stderr, "SDL3 GPU: cannot read %s\n", path.c_str());
        return nullptr;
    }
    if (format_ == SDL_GPU_SHADERFORMAT_MSL)
        code.push_back(0);
    SDL_GPUShaderCreateInfo ci;
    SDL_zero(ci);
    ci.code = code.data();
    ci.code_size = code.size();
    ci.entrypoint = entry;
    ci.format = format_;
    ci.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = info->samplers;
    ci.num_uniform_buffers = info->uniform_buffers;
    SDL_GPUShader* s = SDL_CreateGPUShader(dev_, &ci);
    if (!s)
        fprintf(stderr, "SDL_CreateGPUShader(%s): %s\n", path.c_str(), SDL_GetError());
    return s;
}

Shader* GpuDevice::createShader(const char* name, const char* vs_path, const char* fs_path, const char* defines)
{
    (void)fs_path;
    // "shaders/gos_vertex.vert" -> program "gos_vertex", compiled files in
    // "shaders/compiled"; the defines select the variant.
    std::string vs = vs_path;
    const size_t slash = vs.find_last_of('/');
    const std::string dir = (slash == std::string::npos ? std::string(".") : vs.substr(0, slash)) + "/compiled";
    std::string prog = slash == std::string::npos ? vs : vs.substr(slash + 1);
    prog = prog.substr(0, prog.find('.'));
    const std::string variant = (defines && strstr(defines, "ALPHA_TEST")) ? "alpha_test" : "base";
    const std::string base = prog + "." + variant;

    StageInfo vsi, fsi;
    SDL_GPUShader* v = loadStage(dir, base, true, &vsi);
    SDL_GPUShader* f = v ? loadStage(dir, base, false, &fsi) : nullptr;
    if (!v || !f) {
        if (v)
            SDL_ReleaseGPUShader(dev_, v);
        return nullptr;
    }
    GpuShader* s = new GpuShader(this, v, f, vsi, fsi);
    shaders_[name] = s;
    return s;
}

void GpuDevice::destroyShader(const char* name)
{
    auto it = shaders_.find(name);
    if (it == shaders_.end())
        return;
    GpuShader* s = it->second;
    // pipelines reference the shader: drop them (rare: shutdown only)
    flush(false, 0, 0, nullptr);
    for (auto p = pipelines_.begin(); p != pipelines_.end();) {
        if (p->first.compare(0, sizeof(GpuShader*), (const char*)&s, sizeof(GpuShader*)) == 0) {
            SDL_ReleaseGPUGraphicsPipeline(dev_, p->second);
            p = pipelines_.erase(p);
        } else {
            ++p;
        }
    }
    if (current_ == s)
        current_ = nullptr;
    SDL_ReleaseGPUShader(dev_, s->vs_);
    SDL_ReleaseGPUShader(dev_, s->fs_);
    delete s;
    shaders_.erase(it);
}

void GpuDevice::ensureTargets()
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(g_sdl_window, &w, &h);
    if (w <= 0 || h <= 0)
        w = h = 1;
    if (color_ && w == target_w_ && h == target_h_)
        return;
    if (!cmds_.empty())
        flush(false, 0, 0, nullptr);
    if (color_)
        release_textures_.push_back(color_);
    if (depth_)
        release_textures_.push_back(depth_);
    SDL_GPUTextureCreateInfo ti;
    SDL_zero(ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = ti.num_levels = 1;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    color_ = SDL_CreateGPUTexture(dev_, &ti);
    ti.format = depth_format_;
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_ = SDL_CreateGPUTexture(dev_, &ti);
    target_w_ = w;
    target_h_ = h;
    target_valid_ = false;
}

SDL_GPUSampler* GpuDevice::sampler(const SamplerDesc& s)
{
    const SamplerKey key = packSamplerKey(s);
    auto it = samplers_.find(key);
    if (it != samplers_.end())
        return it->second;
    SDL_GPUSamplerCreateInfo ci;
    SDL_zero(ci);
    ci.min_filter = ci.mag_filter = s.filter == FilterMode::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    ci.address_mode_u = ci.address_mode_v = ci.address_mode_w =
        s.address == AddressMode::Wrap ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SDL_GPUSampler* smp = SDL_CreateGPUSampler(dev_, &ci);
    samplers_[key] = smp;
    return smp;
}

SDL_GPUGraphicsPipeline* GpuDevice::pipeline(Primitive p, const VertexAttrib* a, int n)
{
    // Key: shader, state, primitive and vertex layout
    std::string key;
    key.append((const char*)&current_, sizeof(current_));
    const PipelineKey pk = packPipelineKey(desc_);
    key.append((const char*)&pk, sizeof(pk));
    key.push_back((char)p);
    for (int i = 0; i < n; ++i) {
        if (a[i].location < 0)
            continue;
        const int32_t v[5] = {a[i].location, a[i].components, (int32_t)a[i].type, a[i].normalized, a[i].offset};
        key.append((const char*)v, sizeof(v));
    }
    const int32_t stride = n ? a[0].stride : 0;
    key.append((const char*)&stride, sizeof(stride));
    auto it = pipelines_.find(key);
    if (it != pipelines_.end())
        return it->second;

    SDL_GPUVertexAttribute attrs[16];
    uint32_t num_attrs = 0;
    for (int i = 0; i < n && num_attrs < 16; ++i) {
        if (a[i].location < 0)
            continue;
        attrs[num_attrs].location = a[i].location;
        attrs[num_attrs].buffer_slot = 0;
        attrs[num_attrs].format = vertexFormat(a[i]);
        attrs[num_attrs].offset = a[i].offset;
        ++num_attrs;
    }
    SDL_GPUVertexBufferDescription vbd;
    SDL_zero(vbd);
    vbd.slot = 0;
    vbd.pitch = stride;
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUColorTargetDescription ctd;
    SDL_zero(ctd);
    ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ctd.blend_state = blendState(desc_.blend);

    SDL_GPUGraphicsPipelineCreateInfo ci;
    SDL_zero(ci);
    ci.vertex_shader = current_->vs_;
    ci.fragment_shader = current_->fs_;
    ci.vertex_input_state.vertex_buffer_descriptions = &vbd;
    ci.vertex_input_state.num_vertex_buffers = 1;
    ci.vertex_input_state.vertex_attributes = attrs;
    ci.vertex_input_state.num_vertex_attributes = num_attrs;
    ci.primitive_type = p == Primitive::Points ? SDL_GPU_PRIMITIVETYPE_POINTLIST
                      : p == Primitive::Lines ? SDL_GPU_PRIMITIVETYPE_LINELIST
                      : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = desc_.cull == CullMode::None ? SDL_GPU_CULLMODE_NONE
                                  : desc_.cull == CullMode::Back ? SDL_GPU_CULLMODE_BACK
                                  : SDL_GPU_CULLMODE_FRONT;
    ci.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE; // GL default
    ci.rasterizer_state.enable_depth_clip = true;
    ci.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    ci.depth_stencil_state.enable_depth_test = desc_.depth_test != DepthTest::Off;
    ci.depth_stencil_state.enable_depth_write = desc_.depth_test != DepthTest::Off && desc_.depth_write;
    ci.depth_stencil_state.compare_op = desc_.depth_test == DepthTest::Less ? SDL_GPU_COMPAREOP_LESS
                                      : desc_.depth_test == DepthTest::LessEqual ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL
                                      : SDL_GPU_COMPAREOP_ALWAYS;
    ci.target_info.color_target_descriptions = &ctd;
    ci.target_info.num_color_targets = 1;
    ci.target_info.depth_stencil_format = depth_format_;
    ci.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* pl = SDL_CreateGPUGraphicsPipeline(dev_, &ci);
    if (!pl)
        fprintf(stderr, "SDL_CreateGPUGraphicsPipeline: %s\n", SDL_GetError());
    pipelines_[key] = pl;
    return pl;
}

uint32_t GpuDevice::appendStaging(std::vector<uint8_t>& v, const void* data, size_t size, uint32_t align)
{
    const uint32_t offset = alignUp((uint32_t)v.size(), align);
    v.resize(offset + size);
    if (size)
        memcpy(v.data() + offset, data, size);
    return offset;
}

bool GpuDevice::locate(GpuBuffer* b, SDL_GPUBuffer** buf, uint32_t* offset)
{
    if (b->gpu && b->gpu_version == b->version) {
        *buf = b->gpu;
        *offset = 0;
        return true;
    }
    if (b->ring_flush != flush_id_ || b->ring_version != b->version) {
        b->ring_offset = appendStaging(ring_, b->data.data(), b->data.size(), 16);
        b->ring_flush = flush_id_;
        b->ring_version = b->version;
    }
    *buf = nullptr; // the ring
    *offset = b->ring_offset;
    return true;
}

void GpuDevice::recordDraw(Primitive p, BufferHandle vbh, BufferHandle ibh, int index_size, const VertexAttrib* a,
                           int n, int first, int count)
{
    if (!current_ || count <= 0)
        return;
    auto vit = buffers_.find(vbh);
    if (vit == buffers_.end())
        return;
    SDL_GPUGraphicsPipeline* pl = pipeline(p, a, n);
    if (!pl)
        return;

    Cmd c;
    c.type = CmdType::Draw;
    c.pipeline = pl;
    locate(vit->second.get(), &c.vb, &c.vb_offset);
    if (ibh) {
        auto iit = buffers_.find(ibh);
        if (iit == buffers_.end())
            return;
        c.indexed = true;
        c.index32 = index_size == 4;
        locate(iit->second.get(), &c.ib, &c.ib_offset);
    }
    c.first = first;
    c.count = count;

    c.num_samplers = std::min(current_->fs_info_.samplers, kMaxTextureUnits);
    for (uint32_t i = 0; i < c.num_samplers; ++i) {
        c.samplers[i].texture = bound_[i] ? bound_[i]->tex : black_;
        c.samplers[i].sampler = sampler(bound_sampler_[i]);
    }

    // Uniform data: PerDraw, then the selected ObjectLights, then SceneData
    c.vs_ubos = current_->vs_info_.uniform_buffers;
    c.fs_ubos = current_->fs_info_.uniform_buffers;
    c.uniforms = appendStaging(uniforms_, &current_params_, sizeof(PerDraw), 16);
    if (std::max(c.vs_ubos, c.fs_ubos) >= 2) {
        uint8_t lights[kObjectLightsSize];
        memset(lights, 0, sizeof(lights));
        auto lit = buffers_.find(uniform_slots_[kLightDataSlot]);
        const int index = (int)current_params_.light_offset_[0];
        if (lit != buffers_.end() && index >= 0 &&
            (size_t)(index + 1) * kObjectLightsSize <= lit->second->data.size())
            memcpy(lights, lit->second->data.data() + (size_t)index * kObjectLightsSize, kObjectLightsSize);
        appendStaging(uniforms_, lights, sizeof(lights), 1);
        uint8_t scene[kSceneDataSize];
        memset(scene, 0, sizeof(scene));
        auto sit = buffers_.find(uniform_slots_[kSceneDataSlot]);
        if (sit != buffers_.end())
            memcpy(scene, sit->second->data.data(), std::min<size_t>(sizeof(scene), sit->second->data.size()));
        appendStaging(uniforms_, scene, sizeof(scene), 1);
    }
    cmds_.push_back(c);
    ++stats_.draw_calls;
}

Texture GpuDevice::createTexture2D(int w, int h, TexFormat fmt, const uint8_t* data)
{
    Texture t;
    const TexFormat storage = storageFormat(fmt);
    SDL_GPUTextureCreateInfo ti;
    SDL_zero(ti);
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = gpuFormat(storage);
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = ti.num_levels = 1;
    if (ti.format == SDL_GPU_TEXTUREFORMAT_INVALID || w <= 0 || h <= 0)
        return t;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(dev_, &ti);
    if (!tex) {
        fprintf(stderr, "SDL_CreateGPUTexture: %s\n", SDL_GetError());
        return t;
    }
    GpuTexture gt;
    gt.tex = tex;
    gt.w = w;
    gt.h = h;
    gt.fmt = fmt;
    gt.storage = storage;
    const uint32_t id = next_texture_id_++;
    textures_[id] = gt;
    if (data) {
        const size_t n = (size_t)w * h;
        std::vector<uint8_t> px(n * getTexFormatPixelSize(storage));
        convertPixels(data, fmt, px.data(), storage, n);
        submitUpload(tex, w, h, px.data(), px.size());
    }
    t.id = id;
    t.w = w;
    t.h = h;
    t.fmt_ = fmt;
    t.type_ = TT_2D;
    t.format = 0;
    return t;
}

// Immediate upload (texture creation): its own small command buffer
void GpuDevice::submitUpload(SDL_GPUTexture* tex, int w, int h, const void* pixels, size_t size)
{
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_zero(tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = (Uint32)size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tci);
    void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
    memcpy(p, pixels, size);
    SDL_UnmapGPUTransferBuffer(dev_, tb);
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureTransferInfo src;
    SDL_zero(src);
    src.transfer_buffer = tb;
    SDL_GPUTextureRegion dst;
    SDL_zero(dst);
    dst.texture = tex;
    dst.w = w;
    dst.h = h;
    dst.d = 1;
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cb);
    SDL_ReleaseGPUTransferBuffer(dev_, tb); // deferred until the upload is done
}

void GpuDevice::updateTexture(const Texture& t, const void* data, TexFormat data_fmt)
{
    auto it = textures_.find(t.id);
    if (it == textures_.end() || !data)
        return;
    const GpuTexture& gt = it->second;
    const size_t n = (size_t)gt.w * gt.h;
    std::vector<uint8_t> px(n * getTexFormatPixelSize(gt.storage));
    convertPixels(data, data_fmt == TF_COUNT ? gt.fmt : data_fmt, px.data(), gt.storage, n);
    // In order with the draws: replayed between them
    Cmd c;
    c.type = CmdType::TexUpload;
    c.tex = gt.tex;
    c.w = gt.w;
    c.h = gt.h;
    c.staging_offset = appendStaging(tex_staging_, px.data(), px.size(), 16);
    cmds_.push_back(c);
}

void GpuDevice::updateTextureRegion(const Texture& t, int x, int y, int w, int h, const void* data, TexFormat data_fmt)
{
    auto it = textures_.find(t.id);
    if (it == textures_.end() || !data || w <= 0 || h <= 0)
        return;
    const GpuTexture& gt = it->second;
    const size_t n = (size_t)w * h;
    std::vector<uint8_t> px(n * getTexFormatPixelSize(gt.storage));
    convertPixels(data, data_fmt == TF_COUNT ? gt.fmt : data_fmt, px.data(), gt.storage, n);
    Cmd c;
    c.type = CmdType::TexUpload;
    c.tex = gt.tex;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.staging_offset = appendStaging(tex_staging_, px.data(), px.size(), 16);
    cmds_.push_back(c);
}

bool GpuDevice::readTexture(const Texture& t, uint8_t* out, TexFormat out_fmt)
{
    auto it = textures_.find(t.id);
    if (it == textures_.end())
        return false;
    const GpuTexture gt = it->second;
    flush(false, 0, 0, nullptr); // pending updates of this texture
    const uint32_t size = (uint32_t)gt.w * gt.h * getTexFormatPixelSize(gt.storage);
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_zero(tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev_, &tci);
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev_);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureRegion src;
    SDL_zero(src);
    src.texture = gt.tex;
    src.w = gt.w;
    src.h = gt.h;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst;
    SDL_zero(dst);
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    SDL_WaitForGPUFences(dev_, true, &fence, 1);
    SDL_ReleaseGPUFence(dev_, fence);
    const void* p = SDL_MapGPUTransferBuffer(dev_, tb, false);
    convertPixels(p, gt.storage, out, out_fmt == TF_COUNT ? gt.fmt : out_fmt, (size_t)gt.w * gt.h);
    SDL_UnmapGPUTransferBuffer(dev_, tb);
    SDL_ReleaseGPUTransferBuffer(dev_, tb);
    return true;
}

void GpuDevice::destroyTexture(Texture* t)
{
    auto it = textures_.find(t->id);
    if (it != textures_.end()) {
        for (GpuTexture*& b : bound_)
            if (b == &it->second)
                b = nullptr;
        release_textures_.push_back(it->second.tex); // may be used by recorded draws
        textures_.erase(it);
    }
    t->id = 0;
}

void GpuDevice::bindTexture(int unit, const Texture* t, const SamplerDesc& s)
{
    if (unit < 0 || unit >= (int)kMaxTextureUnits)
        return;
    GpuTexture* gt = nullptr;
    if (t) {
        auto it = textures_.find(t->id);
        if (it != textures_.end())
            gt = &it->second;
    }
    bound_[unit] = gt;
    bound_sampler_[unit] = s;
}

BufferHandle GpuDevice::createBuffer(BufferKind kind, BufferUsage usage, const void* data, size_t size)
{
    std::unique_ptr<GpuBuffer> b(new GpuBuffer());
    b->kind = kind;
    b->usage = usage;
    if (data)
        b->data.assign((const uint8_t*)data, (const uint8_t*)data + size);
    else
        b->data.assign(size, 0);
    const uint32_t id = next_buffer_id_++;
    queuePromotion(id, b.get());
    buffers_[id] = std::move(b);
    return id;
}

void GpuDevice::updateBuffer(BufferHandle h, BufferKind, const void* data, size_t size)
{
    auto it = buffers_.find(h);
    if (it == buffers_.end())
        return;
    GpuBuffer* b = it->second.get();
    // like glBufferData: the buffer takes the new size
    if (data)
        b->data.assign((const uint8_t*)data, (const uint8_t*)data + size);
    else
        b->data.assign(size, 0);
    ++b->version;
    queuePromotion(h, b);
}

void GpuDevice::destroyBuffer(BufferHandle h)
{
    auto it = buffers_.find(h);
    if (it == buffers_.end())
        return;
    if (it->second->gpu)
        release_buffers_.push_back(it->second->gpu);
    buffers_.erase(it);
}

bool GpuDevice::readPixels(int w, int h, uint8_t* rgba)
{
    std::vector<uint8_t> px;
    if (!flush(false, w, h, &px))
        return false;
    // the Device contract is bottom row first (GL)
    const size_t row = (size_t)w * 4;
    for (int y = 0; y < h; ++y)
        memcpy(rgba + (size_t)y * row, px.data() + (size_t)(h - 1 - y) * row, row);
    return true;
}

void GpuDevice::present()
{
    flush(true, 0, 0, nullptr);
    target_valid_ = false;
}

bool GpuDevice::flush(bool present, int dl_w, int dl_h, std::vector<uint8_t>* download)
{
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(dev_);
    if (!cb) {
        fprintf(stderr, "SDL_AcquireGPUCommandBuffer: %s\n", SDL_GetError());
        return false;
    }

    // Buffers to copy to their own GPU buffer after the replay: static
    // buffers whose contents changed (drawn from the ring until then)
    std::vector<GpuBuffer*> promote;
    for (uint32_t id : promote_queue_) {
        auto it = buffers_.find(id);
        if (it == buffers_.end())
            continue; // destroyed since
        GpuBuffer* b = it->second.get();
        b->queued_for_promotion = false;
        if (b->gpu_version != b->version && !b->data.empty())
            promote.push_back(b);
    }
    promote_queue_.clear();

    // One transfer buffer: [ring][texture staging][promoted buffers]
    const uint32_t ring_size = alignUp((uint32_t)ring_.size(), 16);
    const uint32_t staging_off = ring_size;
    uint32_t total = staging_off + alignUp((uint32_t)tex_staging_.size(), 16);
    std::vector<uint32_t> promote_off;
    for (GpuBuffer* b : promote) {
        promote_off.push_back(total);
        total += alignUp((uint32_t)b->data.size(), 16);
    }
    if (total) {
        if (total > upload_size_) {
            if (upload_)
                SDL_ReleaseGPUTransferBuffer(dev_, upload_);
            upload_size_ = std::max<uint32_t>(total, upload_size_ * 2);
            SDL_GPUTransferBufferCreateInfo tci;
            SDL_zero(tci);
            tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tci.size = upload_size_;
            upload_ = SDL_CreateGPUTransferBuffer(dev_, &tci);
        }
        uint8_t* p = (uint8_t*)SDL_MapGPUTransferBuffer(dev_, upload_, true);
        // an empty vector's data() may be null, which memcpy does not allow
        if (!ring_.empty())
            memcpy(p, ring_.data(), ring_.size());
        if (!tex_staging_.empty())
            memcpy(p + staging_off, tex_staging_.data(), tex_staging_.size());
        for (size_t i = 0; i < promote.size(); ++i)
            if (!promote[i]->data.empty())
                memcpy(p + promote_off[i], promote[i]->data.data(), promote[i]->data.size());
        SDL_UnmapGPUTransferBuffer(dev_, upload_);
    }
    if (ring_size) {
        if (ring_size > ring_gpu_size_) {
            if (ring_gpu_)
                release_buffers_.push_back(ring_gpu_);
            ring_gpu_size_ = std::max<uint32_t>(ring_size, ring_gpu_size_ * 2);
            SDL_GPUBufferCreateInfo bci;
            SDL_zero(bci);
            bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
            bci.size = ring_gpu_size_;
            ring_gpu_ = SDL_CreateGPUBuffer(dev_, &bci);
        }
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
        SDL_GPUTransferBufferLocation src = {upload_, 0};
        SDL_GPUBufferRegion dst = {ring_gpu_, 0, ring_size};
        SDL_UploadToGPUBuffer(cp, &src, &dst, true); // cycle: the last frame may still read it
        SDL_EndGPUCopyPass(cp);
    }

    // Replay
    SDL_GPURenderPass* pass = nullptr;
    bool clear_color = false, clear_depth = false;
    SDL_GPUViewport viewport = {0, kRasterBias, (float)target_w_, (float)target_h_, 0, 1};
    SDL_GPUGraphicsPipeline* bound_pipeline = nullptr;
    const uint8_t* last_push[2][3] = {};
    auto beginPass = [&]() {
        SDL_GPUColorTargetInfo ct;
        SDL_zero(ct);
        ct.texture = color_;
        ct.clear_color = {0, 0, 0, 0};
        ct.load_op = clear_color ? SDL_GPU_LOADOP_CLEAR : (target_valid_ ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR);
        ct.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo dt;
        SDL_zero(dt);
        dt.texture = depth_;
        dt.clear_depth = 1.0f;
        dt.load_op = clear_depth ? SDL_GPU_LOADOP_CLEAR : (target_valid_ ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_CLEAR);
        dt.store_op = SDL_GPU_STOREOP_STORE;
        dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        pass = SDL_BeginGPURenderPass(cb, &ct, 1, &dt);
        SDL_SetGPUViewport(pass, &viewport);
        clear_color = clear_depth = false;
        target_valid_ = true;
        bound_pipeline = nullptr;
        memset(last_push, 0, sizeof(last_push));
    };
    auto endPass = [&]() {
        if (pass)
            SDL_EndGPURenderPass(pass);
        pass = nullptr;
    };

    for (const Cmd& c : cmds_) {
        switch (c.type) {
        case CmdType::Clear:
            endPass();
            clear_color |= c.clear_color;
            clear_depth |= c.clear_depth;
            break;
        case CmdType::Viewport:
            viewport = c.viewport;
            if (pass)
                SDL_SetGPUViewport(pass, &viewport);
                break;
        case CmdType::TexUpload: {
            endPass();
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
            SDL_GPUTextureTransferInfo src;
            SDL_zero(src);
            src.transfer_buffer = upload_;
            src.offset = staging_off + c.staging_offset;
            SDL_GPUTextureRegion dst;
            SDL_zero(dst);
            dst.texture = c.tex;
            dst.x = c.x;
            dst.y = c.y;
            dst.w = c.w;
            dst.h = c.h;
            dst.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, false);
            SDL_EndGPUCopyPass(cp);
            break;
        }
        case CmdType::Draw: {
            if (!pass)
                beginPass();
            if (c.pipeline != bound_pipeline) {
                SDL_BindGPUGraphicsPipeline(pass, c.pipeline);
                bound_pipeline = c.pipeline;
            }
            SDL_GPUBufferBinding vb = {c.vb ? c.vb : ring_gpu_, c.vb_offset};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            if (c.indexed) {
                SDL_GPUBufferBinding ib = {c.ib ? c.ib : ring_gpu_, c.ib_offset};
                SDL_BindGPUIndexBuffer(pass, &ib, c.index32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                                                             : SDL_GPU_INDEXELEMENTSIZE_16BIT);
            }
            if (c.num_samplers)
                SDL_BindGPUFragmentSamplers(pass, 0, c.samplers, c.num_samplers);
            // uniform slots: 0 PerDraw, 1 LightsData entry, 2 SceneData
            const uint8_t* u = uniforms_.data() + c.uniforms;
            const uint8_t* data[3] = {u, u + sizeof(PerDraw), u + sizeof(PerDraw) + kObjectLightsSize};
            const uint32_t size[3] = {sizeof(PerDraw), kObjectLightsSize, kSceneDataSize};
            for (uint32_t s = 0; s < 3; ++s) {
                for (int stage = 0; stage < 2; ++stage) {
                    if (s >= (stage ? c.fs_ubos : c.vs_ubos))
                        continue;
                    const uint8_t*& last = last_push[stage][s];
                    if (last && (last == data[s] || !memcmp(last, data[s], size[s])))
                        continue;
                    if (stage)
                        SDL_PushGPUFragmentUniformData(cb, s, data[s], size[s]);
                    else
                        SDL_PushGPUVertexUniformData(cb, s, data[s], size[s]);
                    last = data[s];
                }
            }
            if (c.indexed)
                SDL_DrawGPUIndexedPrimitives(pass, c.count, 1, 0, 0, 0);
            else
                SDL_DrawGPUPrimitives(pass, c.count, 1, c.first, 0);
            break;
        }
        }
    }
    if (!pass && (clear_color || clear_depth))
        beginPass(); // a clear with no draw after it
    endPass();

    // Static buffers to their own GPU copy (after the replay: draws above
    // used the ring for contents that changed during this frame)
    if (!promote.empty()) {
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
        for (size_t i = 0; i < promote.size(); ++i) {
            GpuBuffer* b = promote[i];
            const uint32_t size = (uint32_t)b->data.size();
            if (!b->gpu || b->gpu_size < size) {
                if (b->gpu)
                    release_buffers_.push_back(b->gpu);
                SDL_GPUBufferCreateInfo bci;
                SDL_zero(bci);
                bci.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
                bci.size = size;
                b->gpu = SDL_CreateGPUBuffer(dev_, &bci);
                b->gpu_size = size;
            }
            SDL_GPUTransferBufferLocation src = {upload_, promote_off[i]};
            SDL_GPUBufferRegion dst = {b->gpu, 0, size};
            SDL_UploadToGPUBuffer(cp, &src, &dst, true);
            b->gpu_version = b->version;
        }
        SDL_EndGPUCopyPass(cp);
    }

    SDL_GPUTransferBuffer* dl = nullptr;
    if (download) {
        SDL_GPUTransferBufferCreateInfo tci;
        SDL_zero(tci);
        tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tci.size = (Uint32)dl_w * dl_h * 4;
        dl = SDL_CreateGPUTransferBuffer(dev_, &tci);
        SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cb);
        SDL_GPUTextureRegion src;
        SDL_zero(src);
        src.texture = color_;
        src.w = std::min(dl_w, target_w_);
        src.h = std::min(dl_h, target_h_);
        src.d = 1;
        SDL_GPUTextureTransferInfo dst;
        SDL_zero(dst);
        dst.transfer_buffer = dl;
        dst.pixels_per_row = dl_w;
        SDL_DownloadFromGPUTexture(cp, &src, &dst);
        SDL_EndGPUCopyPass(cp);
    }

    if (present) {
        SDL_GPUTexture* swapchain = nullptr;
        Uint32 sw = 0, sh = 0;
        if (SDL_WaitAndAcquireGPUSwapchainTexture(cb, g_sdl_window, &swapchain, &sw, &sh) && swapchain) {
            SDL_GPUBlitInfo bi;
            SDL_zero(bi);
            bi.source.texture = color_;
            bi.source.w = target_w_;
            bi.source.h = target_h_;
            bi.destination.texture = swapchain;
            bi.destination.w = sw;
            bi.destination.h = sh;
            bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
            bi.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cb, &bi);
        }
    }

    bool ok = true;
    if (dl) {
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
        ok = fence != nullptr;
        if (fence) {
            SDL_WaitForGPUFences(dev_, true, &fence, 1);
            SDL_ReleaseGPUFence(dev_, fence);
            const uint8_t* p = (const uint8_t*)SDL_MapGPUTransferBuffer(dev_, dl, false);
            download->assign(p, p + (size_t)dl_w * dl_h * 4);
            SDL_UnmapGPUTransferBuffer(dev_, dl);
        }
        SDL_ReleaseGPUTransferBuffer(dev_, dl);
    } else {
        ok = SDL_SubmitGPUCommandBuffer(cb);
    }

    // SDL defers these until the GPU is done with them
    for (SDL_GPUTexture* t : release_textures_)
        SDL_ReleaseGPUTexture(dev_, t);
    for (SDL_GPUBuffer* b : release_buffers_)
        SDL_ReleaseGPUBuffer(dev_, b);
    release_textures_.clear();
    release_buffers_.clear();

    cmds_.clear();
    ring_.clear();
    tex_staging_.clear();
    uniforms_.clear();
    ++flush_id_;
    return ok;
}

} // namespace

Device* createGpuDevice() { return new GpuDevice(); }

} // namespace gos_render
