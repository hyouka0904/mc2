#ifndef GOS_RENDER_SHADER_PARAMS_H
#define GOS_RENDER_SHADER_PARAMS_H

// CPU side of the uniform blocks declared in shaders/include/*.hglsl.
// Keep both in sync: scripts/build_shaders.py checks the shader side against
// the offsets listed there, and the static_asserts below check this side.

#include <stddef.h>

namespace gos_render {

// GL uniform buffer binding points (SDL GPU: uniform slot in brackets)
enum UniformSlot {
    kLightDataSlot = 0, // LightsData   [1]  shaders/include/lighting.hglsl
    kSceneDataSlot = 1, // SceneData    [2]  shaders/include/scene.hglsl
    kPerDrawSlot = 2,   // PerDraw      [0]  shaders/include/gos_common.hglsl
};

static const char* const kPerDrawBlockName = "PerDraw";

// std140, matrices row major (the order GameOS keeps them in)
struct PerDraw {
    float mvp[16];
    float projection_[16];
    float world_[16];
    float view_[16];
    float wvp_[16];
    float vp[4];
    float light_offset_[4];
    float fog_color[4];
    float Foreground[4];
};

static_assert(offsetof(PerDraw, projection_) == 64, "PerDraw layout");
static_assert(offsetof(PerDraw, wvp_) == 256, "PerDraw layout");
static_assert(offsetof(PerDraw, vp) == 320, "PerDraw layout");
static_assert(offsetof(PerDraw, Foreground) == 368, "PerDraw layout");
static_assert(sizeof(PerDraw) == 384, "PerDraw layout");

} // namespace gos_render

#endif
