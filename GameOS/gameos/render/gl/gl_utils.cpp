// SPDX-License-Identifier: GPL-3.0-only
#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif
#include <cassert>
#include <cstring>
#include <GL/glew.h>
#include "render/gl/shader_builder.h"
#include "render/gl/gl_utils.h"
#include "utils/vec.h"
#include "render/gl/gl_render_constants.h"
#include "utils/Image.h"

// maybe just 
// header: extern uint32_t TF_R8
// cpp: TF_R8 = GL_R8 
// ?
//
const GLint textureFormats[TF_COUNT] = {
    0,
    GL_RED,
    GL_RG,
    GL_RGB,
    GL_RGBA,

    GL_R,
    GL_RG,
    GL_RGB,
    GL_RGBA,
};

const GLint textureInternalFormats[TF_COUNT] = {
    0,
    GL_R8,
    GL_RG8,
    GL_RGB8,
    GL_RGBA8,

    GL_R32F,
    GL_RG32F,
    GL_RGB32F,
    GL_RGBA32F,
};


const int textureFormatNumChannels[TF_COUNT] = {
    0,
    1,
    2,
    3,
    4,
    1,
    2,
    3,
    4
};

const GLint textureFormatChannelType[TF_COUNT] = {
    0,
    GL_UNSIGNED_BYTE,
    GL_UNSIGNED_BYTE,
    GL_UNSIGNED_BYTE,
    GL_UNSIGNED_BYTE,
    GL_FLOAT,
    GL_FLOAT,
    GL_FLOAT,
    GL_FLOAT,
};

const static uint32_t textureFormatChannelSize[TF_COUNT] = {
    0,
    1,
    1,
    1,
    1,
    4,
    4,
    4,
    4,
};

const GLuint textureType[TT_COUNT] = {
    0,
    GL_TEXTURE_1D,
    GL_TEXTURE_1D_ARRAY,
    GL_TEXTURE_2D,
    GL_TEXTURE_2D_ARRAY,
    GL_TEXTURE_2D_MULTISAMPLE,
    GL_TEXTURE_2D_MULTISAMPLE_ARRAY,
    GL_TEXTURE_3D,
    GL_TEXTURE_CUBE_MAP,
    GL_TEXTURE_CUBE_MAP_ARRAY,
};

GLuint getTexType(TexType tt) {
    assert(tt > TT_NONE && tt < TT_COUNT);
    return textureType[tt];
}

const GLint textureAddressMode[TAM_COUNT] = {
    0,
    GL_CLAMP_TO_EDGE,
    GL_REPEAT
};

GLint getTextureAddressMode(TexAddressMode address_mode) {
    assert(address_mode > TAM_NONE && address_mode < TAM_COUNT);
    return textureAddressMode[address_mode];
}

const GLint textureFilterMode[TFM_COUNT] = {
    0,
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST_MIPMAP_NEAREST,
    GL_NEAREST_MIPMAP_LINEAR,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_LINEAR,
};

GLint getTextureFilterMode(TexFilterMode filter_mode) {
    assert(filter_mode > TFM_NONE && filter_mode < TFM_COUNT);
    return textureFilterMode[filter_mode];
}

void destroyTexture(Texture* tex)
{
    assert(tex);

    if(tex->isValid())
        glDeleteTextures(1, &tex->id);
}


Texture create2DTexture(int w, int h, TexFormat fmt, const uint8_t* texdata)
{
	GLuint texID;
	glGenTextures(1, &texID);
	glBindTexture(GL_TEXTURE_2D, texID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, textureInternalFormats[fmt], 
            w, h, 0, textureFormats[fmt], textureFormatChannelType[fmt], texdata);
    CHECK_GL_ERROR

	//glGenerateMipmap(GL_TEXTURE_2D);

	Texture t;
	t.id = texID;
	t.w = w;
	t.h = h;
	t.fmt_ = fmt;
    t.type_ = TT_2D;
    t.format = (GLenum)-1;

	return t;
}

