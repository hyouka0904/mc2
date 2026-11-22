#include <include/lighting.hglsl>

// TG shape vertex (TG_HWTypeVertex, mclib/tgl.h) in shape space
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 aRGBLight;
layout(location = 3) in vec2 texcoord;

// wvp_ (shape -> clip before the viewport), world_, projection_ and vp are
// in the PerDraw block of gos_common.hglsl.

layout(location = 0) out vec4 Color;
layout(location = 1) out float FogValue;
layout(location = 2) out vec2 Texcoord;

void main(void)
{
    // same as MultiTransformShape: perspective divide and viewport on the
    // "CPU" side, then projection_ maps screen coordinates to clip space
    vec4 p = wvp_ * vec4(pos.xyz, 1);
    float rhw = 1 / p.w;

    p.x = (p.x * rhw) * vp.z + vp.x;
    p.y = (p.y * rhw) * vp.w + vp.y;
    p.z = p.z * rhw;
    p.w = abs(rhw);

    vec4 p2 = projection_ * vec4(p.xyz, 1);
    gl_Position = gos_clip(p2 / p.w);

    vec3 wpos = (world_ * vec4(pos.xyz, 1.0)).xyz;
    vec3 wnormal = (world_ * vec4(normal, 0.0)).xyz;

    Color = vec4(gos_vertex_light(aRGBLight.bgra, wpos, wnormal), 1.0);
    FogValue = gos_vertex_fog(wpos);
    Texcoord = texcoord;
}
