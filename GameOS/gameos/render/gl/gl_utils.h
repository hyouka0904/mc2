#ifndef __GL_UTILS_H__
#define __GL_UTILS_H__

#include <cassert>
#include <cstdio>
#include "render/gl/shader_builder.h"
#include "render/render_types.h"
#include "utils/vec.h"

#define BUFFER_OFFSET(bytes) ((GLubyte*) NULL + (bytes))



#define CHECK_GL_ERROR \
{ \
   	GLenum err = glGetError();\
	while(err != GL_NO_ERROR) \
	{ \
		printf("OpenGL Error: %s\n", ogl_get_error_code_str(err)); \
		printf("Location : %s : %d\n", __FILE__ , __LINE__); \
   	    err = glGetError();\
	}\
}

template<typename T>
const char* ogl_get_error_code_str(T input)
{
    int errorCode = (int)input;
    switch(errorCode)
    {
	case GL_NO_ERROR:
            return "GL_NO_ERROR";
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";               
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";           
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";      
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";                    
        case GL_STACK_OVERFLOW:
            return "GL_STACK_OVERFLOW";                 
        case GL_STACK_UNDERFLOW:
            return "GL_STACK_UNDERFLOW";        
        default:
            return "unknown error code";
    }
}


template<typename T>
static int ogl_check_val(T input, T reference, const char* message)
{
    if(input==reference)
    {
        return true;
    }
    else
    {
	printf("OpenGL Error: %s Error code: %s\n", message, ogl_get_error_code_str(input));
        return false;
    }
}

Texture create2DTexture(int w, int h, TexFormat fmt, const uint8_t* texdata);
// texture has to be binded before calling this
void setSamplerParams(TexType tt, TexAddressMode address_mode, TexFilterMode filter);

// pdata_format - specifies format of data provided in pdata
void updateTexture(const Texture& t, void* pdata, TexFormat pdata_format = TF_COUNT);
void destroyTexture(Texture* tex);
// fmt - desired format of returned data
void getTextureData(const Texture& t, int lod, unsigned char* poutdata, TexFormat fmt = TF_COUNT);
GLuint makeBuffer(GLenum target, const GLvoid* buffer_data, GLsizei buffer_size, GLenum type = GL_STATIC_DRAW);
void updateBuffer(GLuint buf, GLenum target, const GLvoid* buffer_data, GLsizei buffer_size);
void updateBuffer(GLuint buf, GLenum target, const GLvoid* buffer_data, GLsizei buffer_size, GLenum type);

#endif // __GL_UTILS_H__