void updateTexture(const Texture& t, void* pdata, TexFormat pdata_format/*= TF_COUNT*/) {

	glBindTexture(GL_TEXTURE_2D, t.id);
    assert(t.fmt_ != TF_NONE && "t.format is deprecated");
    if(t.fmt_ != TF_NONE) {
        // TODO: keep this all in platform dependent data of texture
        //GLint int_fmt = textureInternalFormats[t.fmt_];
        GLenum fmt = (pdata_format == TF_COUNT) ? textureFormats[t.fmt_] : textureFormats[pdata_format];
        GLenum ch_type = textureFormatChannelType[t.fmt_];
	    //glTexImage2D(GL_TEXTURE_2D, 0, int_fmt, t.w, t.h, 0, fmt, ch_type, pdata);
        
	    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pdata);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t.w, t.h, fmt, ch_type, pdata);
		CHECK_GL_ERROR
		//glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        // deprecated
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, t.format, GL_UNSIGNED_BYTE, pdata);
        CHECK_GL_ERROR
    }
	glBindTexture(GL_TEXTURE_2D, 0);
}

void setSamplerParams(TexType tt, TexAddressMode address_mode, TexFilterMode filter) {

    GLuint tex_type = getTexType(tt);

    GLint tam = getTextureAddressMode(address_mode);
    GLint tfm = getTextureFilterMode(filter);

	glTexParameteri(tex_type, GL_TEXTURE_WRAP_S, tam);
	glTexParameteri(tex_type, GL_TEXTURE_WRAP_T, tam);
	glTexParameteri(tex_type, GL_TEXTURE_WRAP_R, tam);

	glTexParameteri(tex_type, GL_TEXTURE_MAG_FILTER, tfm);
	glTexParameteri(tex_type, GL_TEXTURE_MIN_FILTER, tfm);

}

void getTextureData(const Texture& t, int lod, unsigned char* poutdata, TexFormat format/*= TF_COUNT*/) {

    glBindTexture(GL_TEXTURE_2D, t.id);
    GLenum fmt = (format == TF_COUNT) ? textureFormats[t.fmt_] : textureFormats[format];
    GLenum ch_type = textureFormatChannelType[t.fmt_];
    glGetTexImage(GL_TEXTURE_2D, lod, fmt, ch_type, poutdata);
    CHECK_GL_ERROR
    glBindTexture(GL_TEXTURE_2D, 0);
}

GLuint makeBuffer(GLenum target, const GLvoid* buffer_data, GLsizei buffer_size, GLenum type)
{
	GLuint buffer;
	glGenBuffers(1, &buffer);
    CHECK_GL_ERROR
	glBindBuffer(target, buffer);
    CHECK_GL_ERROR
	glBufferData(target, buffer_size, buffer_data, type);
    CHECK_GL_ERROR
	glBindBuffer(target, 0);
    CHECK_GL_ERROR
	return buffer;
}

void updateBuffer(GLuint buf, GLenum target, const GLvoid* buffer_data, GLsizei buffer_size)
{
	assert(buf && buffer_data);
	glBindBuffer(target, buf);
    CHECK_GL_ERROR
	glBufferSubData(target, 0, buffer_size, buffer_data);
    CHECK_GL_ERROR
	glBindBuffer(target, 0);
    CHECK_GL_ERROR
}


void updateBuffer(GLuint buf, GLenum target, const GLvoid* buffer_data, GLsizei buffer_size, GLenum type)
{
	assert(buf && buffer_data);
	glBindBuffer(target, buf);
    CHECK_GL_ERROR
	glBufferData(target, buffer_size, buffer_data, type);
    CHECK_GL_ERROR
	glBindBuffer(target, 0);
    CHECK_GL_ERROR
}



///////////////////////////////////////////////////////////////////////////////
// draw a textured cube with GL_TRIANGLES
///////////////////////////////////////////////////////////////////////////////
