// SPDX-License-Identifier: GPL-3.0-only
#include "render/render_types.h"

#include "utils/vec.h"

uint32_t getTexFormatPixelSize(TexFormat fmt)
{
    static const uint32_t bytes[TF_COUNT] = {
        0,  // TF_NONE
        1,  // TF_R8
        2,  // TF_RG8
        3,  // TF_RGB8
        4,  // TF_RGBA8
        4,  // TF_R32F
        8,  // TF_RG32F
        12, // TF_RGB32F
        16, // TF_RGBA32F
    };
    return (fmt >= 0 && fmt < TF_COUNT) ? bytes[fmt] : 0;
}

uint32_t vec4_to_uint32(const vec4& v)
{
    uint32_t x = (uint32_t)(clamp(v.x, 0.0f, 1.0f) * 255.0f);
    uint32_t y = (uint32_t)(clamp(v.y, 0.0f, 1.0f) * 255.0f);
    uint32_t z = (uint32_t)(clamp(v.z, 0.0f, 1.0f) * 255.0f);
    uint32_t w = (uint32_t)(clamp(v.w, 0.0f, 1.0f) * 255.0f);
    return x | (y << 8) | (z << 16) | (w << 24);
}

vec4 uint32_to_vec4(uint32_t v)
{
    float x = v & 0xff;
    float y = (v >> 8) & 0xff;
    float z = (v >> 16) & 0xff;
    float w = (v >> 24) & 0xff;
    return (1.0f / 255.0f) * vec4(x, y, z, w);
}
