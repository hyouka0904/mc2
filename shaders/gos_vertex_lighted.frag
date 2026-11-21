#include <include/gos_common.hglsl>

#define PREC highp

layout(location = 0) in PREC vec4 Color;
layout(location = 1) in PREC float FogValue;
layout(location = 2) in PREC vec2 Texcoord;

layout (location=0) out PREC vec4 FragColor;

#ifdef ENABLE_TEXTURE1
GOS_SAMPLER(0) uniform sampler2D tex1;
#endif

void main(void)
{
    PREC vec4 c = Color;
#ifdef ENABLE_TEXTURE1
    c *= texture(tex1, Texcoord);
#endif
	if(fog_color.x>0.0 || fog_color.y>0.0 || fog_color.z>0.0 || fog_color.w>0.0)
		c.rgb = mix(fog_color.rgb, c.rgb, FogValue);
    FragColor = c;
}
