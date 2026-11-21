#include "render/render_state.h"

#include "gameos.hpp"

namespace gos_render {

// Key layout (bits): blend 0-2 | cull 3-4 | depth test 5-6 | depth write 7 |
// alpha test 8. Everything above bit 8 is zero.
PipelineKey packPipelineKey(const PipelineDesc& d)
{
    return (uint32_t)d.blend | ((uint32_t)d.cull << 3) | ((uint32_t)d.depth_test << 5) |
           ((uint32_t)d.depth_write << 7) | ((uint32_t)d.alpha_test << 8);
}

bool unpackPipelineKey(PipelineKey key, PipelineDesc* out)
{
    const uint32_t blend = key & 7, cull = (key >> 3) & 3, depth = (key >> 5) & 3;
    if (key >> 9 || blend >= (uint32_t)BlendMode::Count || cull >= (uint32_t)CullMode::Count ||
        depth >= (uint32_t)DepthTest::Count)
        return false;
    if (out) {
        out->blend = (BlendMode)blend;
        out->cull = (CullMode)cull;
        out->depth_test = (DepthTest)depth;
        out->depth_write = (key >> 7) & 1;
        out->alpha_test = (key >> 8) & 1;
    }
    return true;
}

SamplerKey packSamplerKey(const SamplerDesc& d)
{
    return (uint32_t)d.filter | ((uint32_t)d.address << 1);
}

PipelineDesc pipelineFromGosStates(const uint32_t* s, bool* valid)
{
    PipelineDesc d;
    bool ok = true;

    switch (s[gos_State_AlphaMode]) {
    case gos_Alpha_OneZero:       d.blend = BlendMode::Opaque; break;
    case gos_Alpha_OneOne:        d.blend = BlendMode::Additive; break;
    case gos_Alpha_AlphaInvAlpha: d.blend = BlendMode::AlphaBlend; break;
    case gos_Alpha_OneInvAlpha:   d.blend = BlendMode::Premultiplied; break;
    case gos_Alpha_AlphaOne:      d.blend = BlendMode::AlphaAdditive; break;
    default: ok = false;
    }

    // GL's default front face is CCW: culling clockwise faces culls back
    // faces (the convention the original renderer used).
    switch (s[gos_State_Culling]) {
    case gos_Cull_None: d.cull = CullMode::None; break;
    case gos_Cull_CW:   d.cull = CullMode::Back; break;
    case gos_Cull_CCW:  d.cull = CullMode::Front; break;
    default: ok = false;
    }

    switch (s[gos_State_ZCompare]) {
    case 0: d.depth_test = DepthTest::Off; break;
    case 1: d.depth_test = DepthTest::LessEqual; break;
    case 2: d.depth_test = DepthTest::Less; break;
    default: ok = false;
    }

    if (s[gos_State_ZWrite] > 1)
        ok = false;
    d.depth_write = s[gos_State_ZWrite] == 1;
    d.alpha_test = s[gos_State_AlphaTest] == 1;

    if (valid)
        *valid = ok;
    return d;
}

SamplerDesc samplerFromGosStates(const uint32_t* s)
{
    SamplerDesc d;
    // No mipmaps are generated, so trilinear behaves like bilinear
    d.filter = s[gos_State_Filter] == gos_FilterNone ? FilterMode::Nearest : FilterMode::Linear;
    d.address = s[gos_State_TextureAddress] == gos_TextureWrap ? AddressMode::Wrap : AddressMode::Clamp;
    return d;
}

} // namespace gos_render
