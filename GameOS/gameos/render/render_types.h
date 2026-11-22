// SPDX-License-Identifier: GPL-3.0-only
#ifndef GOS_RENDER_TYPES_H
#define GOS_RENDER_TYPES_H

// Backend independent texture and sampler types shared by GameOS and the
// render backends. No graphics API headers here.

#ifdef PLATFORM_WINDOWS
	#include<stdint.h>
#endif
#include <inttypes.h>
#include <stddef.h>


#define DEPTH_NONE 0        /**< Disable depthstencil. */
#define BLEND_NONE (-1)     /**< Disable blending. */

enum TexType {
    TT_NONE,
    TT_1D,
    TT_1D_ARRAY,
    TT_2D,
    TT_2D_ARRAY,
    TT_2D_MULTISAMPLE,
    TT_2D_MULTISAMPLE_ARRAY,
    TT_3D,
    TT_CUBE_MAP,
    TT_CUBE_MAP_ARRAY,
    TT_COUNT
};

enum TexFormat {
    TF_NONE,
    TF_R8,
    TF_RG8,
    TF_RGB8,
    TF_RGBA8,
    TF_R32F,
    TF_RG32F,
    TF_RGB32F,
    TF_RGBA32F,
    TF_COUNT
};

enum TexAddressMode {
    TAM_NONE,
    TAM_CLAMP_TO_EDGE,
    TAM_REPEAT,
    TAM_COUNT
};

/*extern const int tex_filter_mode_nearest;
extern const int tex_filer_mode_linear;
extern const int tex_filter_mode_nearest_mipmap_nearest;
extern const int tex_filter_mode_nearest_mipmap_linear;
extern const int tex_filter_mode_linear_mipmap_nearest;
extern const int tex_filter_mode_linear_mipmap_linear;
*/

enum TexFilterMode {
    TFM_NONE = 0,
    TFM_NEAREST,                 //= tex_filter_mode_nearest,
    TFM_LINEAR,                  //= tex_filer_mode_linear,
    TFM_NEAREST_MIPMAP_NEAREST,  //= tex_filter_mode_nearest_mipmap_nearest,
    TFM_NEAREST_MIPMAP_LINEAR,   //= tex_filter_mode_nearest_mipmap_linear,
    TFM_LNEAR_MIPMAP_NEAREST,    //= tex_filter_mode_linear_mipmap_nearest,
    TFM_LNEAR_MIPMAP_LINEAR,     //= tex_filter_mode_linear_mipmap_linear
    TFM_COUNT
};

// A texture as the backends see it. `id` is the backend's handle (0 = none);
// `format` is backend specific (the GL internal format, for instance).
struct Texture {
    Texture():id(0), format(0), w(0), h(0), depth(1), fmt_(TF_NONE), type_(TT_NONE) {}
    bool isValid() const { return id > 0; }

    uint32_t id;
    uint32_t format;
    int w, h, depth;
    TexFormat fmt_;
    TexType type_;
};

uint32_t getTexFormatPixelSize(TexFormat fmt);

// Packed 0xAABBGGRR <-> 0..1 floats (GameOS colours, fog)
struct vec4;
uint32_t vec4_to_uint32(const vec4& v);
vec4 uint32_to_vec4(uint32_t v);

#endif // RENDER_CONSTANTS_H
