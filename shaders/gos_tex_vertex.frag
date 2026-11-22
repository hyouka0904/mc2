#include <include/gos_common.hglsl>

#define PREC highp

layout(location = 0) in PREC vec4 Color;
layout(location = 1) in PREC float FogValue;
layout(location = 2) in PREC vec2 Texcoord;

layout (location=0) out PREC vec4 FragColor;

GOS_SAMPLER(0) uniform sampler2D tex1;

void main(void)
{
    PREC vec4 c = Color.bgra;
    PREC vec4 tex_color = texture(tex1, Texcoord);
    c *= tex_color;
    
#ifdef ALPHA_TEST
    // The original renderer only rejects fully transparent texels (alpha
    // reference 0); a 0.5 cut-off dropped partly transparent UI parts such
    // as the compass.
    if(tex_color.a <= 0.0)
        discard;
#endif

	if(fog_color.x>0.0 || fog_color.y>0.0 || fog_color.z>0.0 || fog_color.w>0.0)
    	c.rgb = mix(fog_color.rgb, c.rgb, FogValue);
	FragColor = c;
}
