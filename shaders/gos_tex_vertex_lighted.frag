#include <include/gos_common.hglsl>

#define PREC highp

// Same pixel stage as gos_tex_vertex (the CPU path of TG shapes): texture
// times the vertex colour, then gos_State_Fog.
layout(location = 0) in PREC vec4 Color;
layout(location = 1) in PREC float FogValue;
layout(location = 2) in PREC vec2 Texcoord;

layout (location=0) out PREC vec4 FragColor;

GOS_SAMPLER(0) uniform sampler2D tex1;

void main(void)
{
    PREC vec4 tex_color = texture(tex1, Texcoord);
    PREC vec4 c = Color * tex_color;

#ifdef ALPHA_TEST
    if(tex_color.a < 0.5)
        discard;
#endif

    if(fog_color.x>0.0 || fog_color.y>0.0 || fog_color.z>0.0 || fog_color.w>0.0)
        c.rgb = mix(fog_color.rgb, c.rgb, FogValue);
    FragColor = c;
}
