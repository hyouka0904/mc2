#include <include/lighting.hglsl>

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 aRGBLight;
layout(location = 3) in vec2 texcoord;

// Parameters (wvp_, world_, projection_, vp, light_offset_...) are in the
// PerDraw block of gos_common.hglsl.

layout(location = 0) out vec3 Normal;
layout(location = 1) out float FogValue;
layout(location = 2) out vec2 Texcoord;
layout(location = 3) out vec4 VertexColor;
layout(location = 4) out vec3 VertexLight;
layout(location = 5) out vec3 WorldPos;
layout(location = 6) out vec3 CameraPos;

void main(void)
{
    vec4 p = wvp_ * vec4(pos.xyz, 1);
    float rhw = 1 / p.w;

    p.x = (p.x * rhw) * vp.z + vp.x;
    p.y = (p.y * rhw) * vp.w + vp.y;
    p.z = p.z * rhw;
    p.w = abs(rhw);

    WorldPos = (world_ * vec4(pos.xyz, 1.0)).xyz;

    // something is wrong with this: check later
    //CameraPos = (inverse(view_) * vec4(0,0,0,1)).xyz;

    vec4 p2 = projection_ * vec4(p.xyz,1);

    //mat4 norm_view_mat = inverse(view_);

    gl_Position = gos_clip(p2 / p.w);
    //Normal = ((world_ * view_) * vec4(normal, 0)).xyz;
    Normal = (world_ * vec4(normal, 0)).xyz;
    //FogValue = fog.w;
    Texcoord = texcoord;

    // base light can be only calculated in VS because relies on exact vertex colors
    // TODO: pass correct parameters here
    vec3 base_light = get_base_light(aRGBLight.bgra, false, 0.0, false, false,
            vec3(0.0), vec3(0.0), vec3(0.0));

#if ENABLE_VERTEX_LIGHTING
    const int lights_index = int(light_offset_.x);
    VertexLight = calc_light(lights_index, Normal, base_light);
#else
    VertexLight = base_light;
#endif

    VertexColor = aRGBLight.bgra;
    FogValue = 0.0;
    CameraPos = vec3(0.0); // unused: fog uses g_scene.cameraPos
}

