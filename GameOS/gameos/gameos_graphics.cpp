#include <vector>
#include <map>
#include <algorithm>
#include <string>

#include "gameos.hpp"
#include "font3d.hpp"
#include "gos_font.h"

#ifdef LINUX_BUILD
#include <cstdarg>
#endif

#include "platform_stdlib.h"
#include "platform_str.h"

#include "render/render_device.h"
#include "utils/Image.h"
#include "utils/vec.h"
#include "utils/string_utils.h"
#include "utils/timing.h"
#include "gos_render.h"

class gosRenderer;
class gosFont;

static const DWORD INVALID_TEXTURE_ID = 0;

static gosRenderer* g_gos_renderer = NULL;

void gos_FlushDrawBatch(); // defined after gosRenderer
gosRenderer* getGosRenderer() {
    return g_gos_renderer;
}

struct gosTextureInfo {
    int width_;
    int height_;
    gos_TextureFormat format_;
};

////////////////////////////////////////////////////////////////////////////////
class gosBuffer {
	friend class gosRenderer;
public:
	gos_render::BufferHandle buffer_;
	int element_size_;
	uint32_t count_;
	gosBUFFER_TYPE type_;
	gosBUFFER_USAGE usage_;
};

static gos_render::AttribType toAttribType(gosVERTEX_ATTRIB_TYPE type) {
	switch (type)
	{
	case gosVERTEX_ATTRIB_TYPE::BYTE: return gos_render::AttribType::Byte;
	case gosVERTEX_ATTRIB_TYPE::UNSIGNED_BYTE: return gos_render::AttribType::UByte;
	case gosVERTEX_ATTRIB_TYPE::SHORT: return gos_render::AttribType::Short;
	case gosVERTEX_ATTRIB_TYPE::UNSIGNED_SHORT: return gos_render::AttribType::UShort;
	case gosVERTEX_ATTRIB_TYPE::INT: return gos_render::AttribType::Int;
	case gosVERTEX_ATTRIB_TYPE::UNSIGNED_INT: return gos_render::AttribType::UInt;
	case gosVERTEX_ATTRIB_TYPE::FLOAT: return gos_render::AttribType::Float;
	default:
		gosASSERT(0 && "unknown vertex attrib type");
	}
	return gos_render::AttribType::Float;
}

////////////////////////////////////////////////////////////////////////////////
class gosVertexDeclaration {
	friend class gosRenderer;

	gosVERTEX_FORMAT_RECORD* vf_;
	uint32_t count_;

	gosVertexDeclaration() :vf_(0), count_(0) {}
public:

	static gosVertexDeclaration* create(gosVERTEX_FORMAT_RECORD* vf, int count)
	{
		gosVertexDeclaration* vdecl = new gosVertexDeclaration();
		if (!vdecl)
			return nullptr;

		vdecl->vf_ = new gosVERTEX_FORMAT_RECORD[count];
		memcpy(vdecl->vf_, vf, count * sizeof(gosVERTEX_FORMAT_RECORD));
		vdecl->count_ = count;
		vdecl->buildAttribs();

		return vdecl;
	}

	static void destroy(gosVertexDeclaration* vdecl)
	{
		delete[] vdecl->vf_;
		vdecl->count_ = -1;
		vdecl->vf_ = nullptr;
		delete vdecl;
	}

	// Attribute layout handed to the render device with each draw
	const gos_render::VertexAttrib* attribs() const { return attribs_.data(); }
	int numAttribs() const { return (int)attribs_.size(); }

private:
	void buildAttribs() {
		attribs_.resize(count_);
		for (uint32_t i = 0; i < count_; ++i) {
			const gosVERTEX_FORMAT_RECORD& rec = vf_[i];
			gos_render::VertexAttrib& a = attribs_[i];
			a.location = (int)rec.index;
			a.components = rec.num_components;
			a.type = toAttribType(rec.type);
			a.normalized = rec.normalized;
			a.stride = rec.stride;
			a.offset = rec.offset;
		}
	}

	std::vector<gos_render::VertexAttrib> attribs_;
};

class gosMaterialVariationHelper;
class gosMaterialVariation {
        friend class gosMaterialVariationHelper;
        char* defines_;
        char* unique_name_suffix_;

    public:
        gosMaterialVariation():defines_(nullptr), unique_name_suffix_(nullptr) {}
        const char* getDefinesString() const { return defines_; }
        const char* getUniqueSuffix() const { return unique_name_suffix_; }

        ~gosMaterialVariation()
        {
            delete[] defines_;
            delete[] unique_name_suffix_;
        }
};

class gosMaterialVariationHelper {
        std::vector<std::string> defines;
    public:

        void addDefine(const char* define)
        {
            defines.push_back(std::string(define));
        }

        void addDefines(const char** define)
        {
            gosASSERT(define);
            while(*define)
            {
                defines.push_back(std::string(*define));
                define++;
            }
        }

        void addDefines(const std::vector<std::string>& define)
        {
            defines.insert(defines.end(), define.begin(), define.end());
        }

        void getMaterialVariation(gosMaterialVariation& variation)
        {
            std::string defines_str = "#version 420\n";
            std::string unique_suffix_str = "#";
            for(auto d : defines)
            {
                defines_str.append("#define ");
                defines_str.append(d);
                defines_str.append(" = 1\n");

                unique_suffix_str.append(d);
                unique_suffix_str.append("#");
            }
            defines_str.append("\n");

            if(variation.defines_)
                delete[] variation.defines_;

            if(variation.unique_name_suffix_)
                delete[] variation.unique_name_suffix_;

            size_t size = defines_str.size() + 1;
            variation.defines_ = new char[size];
            memcpy(variation.defines_, defines_str.c_str(), size);
            variation.defines_[size-1]='\0';

            size = unique_suffix_str.size() + 1;
            variation.unique_name_suffix_ = new char[size];
            memcpy(variation.unique_name_suffix_, unique_suffix_str.c_str(), size);
            variation.unique_name_suffix_[size-1]='\0';
        }
};

enum class gosGLOBAL_SHADER_FLAGS : unsigned int
{
    ALPHA_TEST = 0
    // etc.
};

#define SHADER_FLAG_INDEX_TO_MASK(x) (1 << ((uint32_t)x))
#define SHADER_FLAG_MASK_TO_INDEX(x) (ffs(x)-1) // use __popcnt etc for windows, and move to platform_*.h

static const char* const g_shader_flags[] = {
    "ALPHA_TEST"
};


class gosRenderMaterial {

		static const std::string s_mvp;
		static const std::string s_fog_color;
    public:
        static gosRenderMaterial* load(const char* shader, const gosMaterialVariation& mvar) {
            gosASSERT(shader);
            gosRenderMaterial* pmat = new gosRenderMaterial();
            char vs[256];
            char ps[256];
            StringFormat(vs, 255, "shaders/%s.vert", shader);
            StringFormat(ps, 255, "shaders/%s.frag", shader);

            std::string sh_name = shader;
            sh_name.append(mvar.getUniqueSuffix());

            pmat->program_ = gos_render::device()->createShader(sh_name.c_str(), vs, ps, mvar.getDefinesString());
            if(!pmat->program_) {
                SPEW(("SHADERS", "Failed to create %s material\n", shader));
                delete pmat;
                return NULL;
            }
            
            pmat->name_ = new char[strlen(shader) + 1];
            strcpy(pmat->name_, shader);
            pmat->variant_name_ = sh_name;

            pmat->onLoad();

            return pmat;
        }


        void onLoad() {
            gosASSERT(program_);

            // gos_VERTEX: float x, y, z, rhw; DWORD argb; DWORD frgb; float u, v
            const int stride = sizeof(gos_VERTEX);
            const int pos = program_->attribLocation("pos");
            gosASSERT(pos >= 0);
            vertex_attribs_[0] = { pos, 4, gos_render::AttribType::Float, false, stride, 0 };
            vertex_attribs_[1] = { program_->attribLocation("color"), 4, gos_render::AttribType::UByte, true, stride, (int)(4*sizeof(float)) };
            vertex_attribs_[2] = { program_->attribLocation("fog"), 4, gos_render::AttribType::UByte, true, stride, (int)(4*sizeof(float) + sizeof(uint32_t)) };
            vertex_attribs_[3] = { program_->attribLocation("texcoord"), 2, gos_render::AttribType::Float, false, stride, (int)(4*sizeof(float) + 2*sizeof(uint32_t)) };
        }

        // gos_VERTEX layout for this shader's attribute locations
        const gos_render::VertexAttrib* vertexAttribs() const { return vertex_attribs_; }
        static const int NUM_VERTEX_ATTRIBS = 4;

        static void destroy(gosRenderMaterial* pmat) {
            gosASSERT(pmat);
            if(pmat->program_) {
                gos_render::device()->destroyShader(pmat->variant_name_.c_str());
                pmat->program_ = 0;
            }

            delete[] pmat->name_;
            pmat->name_ = 0;
            delete pmat;
        }

        void checkReload()
        {
            if(program_) {
                if(program_->needsReload()) {
                    if(program_->reload())
                        onLoad();
                }
            }
        }

        bool setSamplerUnit(const std::string& sampler_name, uint32_t unit) {
            gosASSERT(!sampler_name.empty());
            return program_->setSampler(sampler_name, (int)unit);
        }

        bool setUniformBlock(const std::string& uniform_block_name, uint32_t unit) {
            gosASSERT(!uniform_block_name.empty());
            return program_->setUniformBlock(uniform_block_name, (int)unit);
        }

        bool setTransform(const mat4& m) {
            program_->setMat4(s_mvp, m);
            return true;
        }

		bool setFogColor(const vec4& fog_color) {
            program_->setFloat4(s_fog_color, fog_color);
            return true;
		}

        void apply() {
            gosASSERT(program_);
            program_->apply();
        }

        const char* getName() const { return name_; }

        gos_render::Shader* getShader() { return program_; }

		void end() {
            gos_render::device()->endShader();
        }

    private:
        gosRenderMaterial():
            program_(NULL)
            , name_(NULL)
        {
        }

        gos_render::Shader* program_;
        char* name_;
        std::string variant_name_; // name the shader was created under
        gos_render::VertexAttrib vertex_attribs_[NUM_VERTEX_ATTRIBS];
};

const std::string gosRenderMaterial::s_mvp = std::string("mvp");
const std::string gosRenderMaterial::s_fog_color = std::string("fog_color");

class gosMesh {
    public:
        typedef WORD INDEX_TYPE;

        static gosMesh* makeMesh(gosPRIMITIVETYPE prim_type, int vertex_capacity, int index_capacity = 0) {
            using namespace gos_render;
            BufferHandle vb = device()->createBuffer(BufferKind::Vertex, BufferUsage::Dynamic, 0, sizeof(gos_VERTEX)*vertex_capacity);
            if(!vb)
                return NULL;

            BufferHandle ib = 0;
            if(index_capacity > 0) {
                ib = device()->createBuffer(BufferKind::Index, BufferUsage::Dynamic, 0, sizeof(INDEX_TYPE)*index_capacity);
                if(!ib)
                    return NULL;
            }

            gosMesh* mesh = new gosMesh(prim_type, vertex_capacity, index_capacity);
            mesh->vb_ = vb;
            mesh->ib_ = ib;
            mesh->pvertex_data_ = new gos_VERTEX[vertex_capacity];
            mesh->pindex_data_ = new INDEX_TYPE[index_capacity];
            return mesh;
        }

        static void destroy(gosMesh* pmesh) {

            gosASSERT(pmesh);

            delete[] pmesh->pvertex_data_;
            delete[] pmesh->pindex_data_;

            gos_render::device()->destroyBuffer(pmesh->vb_);
            gos_render::device()->destroyBuffer(pmesh->ib_);
            delete pmesh;
        }

        bool addVertices(gos_VERTEX* vertices, int count) {
            if(num_vertices_ + count <= vertex_capacity_) {
                memcpy(pvertex_data_ + num_vertices_, vertices, sizeof(gos_VERTEX)*count);
                num_vertices_ += count;
                return true;
            }
            return false;
        }

        bool addIndices(INDEX_TYPE* indices, int count) {
            if(num_indices_ + count <= index_capacity_) {
                memcpy(pindex_data_ + num_indices_, indices, sizeof(INDEX_TYPE)*count);
                num_indices_ += count;
                return true;
            }
            return false;
        }

        int getVertexCapacity() const { return vertex_capacity_; }
        int getIndexCapacity() const { return index_capacity_; }
        int getNumVertices() const { return num_vertices_; }
        int getNumIndices() const { return num_indices_; }
        const gos_VERTEX* getVertices() const { return pvertex_data_; }
        const WORD* getIndices() const { return pindex_data_; }

        int getIndexSizeBytes() const { return sizeof(INDEX_TYPE); }

        void rewind() { num_vertices_ = 0; num_indices_ = 0; }

        void draw(gosRenderMaterial* material) const;
        void drawIndexed(gosRenderMaterial* material) const;

		static void drawIndexed(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl, gosRenderMaterial* material);
		static void drawIndexed(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl);

		static const std::string s_tex1;

    private:

        gosMesh(gosPRIMITIVETYPE prim_type, int vertex_capacity, int index_capacity)
            : vertex_capacity_(vertex_capacity)
            , index_capacity_(index_capacity)
            , num_vertices_(0)
            , num_indices_(0)
            , pvertex_data_(NULL)    
            , pindex_data_(NULL)    
            , prim_type_(prim_type)
            , vb_(0)
            , ib_(0)
         {
         }

        int vertex_capacity_;
        int index_capacity_;
        int num_vertices_;
        int num_indices_;
        gos_VERTEX* pvertex_data_;
        INDEX_TYPE* pindex_data_;
        gosPRIMITIVETYPE prim_type_;

        gos_render::BufferHandle vb_;
        gos_render::BufferHandle ib_;
};

static gos_render::Primitive toPrimitive(gosPRIMITIVETYPE pt) {
    switch(pt) {
        case PRIMITIVE_POINTLIST: return gos_render::Primitive::Points;
        case PRIMITIVE_LINELIST: return gos_render::Primitive::Lines;
        case PRIMITIVE_TRIANGLELIST: return gos_render::Primitive::Triangles;
        default: gosASSERT(0 && "Wrong primitive type");
    }
    return gos_render::Primitive::Triangles;
}

const std::string gosMesh::s_tex1 = std::string("tex1");

void gosMesh::draw(gosRenderMaterial* material) const
{
    gosASSERT(material);

    if(num_vertices_ == 0)
        return;

    using namespace gos_render;
    device()->updateBuffer(vb_, BufferKind::Vertex, pvertex_data_, num_vertices_*sizeof(gos_VERTEX));

    material->apply();
    material->setSamplerUnit(s_tex1, 0);

    device()->draw(toPrimitive(prim_type_), vb_, material->vertexAttribs(),
                   gosRenderMaterial::NUM_VERTEX_ATTRIBS, 0, num_vertices_);

    material->end();
}

void gosMesh::drawIndexed(gosRenderMaterial* material) const
{
    gosASSERT(material);

    if(num_vertices_ == 0)
        return;

    using namespace gos_render;
    device()->updateBuffer(vb_, BufferKind::Vertex, pvertex_data_, num_vertices_*sizeof(gos_VERTEX));
    device()->updateBuffer(ib_, BufferKind::Index, pindex_data_, num_indices_*sizeof(INDEX_TYPE));

    material->apply();
    material->setSamplerUnit(s_tex1, 0);

    device()->drawIndexed(toPrimitive(prim_type_), vb_, ib_, getIndexSizeBytes(), material->vertexAttribs(),
                          gosRenderMaterial::NUM_VERTEX_ATTRIBS, num_indices_);

    material->end();
}

void gosMesh::drawIndexed(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl, gosRenderMaterial* material)
{
	gosASSERT(material);

	int index_size = ib->element_size_;
	gosASSERT(index_size == 2 || index_size == 4);

	if (ib->count_ == 0)
		return;

	material->apply();
	material->setSamplerUnit(s_tex1, 0);

	gos_render::device()->drawIndexed(gos_render::Primitive::Triangles, vb->buffer_, ib->buffer_, index_size,
	                                  vdecl->attribs(), vdecl->numAttribs(), ib->count_);

	material->end();
}

void gosMesh::drawIndexed(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl)
{
	int index_size = ib->element_size_;
	gosASSERT(index_size == 2 || index_size == 4);

	if (ib->count_ == 0)
		return;

	// uses the material the caller applied (gos_ApplyRenderMaterial)
	gos_render::device()->drawIndexed(gos_render::Primitive::Triangles, vb->buffer_, ib->buffer_, index_size,
	                                  vdecl->attribs(), vdecl->numAttribs(), ib->count_);

	gos_render::device()->endShader();
}



class gosTexture {
    public:
        gosTexture(gos_TextureFormat fmt, const char* fname, DWORD hints, BYTE* pdata, DWORD size, bool from_memory)
        {

	        //if(fmt == gos_Texture_Detect || /*fmt == gos_Texture_Keyed ||*/ fmt == gos_Texture_Bump || fmt == gos_Texture_Normal)
            //     PAUSE((""));

            format_ = fmt;
            if(fname) {
                filename_ = new char[strlen(fname)+1];
                strcpy(filename_, fname);
            } else {
                filename_ = 0;
            }
            texname_ = NULL;

            hints_ = hints;

            plocked_area_ = NULL;

            size_ = 0;
            pcompdata_ = NULL;
            if(size) {
                size_ = size;
                pcompdata_ = new BYTE[size];
                memcpy(pcompdata_, pdata, size);
            }

            is_locked_ = false;
            is_from_memory_ = from_memory;
        }

        gosTexture(gos_TextureFormat fmt, DWORD hints, DWORD w, DWORD h, const char* texname)
        {
	        //if(fmt == gos_Texture_Detect /*|| fmt == gos_Texture_Keyed*/ || fmt == gos_Texture_Bump || fmt == gos_Texture_Normal)
            //     PAUSE((""));

            format_ = fmt;
            if(texname) {
                texname_ = new char[strlen(texname)+1];
                strcpy(texname_, texname);
            } else {
                texname_ = 0;
            }
            filename_ = NULL;
            hints_ = hints;

            plocked_area_ = NULL;

            size_ = 0;
            pcompdata_ = NULL;
            tex_.w = w;
            tex_.h = h;

            is_locked_ = false;
            is_from_memory_ = true;
        }

        bool createHardwareTexture();

        ~gosTexture() {

            //SPEW(("Destroying texture: %s\n", filename_));

            gosASSERT(is_locked_ == false);

            if(pcompdata_)
                delete[] pcompdata_;
            if(filename_)
                delete[] filename_;
            if(texname_)
                delete[] texname_;

            gos_render::device()->destroyTexture(&tex_);
        }

        uint32_t getTextureId() const { return tex_.id; }
        const Texture& getTexture() const { return tex_; }

        // Lock/Unlock exchange BGRA pixels (D3D order); the backends store
        // RGBA. A CPU copy of the pixels (shadow_) means locking never waits
        // for the GPU: it is filled when the texture is created from memory,
        // or by a single readback the first time a file texture is locked,
        // and kept up to date by Unlock().
        BYTE* Lock(int mipl_level, bool is_read_only, int* pitch) {
            (void)mipl_level;
            gosASSERT(is_locked_ == false);
            is_locked_ = true;
            // TEXTUREPTR::Pitch is measured in DWORDs: tightly packed rows
            gosASSERT(pitch);
            *pitch = tex_.w;

            gosASSERT(!plocked_area_);
            lock_type_read_only_ = is_read_only;
            const size_t num_pixels = (size_t)tex_.w * tex_.h;
            if(shadow_.empty()) {
                shadow_.resize(num_pixels * 4);
                gos_render::device()->readTexture(tex_, shadow_.data(), TF_RGBA8);
                ++s_shadow_readbacks;
            }
            plocked_area_ = new BYTE[num_pixels * 4];
            const DWORD* src = (const DWORD*)shadow_.data();
            DWORD* dst = (DWORD*)plocked_area_;
            for(size_t i = 0; i < num_pixels; ++i)
                dst[i] = swapRedBlue(src[i]);
            return plocked_area_;
        }

        void Unlock() {
            gosASSERT(is_locked_ == true);

            if(!lock_type_read_only_) {
                const size_t num_pixels = (size_t)tex_.w * tex_.h;
                const DWORD* src = (const DWORD*)plocked_area_;
                DWORD* dst = (DWORD*)shadow_.data();
                for(size_t i = 0; i < num_pixels; ++i)
                    dst[i] = swapRedBlue(src[i]);
                gos_FlushDrawBatch(); // pending draws see the old pixels
                gos_render::device()->updateTexture(tex_, shadow_.data(), TF_RGBA8);
            }

            delete[] plocked_area_;
            plocked_area_ = NULL;

            is_locked_ = false;
        }

        // Number of textures that had to be read back from the GPU once
        static uint32_t s_shadow_readbacks;

        void getTextureInfo(gosTextureInfo* texinfo) const {
            gosASSERT(texinfo);
            texinfo->width_ = tex_.w;
            texinfo->height_ = tex_.h;
            texinfo->format_ = format_;
        }

    private:
        BYTE* pcompdata_;
        BYTE* plocked_area_;
        DWORD size_;
        Texture tex_;

        gos_TextureFormat format_;
        char* filename_;
        char* texname_;
        DWORD hints_;

        bool is_locked_;
        bool lock_type_read_only_;
        bool is_from_memory_; // not loaded from file
        std::vector<uint8_t> shadow_; // RGBA8 copy of the pixels, see Lock()

        // BGRA <-> RGBA (the same swap both ways)
        static DWORD swapRedBlue(DWORD c) {
            return (c & 0xFF00FF00) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF);
        }
};

uint32_t gosTexture::s_shadow_readbacks = 0;

struct gosTextAttribs {
    HGOSFONT3D FontHandle;
    DWORD Foreground;
    float Size;
    bool WordWrap;
    bool Proportional;
    bool Bold;
    bool Italic;
    DWORD WrapType;
    bool DisableEmbeddedCodes;
};

static void makeKindaSolid(Image& img) {
    // have to do this, otherwise texutre with zero alpha could be drawn with alpha blend enabled, evel though logically aplha blend should not be enabled!
    // (happens when drawing terrain, see TerrainQuad::draw() case when no detail and no owerlay bu t isCement is true)
    DWORD* pixels = (DWORD*)img.getPixels();
    for(int y=0;y<img.getHeight(); ++y) {
        for(int x=0;x<img.getWidth(); ++x) {
            DWORD pix = pixels[y*img.getWidth() + x];
            pixels[y*img.getWidth() + x] = pix | 0xff000000;
        }
    }
}

static bool doesLookLikeAlpha(const Image& img) {
    gosASSERT(img.getFormat() == FORMAT_RGBA8);

    DWORD* pixels = (DWORD*)img.getPixels();
    for(int y=0;y<img.getHeight(); ++y) {
        for(int x=0;x<img.getWidth(); ++x) {
            DWORD pix = pixels[y*img.getWidth() + x];
            if((0xFF000000 & pix) != 0xFF000000)
                return true;
        }
    }
    return false;
}

static gos_TextureFormat convertIfNecessary(Image& img, gos_TextureFormat gos_format) {

    const bool has_alpha_channel = FORMAT_RGBA8 == img.getFormat();

    if(gos_format == gos_Texture_Detect) {
        bool has_alpha = has_alpha_channel ? doesLookLikeAlpha(img) : false;
        gos_format = has_alpha ? gos_Texture_Alpha : gos_Texture_Solid;
    }

    if(gos_format == gos_Texture_Solid && has_alpha_channel)
        makeKindaSolid(img);

    return gos_format;
}

bool gosTexture::createHardwareTexture() {

    if(!is_from_memory_) {

        gosASSERT(filename_);

        Image img;
        if(!img.loadFromFile(filename_)) {
            SPEW(("DBG", "failed to load texture from file: %s\n", filename_));
            return false;
        }

        // check for only those formats, because lock.unlock may incorrectly work with different channes size (e.g. 16 or 32bit or floats)
        FORMAT img_fmt = img.getFormat();
        if(img_fmt != FORMAT_RGB8 && img_fmt != FORMAT_RGBA8) {
            STOP(("Unsupported texture format when loading %s\n", filename_));
        }

        TexFormat tf = img_fmt == FORMAT_RGB8 ? TF_RGB8 : TF_RGBA8;

        format_ = convertIfNecessary(img, format_);

        tex_ = gos_render::device()->createTexture2D(img.getWidth(), img.getHeight(), tf, img.getPixels());
        return tex_.isValid();

    } else if(pcompdata_ && size_ > 0) {

        // Texture supplied as an in-memory TGA image.
        Image img;
        if(!img.loadTGA(pcompdata_, size_)) {
            SPEW(("DBG", "failed to load texture from data, filename: %s, texname: %s\n", filename_? filename_ : "NO FILENAME", texname_?texname_:"NO TEXNAME"));
            return false;
        }

        FORMAT img_fmt = img.getFormat();

        if(img_fmt != FORMAT_RGB8 && img_fmt != FORMAT_RGBA8) {
            STOP(("Unsupported texture format when loading %s\n", filename_));
        }

        TexFormat tf = img_fmt == FORMAT_RGB8 ? TF_RGB8 : TF_RGBA8;

        format_ = convertIfNecessary(img, format_);

        tex_ = gos_render::device()->createTexture2D(img.getWidth(), img.getHeight(), tf, img.getPixels());
        return tex_.isValid();
    } else {
        gosASSERT(tex_.w >0 && tex_.h > 0);

        // No source data: create a magenta placeholder of the requested size.
        // Lock()/Unlock() always exchange RGBA8, so the storage format is
        // RGBA8 regardless of format_ (which only describes alpha usage).
        // The pixels are known here, so the CPU copy starts filled and
        // locking this texture never needs a GPU readback.
        TexFormat tf = TF_RGBA8;
        shadow_.resize((size_t)tex_.w * tex_.h * 4);
        DWORD* pdata = (DWORD*)shadow_.data();
        for(int i=0;i<tex_.w*tex_.h;++i)
            pdata[i] = 0xFF00FFFF;
        tex_ = gos_render::device()->createTexture2D(tex_.w, tex_.h, tf, shadow_.data());
        return tex_.isValid();
    }

}

////////////////////////////////////////////////////////////////////////////////
class gosFont {
        friend class gosRenderer;
    public:
        static gosFont* load(const char* fontFile);

        int getMaxCharWidth() const { return gi_.max_advance_; }
        int getMaxCharHeight() const { return gi_.font_line_skip_; }
        int getFontAscent() const { return gi_.font_ascent_; }

        int getCharWidth(int c) const;
        void getCharUV(int c, uint32_t* u, uint32_t* v) const;
        int getCharAdvance(int c) const;
        const gosGlyphMetrics& getGlyphMetrics(int c) const;


        DWORD getTextureId() const { return tex_id_; }
        const char* getName() const { return font_name_; }
        const char* getId() const { return font_id_; }

        uint32_t getRefCount() { return ref_count_; }
        uint32_t addRef() { return ++ref_count_; }
        uint32_t decRef() { gosASSERT(ref_count_>0); return --ref_count_; }

    private:
        static uint32_t destroy(gosFont* font);
        gosFont():font_name_(0), font_id_(0), tex_id_(0), ref_count_(1) {};
        ~gosFont();

        char* font_name_;
        char* font_id_;
        gosGlyphInfo gi_;
        DWORD tex_id_;
        uint32_t ref_count_;
};


////////////////////////////////////////////////////////////////////////////////
class gosRenderer {

    friend class gosShapeRenderer;

    typedef uint32_t RenderState[gos_MaxState];
	static const std::string s_Foreground;

    public:
        gosRenderer(graphics::RenderContextHandle ctx_h, graphics::RenderWindowHandle win_h, int w, int h) {
            width_ = w;
            height_ = h;
            ctx_h_ = ctx_h;
            win_h_ = win_h;
        }

        uint32_t addTexture(gosTexture* texture) {
            gosASSERT(texture);
            // Reuse slots freed by deleteTexture() so ids stay small and the
            // list does not grow with every texture loaded over a session.
            if(!freeTextureSlots_.empty()) {
                const uint32_t id = freeTextureSlots_.back();
                freeTextureSlots_.pop_back();
                gosASSERT(textureList_[id] == 0);
                textureList_[id] = texture;
                return id;
            }
            textureList_.push_back(texture);
            return (uint32_t)(textureList_.size()-1);
        }

        uint32_t addFont(gosFont* font) {
            gosASSERT(font);
            fontList_.push_back(font);
            return (uint32_t)(fontList_.size()-1);
        }

		uint32_t addBuffer(gosBuffer* buffer) {
			gosASSERT(buffer);
			bufferList_.push_back(buffer);
			return (uint32_t)(bufferList_.size() - 1);
		}

		uint32_t addVertexDeclaration(gosVertexDeclaration* vdecl) {
			gosASSERT(vdecl);
			vertexDeclarationList_.push_back(vdecl);
			return (uint32_t)(vertexDeclarationList_.size() - 1);
		}

        // Fonts are looked up by pointer/name, never by the index addFont()
        // returns, so erasing from the list is safe here.
        void deleteFont(gosFont* font) {
            
            struct equals_to {
                gosFont* fnt_;
                bool operator()(gosFont* fnt) {
                    return fnt == fnt_;
                }
            };

            equals_to eq;
            eq.fnt_ = font;

            std::vector<gosFont*>::iterator it = 
                std::find_if(fontList_.begin(), fontList_.end(), eq);
            if(it != fontList_.end())
            {
                gosFont* font = *it;
                if(0 == gosFont::destroy(font))
                    fontList_.erase(it);
            }
        }

		bool deleteBuffer(gosBuffer* buffer) {
			std::vector<gosBuffer*>::iterator it = std::find(bufferList_.begin(), bufferList_.end(), buffer);
			if (it != bufferList_.end())
			{
				bufferList_.erase(it);
				return true;
			}
			return false;
		}

		bool deleteVertexDeclaration(gosVertexDeclaration* vdecl) {
			std::vector<gosVertexDeclaration*>::iterator it = std::find(vertexDeclarationList_.begin(), vertexDeclarationList_.end(), vdecl);
			if (it != vertexDeclarationList_.end())
			{
				vertexDeclarationList_.erase(it);
				return true;
			}
			return false;
		}

        gosFont* findFont(const char* font_id) {
            
            struct equals_to {
                const char* font_id_;
                bool operator()(const gosFont* fnt) {
                    return strcmp(fnt->getId(), font_id_)==0;
                }
            };

            equals_to eq;
            eq.font_id_ = font_id;

            std::vector<gosFont*>::iterator it = 
                std::find_if(fontList_.begin(), fontList_.end(), eq);
            if(it != fontList_.end())
                return *it;
            return NULL;
        }

        gosTexture* getTexture(DWORD texture_id) {
            if(texture_id == INVALID_TEXTURE_ID) {
                gosASSERT(0 && "Should not be requested");
                return NULL;
            }
            gosASSERT(textureList_.size() > texture_id);
            gosASSERT(textureList_[texture_id] != 0);
            return textureList_[texture_id];
        }

        void deleteTexture(DWORD texture_id) {
            flushBatch(); // the pending batch may use it
            gosASSERT(textureList_.size() > texture_id);
            gosASSERT(textureList_[texture_id] != 0);
            delete textureList_[texture_id];
            textureList_[texture_id] = 0;
            freeTextureSlots_.push_back(texture_id);
        }

        uint32_t getFlagsFromStates()
        {
            return curStates_[gos_State_AlphaTest] ? SHADER_FLAG_INDEX_TO_MASK(gosGLOBAL_SHADER_FLAGS::ALPHA_TEST) : 0;
        }

        gosRenderMaterial* getRenderMaterial(const char* name) {

            uint32_t flags = getFlagsFromStates();
            return materialDB_[name][flags];
        }

        gosTextAttribs& getTextAttributes() { return curTextAttribs_; }
        void setTextPos(int x, int y) { curTextPosX_ = x; curTextPosY_ = y; }
        void getTextPos(int& x, int& y) { x = curTextPosX_; y = curTextPosY_; }
        void setTextRegion(int Left, int Top, int Right, int Bottom) {
            curTextLeft_ = Left;
            curTextTop_ = Top;
            curTextRight_ = Right;
            curTextBottom_ = Bottom;
        }

        int getTextRegionWidth() { return curTextRight_ - curTextLeft_; }
        int getTextRegionHeight() { return curTextBottom_ - curTextTop_; }

        void setupViewport(bool FillZ, float ZBuffer, bool FillBG, DWORD BGColor, float top, float left, float bottom, float right, bool ClearStencil = 0, DWORD StencilValue = 0) {

            clearDepth_ = FillZ;
            clearDepthValue_ = ZBuffer;
            clearColor_ = FillBG;
            clearColorValue_ = BGColor;
            clearStencil_ = ClearStencil;
            clearStencilValue_ = StencilValue;
            viewportTop_ = top;
            viewportLeft_ = left;
            viewportBottom_ = bottom;
            viewportRight_ = right;
        }

        void getViewportTransform(float* viewMulX, float* viewMulY, float* viewAddX, float* viewAddY) {
            gosASSERT(viewMulX && viewMulY && viewAddX && viewAddY);
            *viewMulX = (viewportRight_ - viewportLeft_)*width_;
            *viewMulY = (viewportBottom_ - viewportTop_)*height_;
            *viewAddX = viewportLeft_ * width_;
            *viewAddY = viewportTop_ * height_;
        }

		void setRenderViewport(const vec4& vp) { render_viewport_ = vp; }
		vec4 getRenderViewport() { return render_viewport_; }

		const mat4& getProj2Screen() { return projection_; }

        void setRenderState(gos_RenderState RenderState, int Value) {
            renderStates_[RenderState] = Value;
        }

        int getRenderState(gos_RenderState RenderState) const {
            return renderStates_[RenderState];
        }

        void setScreenMode(DWORD width, DWORD height, DWORD bit_depth, bool GotoFullScreen, bool anti_alias) {
            reqWidth = width;
            reqHeight = height;
            reqBitDepth = bit_depth;
            reqAntiAlias = anti_alias;
            reqGotoFullscreen = GotoFullScreen;
            pendingRequest = true;
        }

        void pushRenderStates();
        void popRenderStates();

        void applyRenderStates();

        void drawQuads(gos_VERTEX* vertices, int count);
        void drawLines(gos_VERTEX* vertices, int count);
        void drawPoints(gos_VERTEX* vertices, int count);
        void drawTris(gos_VERTEX* vertices, int count);
        void drawIndexedTris(gos_VERTEX* vertices, int num_vertices, WORD* indices, int num_indices);
		void drawIndexedTris(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl, const float* mvp);
		void drawIndexedTris(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl);
        void drawText(const char* text);

        void beginFrame();
        void endFrame();

        void init();
        void destroy();
        void flush();

        // Draw batching: consecutive gos_DrawTriangles / Quads / Lines /
        // Points / indexed triangle calls with identical render states are
        // merged into one draw. flushBatch() draws what is pending; it runs
        // when the state changes and before anything that could change
        // what the pending vertices look like (texture or buffer updates,
        // other kinds of draws, the end of the frame).
        void flushBatch();

        // debug interface
        void setNumDrawCallsToDraw(uint32_t num) { num_draw_calls_to_draw_ = num; }
        uint32_t getNumDrawCallsToDraw() { return num_draw_calls_to_draw_; }
        void setBreakOnDrawCall(bool b_break) { break_on_draw_call_ = b_break; }
        bool getBreakOnDrawCall() { return break_on_draw_call_; }
        void setBreakDrawCall(uint32_t num) { break_draw_call_num_ = num; }

        graphics::RenderContextHandle getRenderContextHandle() { return ctx_h_; }

        uint32_t getRenderState(gos_RenderState render_state) { return curStates_[render_state]; }

        gosRenderMaterial* selectBasicRenderMaterial(const RenderState& rs) const ;
        gosRenderMaterial* selectLightedRenderMaterial(const RenderState& rs) const ;

		void handleEvents();

    private:

        bool beforeDrawCall();
        void afterDrawCall();

        enum class BatchKind { None, Tris, Lines, Points, IndexedTris };
        // Makes room for a draw of `kind` with the current render states:
        // flushes the pending batch unless the draw can be appended to it.
        gosMesh* beginBatch(BatchKind kind, int num_vertices, int num_indices);
        void mirrorCurStates();
        BatchKind batch_kind_ = BatchKind::None;
        uint32_t batch_states_[gos_MaxState];

        // render target size
        int width_;
        int height_;
        graphics::RenderContextHandle ctx_h_;
        graphics::RenderWindowHandle win_h_;

        // fits vertices into viewport
        mat4 projection_;

		vec4 fog_color_;

        void initRenderStates();

        std::vector<gosTexture*> textureList_;
        std::vector<uint32_t> freeTextureSlots_; // indices of null entries in textureList_
        std::vector<gosFont*> fontList_;
        std::vector<gosBuffer*> bufferList_;
        std::vector<gosVertexDeclaration*> vertexDeclarationList_;
        std::vector<gosRenderMaterial*> materialList_;
		typedef std::map<uint32_t, gosRenderMaterial*> MaterialDBValue_t;
		typedef std::map<std::string, MaterialDBValue_t> MaterialDB_t;
        MaterialDB_t materialDB_;

        DWORD reqWidth;
        DWORD reqHeight;
        DWORD reqBitDepth;
        DWORD reqAntiAlias;
        bool reqGotoFullscreen;
        bool pendingRequest;

        // states data
        RenderState curStates_;
        RenderState renderStates_;

        static const int RENDER_STATES_STACK_SIZE = 16;
        int renderStatesStackPointer;
        RenderState statesStack_[RENDER_STATES_STACK_SIZE];
        //

        // text data
        gosTextAttribs curTextAttribs_;

        int curTextPosX_;
        int curTextPosY_;

        int curTextLeft_;
        int curTextTop_;
        int curTextRight_;
        int curTextBottom_;
        //
        
        // viewport config
        bool clearDepth_;
        float clearDepthValue_;
        bool clearColor_;
        DWORD clearColorValue_;
        bool clearStencil_;
        DWORD clearStencilValue_;
        float viewportTop_;
        float viewportLeft_;
        float viewportBottom_;
        float viewportRight_;
        //

		vec4 render_viewport_;
        
        gosMesh* quads_;
        gosMesh* tris_;
        gosMesh* indexed_tris_;
        gosMesh* lines_;
        gosMesh* points_;
        gosMesh* text_;
        gosRenderMaterial* basic_material_;
        gosRenderMaterial* basic_tex_material_;
        gosRenderMaterial* text_material_;

        gosRenderMaterial* basic_lighted_material_;
        gosRenderMaterial* basic_tex_lighted_material_;
        //
        uint32_t num_draw_calls_;
        uint32_t num_draw_calls_to_draw_;
        bool break_on_draw_call_;
        uint32_t break_draw_call_num_;

};

const std::string gosRenderer::s_Foreground = std::string("Foreground");


void gosRenderer::init() {
    initRenderStates();

    // x = 1/w; x =2*x - 1;
    // y = 1/h; y= 1- y; y =2*y - 1;
    // z = z;
    projection_ = mat4(
            2.0f / (float)width_, 0, 0.0f, -1.0f,
            0, -2.0f / (float)height_, 0.0f, 1.0f,
            0, 0, 1.0f, 0.0f,
            0, 0, 0.0f, 1.0f);

	graphics::get_drawable_size(win_h_, &Environment.drawableWidth, &Environment.drawableHeight);

    // setup viewport
    setupViewport(true, 1.0f, true, 0, 0.0f, 0.0f, 1.0f, 1.0f);

    quads_ = gosMesh::makeMesh(PRIMITIVE_TRIANGLELIST, 1024*10);
    gosASSERT(quads_);
    tris_ = gosMesh::makeMesh(PRIMITIVE_TRIANGLELIST, 1024*10);
    gosASSERT(tris_);
    indexed_tris_ = gosMesh::makeMesh(PRIMITIVE_TRIANGLELIST, 1024*10, 1024*10);
    gosASSERT(indexed_tris_);
    lines_ = gosMesh::makeMesh(PRIMITIVE_LINELIST, 1024*10);
    gosASSERT(lines_);
    points_= gosMesh::makeMesh(PRIMITIVE_POINTLIST, 1024*10);
    gosASSERT(points_);
    text_ = gosMesh::makeMesh(PRIMITIVE_TRIANGLELIST, 4024 * 6);
    gosASSERT(text_);


    const char* shader_list[] = {"gos_vertex", "gos_tex_vertex", "gos_text", "gos_vertex_lighted", "gos_tex_vertex_lighted"};
    gosRenderMaterial** shader_ptr_list[] = { &basic_material_, &basic_tex_material_, &text_material_, &basic_lighted_material_, &basic_tex_lighted_material_};

    static_assert(COUNTOF(shader_list) == COUNTOF(shader_ptr_list), "Arrays myst have same size");
    uint32_t combinations[] = {0, SHADER_FLAG_INDEX_TO_MASK(gosGLOBAL_SHADER_FLAGS::ALPHA_TEST)};

    for(size_t i=0; i<COUNTOF(combinations); ++i)
    {
        std::vector<std::string> defines;
        for(size_t bit = 0; bit < 32; ++bit)
        {
            uint32_t bit_mask = 1<<bit;
            if(bit_mask & combinations[i])
            {
                std::string s = g_shader_flags[bit];
                defines.push_back(s);
            }
        }

        gosMaterialVariationHelper helper;
        helper.addDefines(defines);
        gosMaterialVariation mvar;
        helper.getMaterialVariation(mvar);

        //TODO: remove texture / no texture variants and move it to flags
        
        for(uint32_t sh_idx = 0; sh_idx < COUNTOF(shader_list); ++sh_idx)
        {
            gosRenderMaterial* pmat = gosRenderMaterial::load(shader_list[sh_idx], mvar);
            gosASSERT(pmat);
            materialList_.push_back(pmat);
            materialDB_[ shader_list[sh_idx] ].insert(std::make_pair(combinations[i], pmat));

            *shader_ptr_list[sh_idx] = pmat;
        }
    }


    pendingRequest = false;

    num_draw_calls_ = 0;
    num_draw_calls_to_draw_ = 0;
    break_on_draw_call_ = false;
    break_draw_call_num_ = 0;

    // add fake texture so that no one will get 0 index, as it is invalid in this game
    DWORD tex_id = gos_NewEmptyTexture( gos_Texture_Solid, "DEBUG_this_is_not_a_real_texture_debug_it!", 1);
    (void)tex_id;
    gosASSERT(tex_id == INVALID_TEXTURE_ID);

	fog_color_ = vec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void gosRenderer::destroy() {

    gosMesh::destroy(quads_);
    gosMesh::destroy(tris_);
    gosMesh::destroy(indexed_tris_);
    gosMesh::destroy(lines_);
    gosMesh::destroy(points_);
    gosMesh::destroy(text_);

    // (used to loop over fontList_.size(), leaking or overrunning materials)
    for(size_t i=0; i<materialList_.size(); i++) {
        gosRenderMaterial::destroy(materialList_[i]);
    }
    materialList_.clear();

    // delete fonts before textures, because they refer them
    for(size_t i=0; i<fontList_.size(); i++) {
        while(gosFont::destroy(fontList_[i])) {};
    }
    fontList_.clear();

    for(size_t i=0; i<textureList_.size(); i++) {
        delete textureList_[i];
    }
    textureList_.clear();
    freeTextureSlots_.clear();

}

void gosRenderer::initRenderStates() {

	renderStates_[gos_State_Texture] = INVALID_TEXTURE_ID;
	renderStates_[gos_State_Texture2] = INVALID_TEXTURE_ID;
    renderStates_[gos_State_Texture3] = INVALID_TEXTURE_ID;
	renderStates_[gos_State_Filter] = gos_FilterNone;
	renderStates_[gos_State_ZCompare] = 1; 
    renderStates_[gos_State_ZWrite] = 1;
	renderStates_[gos_State_AlphaTest] = 0;
	renderStates_[gos_State_Perspective] = 1;
	renderStates_[gos_State_Specular] = 0;
	renderStates_[gos_State_Dither] = 0;
	renderStates_[gos_State_Clipping] = 0;	
	renderStates_[gos_State_WireframeMode] = 0;
	renderStates_[gos_State_AlphaMode] = gos_Alpha_OneZero;
	renderStates_[gos_State_TextureAddress] = gos_TextureWrap;
	renderStates_[gos_State_ShadeMode] = gos_ShadeGouraud;
	renderStates_[gos_State_TextureMapBlend] = gos_BlendModulateAlpha;
	renderStates_[gos_State_MipMapBias] = 0;
	renderStates_[gos_State_Fog]= 0;
	renderStates_[gos_State_MonoEnable] = 0;
	renderStates_[gos_State_Culling] = gos_Cull_None;
	renderStates_[gos_State_StencilEnable] = 0;
	renderStates_[gos_State_StencilFunc] = gos_Cmp_Never;
	renderStates_[gos_State_StencilRef] = 0;
	renderStates_[gos_State_StencilMask] = 0xffffffff;
	renderStates_[gos_State_StencilZFail] = gos_Stencil_Keep;
	renderStates_[gos_State_StencilFail] = gos_Stencil_Keep;
	renderStates_[gos_State_StencilPass] = gos_Stencil_Keep;
	renderStates_[gos_State_Multitexture] = gos_Multitexture_None;
	renderStates_[gos_State_Ambient] = 0xffffff;
	renderStates_[gos_State_Lighting] = 0;
	renderStates_[gos_State_NormalizeNormals] = 0;
	renderStates_[gos_State_VertexBlend] = 0;

    applyRenderStates();
    renderStatesStackPointer = -1;
}

void gosRenderer::pushRenderStates()
{
    gosASSERT(renderStatesStackPointer>=-1 && renderStatesStackPointer < RENDER_STATES_STACK_SIZE - 1);
    if(!(renderStatesStackPointer>=-1 && renderStatesStackPointer < RENDER_STATES_STACK_SIZE - 1)) {
        return;
    }

    renderStatesStackPointer++;
    memcpy(&statesStack_[renderStatesStackPointer], &renderStates_, sizeof(renderStates_));
}

void gosRenderer::popRenderStates()
{
    gosASSERT(renderStatesStackPointer>=0 && renderStatesStackPointer < RENDER_STATES_STACK_SIZE);
    
    if(!(renderStatesStackPointer>=0 && renderStatesStackPointer < RENDER_STATES_STACK_SIZE)) {
        return;
    }

    memcpy(&renderStates_, &statesStack_[renderStatesStackPointer], sizeof(renderStates_));
    renderStatesStackPointer--;
}

void gosRenderer::applyRenderStates() {

    // Blending, depth, culling: one backend pipeline state
    bool valid = true;
    gos_render::device()->applyPipeline(gos_render::pipelineFromGosStates(renderStates_, &valid));
    gosASSERT(valid && "Unsupported culling / depth / alpha mode value");
    const gos_RenderState pipeline_states[] = { gos_State_Culling, gos_State_ZWrite,
        gos_State_ZCompare, gos_State_AlphaMode, gos_State_AlphaTest };
    for(size_t i = 0; i < COUNTOF(pipeline_states); ++i)
        curStates_[pipeline_states[i]] = renderStates_[pipeline_states[i]];

    fog_color_ = uint32_to_vec4(renderStates_[gos_State_Fog]);
    curStates_[gos_State_Fog] = renderStates_[gos_State_Fog];

    // Sampler state is bound with each texture (no mipmaps exist, so
    // trilinear filtering is bilinear)
    const gos_render::SamplerDesc sampler = gos_render::samplerFromGosStates(renderStates_);
    curStates_[gos_State_Filter] = renderStates_[gos_State_Filter];
    curStates_[gos_State_TextureAddress] = renderStates_[gos_State_TextureAddress];

    const uint32_t tex_states[] = { gos_State_Texture, gos_State_Texture2, gos_State_Texture3 };
    for(int i=0; i<(int)COUNTOF(tex_states); ++i) {
        DWORD gosTextureHandle = renderStates_[tex_states[i]];

        gosTexture* tex = gosTextureHandle == INVALID_TEXTURE_ID ? 0 : this->getTexture(gosTextureHandle);
        if(tex) {
            gos_render::device()->bindTexture(i, &tex->getTexture(), sampler);

            gosTextureInfo texinfo;
            tex->getTextureInfo(&texinfo);
            if(renderStates_[gos_State_TextureMapBlend] == gos_BlendDecal && texinfo.format_ == gos_Texture_Alpha)
            {
                PAUSE((""));
            }
        } else {
            gos_render::device()->bindTexture(i, NULL, sampler);
        }
        curStates_[tex_states[i]] = gosTextureHandle;
    }
}

void gosRenderer::beginFrame()
{
    gos_render::device()->beginFrame();
    num_draw_calls_ = 0;
}

// Shader hot reload is a development feature (-shader-reload): it stat()s
// every shader source twice a second.
static bool g_shader_hot_reload = false;
void gos_SetShaderHotReload(bool enable) { g_shader_hot_reload = enable; }

void gosRenderer::endFrame()
{
    flushBatch();
    if (!g_shader_hot_reload)
        return;
    // Check shader files for changes (hot reload) every half second.
    static uint64_t last_check_time = timing::get_wall_time_ms();
    const uint64_t now = timing::get_wall_time_ms();
    if(now - last_check_time > 500)
    {
        last_check_time = now; // previously never updated: stat()ed every frame
        for(size_t i=0; i< materialList_.size(); ++i)
        {
            materialList_[i]->checkReload();
        }
    }
}

// -windowed: ignore the game's fullscreen setting (tests, debugging)
static bool g_force_windowed = false;
void gos_SetForceWindowed(bool windowed) { g_force_windowed = windowed; }

void gosRenderer::handleEvents()
{
    if(pendingRequest) {
        flushBatch();

        width_ = reqWidth;
        height_ = reqHeight;

        // x = 1/w; x =2*x - 1;
        // y = 1/h; y= 1- y; y =2*y - 1;
        // z = z;
        projection_ = mat4(2.0f / (float)width_, 0, 0.0f, -1.0f,
                0, -2.0f / (float)height_, 0.0f, 1.0f,
                0, 0, 1.0f, 0.0f,
                0, 0, 0.0f, 1.0f);

        if(graphics::resize_window(win_h_, width_, height_))
		{
            graphics::set_window_fullscreen(win_h_, reqGotoFullscreen && !g_force_windowed);

            Environment.screenWidth = width_;
            Environment.screenHeight = height_;

			graphics::get_drawable_size(win_h_, &Environment.drawableWidth, &Environment.drawableHeight);

        }
        pendingRequest = false;
    }
}

bool gosRenderer::beforeDrawCall()
{
    num_draw_calls_++;
    if(break_draw_call_num_ == num_draw_calls_ && break_on_draw_call_) {
        PAUSE(("Draw call %d break\n", num_draw_calls_ - 1));
    }

    return (num_draw_calls_ > num_draw_calls_to_draw_) && num_draw_calls_to_draw_ != 0;
}

void gosRenderer::afterDrawCall()
{
}

gosRenderMaterial* gosRenderer::selectBasicRenderMaterial(const RenderState& rs) const 
{
	const auto& sh_var = rs[gos_State_Texture]!=0 ?
		materialDB_.find("gos_tex_vertex")->second :
		materialDB_.find("gos_vertex")->second;
    uint32_t flags = rs[gos_State_AlphaTest] ? SHADER_FLAG_INDEX_TO_MASK(gosGLOBAL_SHADER_FLAGS::ALPHA_TEST) : 0;

    if(sh_var.count(flags))
        return sh_var.at(flags);
    else
    {
        STOP(("Trying to get variation which does not exist: shader: %s flags: %d\n", "basic", flags));
        return nullptr;
    }
}

gosRenderMaterial* gosRenderer::selectLightedRenderMaterial(const RenderState& rs) const
{
	const auto& sh_var = rs[gos_State_Texture]!=0 ?
		materialDB_.find("gos_tex_vertex_lighted")->second :
		materialDB_.find("gos_vertex_lighted")->second;
    uint32_t flags = rs[gos_State_AlphaTest] ? SHADER_FLAG_INDEX_TO_MASK(gosGLOBAL_SHADER_FLAGS::ALPHA_TEST) : 0;

    if(sh_var.count(flags))
        return sh_var.at(flags);
    else
    {
        STOP(("Trying to get variation which does not exist: shader: %s flags: %d\n", "lighted", flags));
        return nullptr;
    }
}

void gos_FlushDrawBatch() { if(g_gos_renderer) g_gos_renderer->flushBatch(); }

// -no-batching: every call is drawn on its own (as before batching)
static bool g_draw_batching = true;
void gos_SetDrawBatching(bool enable) { g_draw_batching = enable; }

void gosRenderer::mirrorCurStates()
{
    // what applyRenderStates() records, without touching the device:
    // gos_GetRenderState reports the state of the last draw
    const gos_RenderState states[] = { gos_State_Culling, gos_State_ZWrite, gos_State_ZCompare,
        gos_State_AlphaMode, gos_State_AlphaTest, gos_State_Fog, gos_State_Filter,
        gos_State_TextureAddress, gos_State_Texture, gos_State_Texture2, gos_State_Texture3 };
    for(size_t i = 0; i < COUNTOF(states); ++i)
        curStates_[states[i]] = renderStates_[states[i]];
}

gosMesh* gosRenderer::beginBatch(BatchKind kind, int num_vertices, int num_indices)
{
    gosMesh* mesh = kind == BatchKind::Tris ? tris_ : kind == BatchKind::Lines ? lines_
                  : kind == BatchKind::Points ? points_ : indexed_tris_;
    const bool same = batch_kind_ == kind && 0 == memcmp(batch_states_, renderStates_, sizeof(batch_states_));
    const bool fits = mesh->getNumVertices() + num_vertices <= mesh->getVertexCapacity() &&
                      mesh->getNumIndices() + num_indices <= mesh->getIndexCapacity() &&
                      // 16-bit indices are rebased by the vertex count
                      (kind != BatchKind::IndexedTris || mesh->getNumVertices() + num_vertices <= 0x10000);
    if(!same || !fits) {
        flushBatch();
        batch_kind_ = kind;
        memcpy(batch_states_, renderStates_, sizeof(batch_states_));
    }
    mirrorCurStates();
    return mesh;
}

void gosRenderer::flushBatch()
{
    if(batch_kind_ == BatchKind::None)
        return;
    const BatchKind kind = batch_kind_;
    batch_kind_ = BatchKind::None;

    // draw with the states the batch was recorded with
    RenderState saved;
    memcpy(saved, renderStates_, sizeof(saved));
    memcpy(renderStates_, batch_states_, sizeof(renderStates_));
    applyRenderStates();

    gosRenderMaterial* mat = (kind == BatchKind::Tris || kind == BatchKind::IndexedTris)
        ? selectBasicRenderMaterial(curStates_) : basic_material_;
    gosASSERT(mat);
    mat->setTransform(projection_);
    mat->setFogColor(fog_color_);
    switch(kind) {
        case BatchKind::Tris: tris_->draw(mat); tris_->rewind(); break;
        case BatchKind::Lines: lines_->draw(mat); lines_->rewind(); break;
        case BatchKind::Points: points_->draw(mat); points_->rewind(); break;
        case BatchKind::IndexedTris: indexed_tris_->drawIndexed(mat); indexed_tris_->rewind(); break;
        default: break;
    }
    memcpy(renderStates_, saved, sizeof(renderStates_));
}

void gosRenderer::drawQuads(gos_VERTEX* vertices, int count) {
    gosASSERT(vertices);

    if(beforeDrawCall()) return;

    // each quad becomes two triangles in the triangle batch
    const int num_quads = count / 4;
    gosMesh* mesh = beginBatch(BatchKind::Tris, num_quads * 6, 0);
    gosASSERT(mesh->getNumVertices() + num_quads * 6 <= mesh->getVertexCapacity());
    for(int i=0; i<count;i+=4) {
        mesh->addVertices(vertices + 4*i + 0, 1);
        mesh->addVertices(vertices + 4*i + 1, 1);
        mesh->addVertices(vertices + 4*i + 2, 1);

        mesh->addVertices(vertices + 4*i + 0, 1);
        mesh->addVertices(vertices + 4*i + 2, 1);
        mesh->addVertices(vertices + 4*i + 3, 1);
    }
    if(!g_draw_batching)
        flushBatch();

    afterDrawCall();
}

void gosRenderer::drawLines(gos_VERTEX* vertices, int count) {
    gosASSERT(vertices);

    if(beforeDrawCall()) return;

    gosMesh* mesh = beginBatch(BatchKind::Lines, count, 0);
    gosASSERT(mesh->getNumVertices() + count <= mesh->getVertexCapacity());
    mesh->addVertices(vertices, count);
    if(!g_draw_batching)
        flushBatch();

    afterDrawCall();
}

void gosRenderer::drawPoints(gos_VERTEX* vertices, int count) {
    gosASSERT(vertices);

    if(beforeDrawCall()) return;

    gosMesh* mesh = beginBatch(BatchKind::Points, count, 0);
    gosASSERT(mesh->getNumVertices() + count <= mesh->getVertexCapacity());
    mesh->addVertices(vertices, count);
    if(!g_draw_batching)
        flushBatch();

    afterDrawCall();
}

void gosRenderer::drawTris(gos_VERTEX* vertices, int count) {
    gosASSERT(vertices);

    gosASSERT((count % 3) == 0);

    if(beforeDrawCall()) return;

    gosMesh* mesh = beginBatch(BatchKind::Tris, count, 0);
    gosASSERT(mesh->getNumVertices() + count <= mesh->getVertexCapacity());
    mesh->addVertices(vertices, count);
    if(!g_draw_batching)
        flushBatch();

    afterDrawCall();
}

void gosRenderer::drawIndexedTris(gos_VERTEX* vertices, int num_vertices, WORD* indices, int num_indices) {
    gosASSERT(vertices && indices);

    gosASSERT((num_indices % 3) == 0);

    if(beforeDrawCall()) return;

    gosMesh* mesh = beginBatch(BatchKind::IndexedTris, num_vertices, num_indices);
    gosASSERT(mesh->getNumVertices() + num_vertices <= mesh->getVertexCapacity());
    gosASSERT(mesh->getNumIndices() + num_indices <= mesh->getIndexCapacity());
    // indices refer to this call's vertices: rebase them
    const WORD base = (WORD)mesh->getNumVertices();
    mesh->addVertices(vertices, num_vertices);
    if(base == 0) {
        mesh->addIndices(indices, num_indices);
    } else {
        WORD rebased[256];
        for(int i = 0; i < num_indices; i += 256) {
            const int n = num_indices - i < 256 ? num_indices - i : 256;
            for(int j = 0; j < n; ++j)
                rebased[j] = (WORD)(indices[i + j] + base);
            mesh->addIndices(rebased, n);
        }
    }
    if(!g_draw_batching)
        flushBatch();

    afterDrawCall();
}

void gosRenderer::drawIndexedTris(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl, const float* mvp)
{
    flushBatch();
    gosASSERT(ib && vb && mvp);
    gosASSERT((ib->count_ % 3) == 0);

    if(beforeDrawCall()) return;

    applyRenderStates();

    gosRenderMaterial* mat = selectLightedRenderMaterial(curStates_);
    gosASSERT(mat);

	mat4 transform(	mvp[0], mvp[1], mvp[2], mvp[3], 
					mvp[4], mvp[5], mvp[6], mvp[7],
					mvp[8], mvp[9], mvp[10], mvp[11],
					mvp[12], mvp[13], mvp[14], mvp[15]);

	vec4 vp = g_gos_renderer->getRenderViewport();

	mat->getShader()->setFloat4("vp", vp);
	mat->getShader()->setMat4("projection_", projection_);

    mat->setTransform(transform);
    //mat->setFogColor(fog_color_);

	gosMesh::drawIndexed(ib, vb, vdecl, mat);

    afterDrawCall();
}

void gosRenderer::drawIndexedTris(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl)
{
    flushBatch();
    gosASSERT(ib && vb);
    gosASSERT((ib->count_ % 3) == 0);

    if(beforeDrawCall()) return;

    applyRenderStates();

	// maybe getCurMaterial->set.... to not set it from outer code?
	//vec4 vp = g_gos_renderer->getRenderViewport();
	//mat->getShader()->setFloat4("vp", vp);
	//mat->getShader()->setMat4("projection_", projection_);

	gosMesh::drawIndexed(ib, vb, vdecl);

    afterDrawCall();
}

static int get_next_break(const char* text) {
    const char* start = text;
    do {
        char c = *text;
        if(c==' ' || c=='\n')
            return (int32_t)(text - start);
    } while(*text++);

    return (int32_t)(text - start - 1);
}

int findTextBreak(const char* text, const int count, const gosFont* font, const int region_width, int* out_str_width) {

    int width = 0;
    int pos = 0;

    int space_adv = font->getCharAdvance(' ');

    while(text[pos]) {

        int break_pos = get_next_break(text + pos);

        int cur_width = 0;
        for(int j=0;j<break_pos;++j) {
            cur_width += font->getCharAdvance(text[pos + j]);
        }

        // if next possible break will not fit, then return now
        if(width + cur_width >= region_width) {

            if(pos == 0) { // handle case when only one word in line and it does not fit it, just return whole line
                width = cur_width;
                pos = break_pos;
            }
            break;
        } else {
            width += cur_width;
            pos += break_pos;

            if(text[pos] == '\n') {
                pos++;
                break;
            }

            if(text[pos] == ' ') {
                width += space_adv;
                pos++;
            }
        }
    }

    if(out_str_width)
        *out_str_width = width;
    return pos;
}
// returnes num lines in text which should be wrapped in region_width
int calcTextHeight(const char* text, const int count, const gosFont* font, int region_width)
{
    int pos = 0;
    int num_lines = 0;
    while(pos < count) {

        int num_chars = findTextBreak(text + pos, count - pos, font, region_width, NULL);
        pos += num_chars;
        num_lines++;
    }
    return num_lines;
}

void addCharacter(gosMesh* text_, const float u, const float v, const float u2, const float v2, const float x, const float y, const float x2, const float y2) {

    gos_VERTEX tr, tl, br, bl;

    tl.x = x;
    tl.y = y;
    tl.z = 0;
    tl.u = u;
    tl.v = v;
    tl.argb = 0xffffffff;
    tl.frgb = 0xff000000;

    tr.x = x2;
    tr.y = y;
    tr.z = 0;
    tr.u = u2;
    tr.v = v;
    tr.argb = 0xffffffff;
    tr.frgb = 0xff000000;

    bl.x = x;
    bl.y = y2;
    bl.z = 0;
    bl.u = u;
    bl.v = v2;
    bl.argb = 0xffffffff;
    bl.frgb = 0xff000000;

    br.x = x2;
    br.y = y2;
    br.z = 0;
    br.u = u2;
    br.v = v2;
    br.argb = 0xffffffff;
    br.frgb = 0xff000000;

    text_->addVertices(&tl, 1);
    text_->addVertices(&tr, 1);
    text_->addVertices(&bl, 1);

    text_->addVertices(&tr, 1);
    text_->addVertices(&br, 1);
    text_->addVertices(&bl, 1);

}

void gosRenderer::drawText(const char* text) {
    flushBatch();
    gosASSERT(text);

    if(beforeDrawCall()) return;

    const int count = (int)strlen(text);  
/*
    if(text_->getNumVertices() + count > text_->getCapacity()) {
        applyRenderStates();
        gosRenderMaterial* mat = 
            curStates_[gos_State_Texture]!=0 ? basic_tex_material_ : basic_material_;
        text_->draw(mat);
        text_->rewind();
    } 
*/

    gosASSERT(text_->getNumVertices() + 6 * count <= text_->getVertexCapacity());

    int ix, iy;
    getTextPos(ix, iy);
	float x = (float)ix, y = (float)iy;
    const float start_x = x;

    const gosTextAttribs& ta = g_gos_renderer->getTextAttributes();
    const gosFont* font = ta.FontHandle;

    const DWORD tex_id = font->getTextureId();
    const gosTexture* tex = getTexture(tex_id);
    gosTextureInfo ti;
    tex->getTextureInfo(&ti);
    const float oo_tex_width = 1.0f / (float)ti.width_;
    const float oo_tex_height = 1.0f / (float)ti.height_;
    
    const int font_height = font->getMaxCharHeight();
    const int font_ascent = font->getFontAscent();

    const int region_width = getTextRegionWidth();
    const int region_height = getTextRegionHeight();

    const int num_lines = calcTextHeight(text, count, font, region_width);
    if(ta.WrapType == 3) { // center in Y direction as well
        y += (region_height - num_lines * font_height) / 2;
    }
   
    int pos = 0;
    int str_width = 0;
    while(pos < count) {

        x = start_x;    
        int num_chars = findTextBreak(text + pos, count - pos, font, region_width, &str_width);

        // WrapType		- 0=Left aligned, 1=Right aligned, 2=Centered, 3=Centered in region (X and Y)
        switch(ta.WrapType) {
            case 0: break;
            case 1: x += region_width - str_width; break;
            case 2: x += (region_width - str_width) / 2; break;
            case 3: // see vertical centering above
                    x += (region_width - str_width) / 2;
                    break;
        }

        for(int i=0; i<num_chars; ++i) {

            const char c = text[i + pos];

            const gosGlyphMetrics& gm = font->getGlyphMetrics(c);
            int char_off_x = gm.minx;
            int char_off_y = font_ascent - gm.maxy;
            int char_w = gm.maxx - gm.minx;
            int char_h = gm.maxy - gm.miny;

            uint32_t iu0 = gm.u + char_off_x;
            uint32_t iv0 = gm.v + char_off_y;
            uint32_t iu1 = iu0 + char_w;
            uint32_t iv1 = iv0 + char_h;

            float u0 = (float)iu0 * oo_tex_width;
            float v0 = (float)iv0 * oo_tex_height;
            float u1 = (float)iu1 * oo_tex_width;
            float v1 = (float)iv1 * oo_tex_height;

            addCharacter(text_, u0, v0, u1, v1, (float)(x + char_off_x), (float)(y + char_off_y), (float)(x + char_off_x + char_w), (float)(y + char_off_y + char_h));

            x += font->getCharAdvance(c);
        }
        y += font_height;
        pos += num_chars;
    }
    // Client code sets every other state; only texture and filter are changed
    // here, and both are restored after the draw so the caller never sees it.
    const int prev_texture = getRenderState(gos_State_Texture);
    const int prev_filter = getRenderState(gos_State_Filter);
    
    // All states are set by client code
    // so we only set font texture
    setRenderState(gos_State_Texture, tex_id);
    setRenderState(gos_State_Filter, gos_FilterNone);

    // for now draw anyway because no render state saved for draw calls
    applyRenderStates();
    gosRenderMaterial* mat = text_material_;

    //ta.Foreground
    vec4 fg;
    fg.x = (float)((ta.Foreground & 0xFF0000) >> 16);
    fg.y = (float)((ta.Foreground & 0xFF00) >> 8);
    fg.z = (float)(ta.Foreground & 0xFF);
    fg.w = 255.0f;//(ta.Foreground & 0xFF000000) >> 24;
    fg = fg / 255.0f;
    mat->getShader()->setFloat4(s_Foreground, fg);
    //ta.Size 
    //ta.WordWrap 
    //ta.Proportional
    //ta.Bold
    //ta.Italic
    //ta.WrapType
    //ta.DisableEmbeddedCodes

    mat->setTransform(projection_);
    mat->setFogColor(fog_color_);
    text_->draw(mat);
    text_->rewind();

    setRenderState(gos_State_Texture, prev_texture);
    setRenderState(gos_State_Filter, prev_filter);

    afterDrawCall();
}

void gosRenderer::flush()
{
}

bool gos_CreateRenderer(graphics::RenderContextHandle ctx_h, graphics::RenderWindowHandle win_h, int w, int h) {

    const bool gpu = gos_render::backend() == gos_render::Backend::GPU;
    gos_render::Device* dev = gos_render::createDevice(gos_render::backend());
    if(!dev) {
        fprintf(stderr, "error: this program was built without the %s renderer\n", gpu ? "SDL3 GPU" : "OpenGL");
        return false;
    }
    if(!dev->init()) {
        fprintf(stderr, "error: the %s renderer cannot start\n", gpu ? "SDL3 GPU" : "OpenGL");
        delete dev;
        return false;
    }
    gos_render::setDevice(dev);

    g_gos_renderer = new gosRenderer(ctx_h, win_h, w, h);
    g_gos_renderer->init();
    return true;
}

void gos_DestroyRenderer() {

    g_gos_renderer->destroy();
    delete g_gos_renderer;
    g_gos_renderer = NULL;

    gos_render::Device* dev = gos_render::device();
    gos_render::setDevice(NULL);
    dev->shutdown();
    delete dev;
}

void gos_RendererBeginFrame() {
    gosASSERT(g_gos_renderer);
    g_gos_renderer->beginFrame();
}

void gos_RendererEndFrame() {
    gosASSERT(g_gos_renderer);
    g_gos_renderer->endFrame();
}

void gos_RendererHandleEvents() {
    gosASSERT(g_gos_renderer);
    g_gos_renderer->handleEvents();
}


gosFont::~gosFont()
{
    if(tex_id_ != INVALID_TEXTURE_ID)
        getGosRenderer()->deleteTexture(tex_id_);

    delete[] gi_.glyphs_;
    delete[] font_name_;
    delete[] font_id_;
}

////////////////////////////////////////////////////////////////////////////////
gosFont* gosFont::load(const char* fontFile) {

    char fname[256];
    char dir[256];
    _splitpath(fontFile, NULL, dir, fname, NULL);
    const char* tex_ext = ".bmp";
    const char* glyph_ext = ".glyph";
    
	const size_t textureNameSize = strlen(fname) + sizeof('/') + strlen(dir) + strlen(tex_ext) + 1;
    char* textureName = new char[textureNameSize];
	memset(textureName, 0, textureNameSize);

	const size_t glyphNameSize = strlen(fname) + sizeof('/') + strlen(dir) + strlen(glyph_ext) + 1;
    char* glyphName = new char[glyphNameSize];
	memset(glyphName, 0, glyphNameSize);

    uint32_t formatted_len = S_snprintf(textureName, textureNameSize, "%s/%s%s", dir, fname, tex_ext);
	gosASSERT(formatted_len <= textureNameSize - 1);

    formatted_len = S_snprintf(glyphName, glyphNameSize, "%s/%s%s", dir, fname, glyph_ext);
	gosASSERT(formatted_len <= glyphNameSize - 1);

    gosTexture* ptex = new gosTexture(gos_Texture_Alpha, textureName, 0, NULL, 0, false);
    if(!ptex || !ptex->createHardwareTexture()) {
        STOP(("Failed to create font texture: %s\n", textureName));
    }

    DWORD tex_id = getGosRenderer()->addTexture(ptex);

    gosFont* font = new gosFont();
    if(!gos_load_glyphs(glyphName, font->gi_)) {
        delete font;
        STOP(("Failed to load font glyphs: %s\n", glyphName));
        return NULL;
    }

    font->font_name_ = new char[strlen(fname) + 1];
    strcpy(font->font_name_, fname);

    font->font_id_ = new char[strlen(fontFile) + 1];
    strcpy(font->font_id_, fontFile);

    font->tex_id_ = tex_id;

    delete[] textureName;
    delete[] glyphName;

    return font;

}

uint32_t gosFont::destroy(gosFont* font) {
    uint32_t rc = font->decRef();
    if(0 == rc) {
        delete font;
    }

    return rc;
}

void gosFont::getCharUV(int c, uint32_t* u, uint32_t* v) const {

    gosASSERT(u && v);

    int32_t pos = c - gi_.start_glyph_;
    if(pos < 0 || pos >= (int)gi_.num_glyphs_) {
        *u = *v = 0;
        return;
    }

    *u = gi_.glyphs_[pos].u;
    *v = gi_.glyphs_[pos].v;
}

int gosFont::getCharAdvance(int c) const
{
    int pos = c - gi_.start_glyph_;
    if(pos < 0 || pos >= (int)gi_.num_glyphs_) {
        return getMaxCharWidth();
    }

    return gi_.glyphs_[pos].advance;
}

const gosGlyphMetrics& gosFont::getGlyphMetrics(int c) const {
    int pos = c - gi_.start_glyph_;
    if(pos < 0 || pos >= (int)gi_.num_glyphs_)
        pos = 0;

    return gi_.glyphs_[pos];
}




////////////////////////////////////////////////////////////////////////////////
// graphics
//
void _stdcall gos_DrawLines(gos_VERTEX* Vertices, int NumVertices)
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->drawLines(Vertices, NumVertices);
}
void _stdcall gos_DrawPoints(gos_VERTEX* Vertices, int NumVertices)
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->drawPoints(Vertices, NumVertices);
}

bool g_disable_quads = true;
void _stdcall gos_DrawQuads(gos_VERTEX* Vertices, int NumVertices)
{
    gosASSERT(g_gos_renderer);
    if(g_disable_quads == false )
        g_gos_renderer->drawQuads(Vertices, NumVertices);
}
void _stdcall gos_DrawTriangles(gos_VERTEX* Vertices, int NumVertices)
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->drawTris(Vertices, NumVertices);
}

void __stdcall gos_GetViewport( float* pViewportMulX, float* pViewportMulY, float* pViewportAddX, float* pViewportAddY )
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->getViewportTransform(pViewportMulX, pViewportMulY, pViewportAddX, pViewportAddY);
}

HGOSFONT3D __stdcall gos_LoadFont( const char* FontFile, DWORD StartLine/* = 0*/, int CharCount/* = 256*/, DWORD TextureHandle/*=0*/)
{

    gosFont* font = getGosRenderer()->findFont(FontFile);
    if(!font) {
        font = gosFont::load(FontFile);
        getGosRenderer()->addFont(font);
    } else {
        font->addRef();
    }

    return font;
}

void __stdcall gos_DeleteFont( HGOSFONT3D FontHandle )
{
    gosASSERT(FontHandle);
    gosFont* font = FontHandle;
    getGosRenderer()->deleteFont(font);
}

DWORD __stdcall gos_NewEmptyTexture( gos_TextureFormat Format, const char* Name, DWORD HeightWidth, DWORD Hints/*=0*/, gos_RebuildFunction pFunc/*=0*/, void *pInstance/*=0*/)
{
    int w = HeightWidth;
    int h = HeightWidth;
    if(HeightWidth&0xffff0000)
    {
        h = HeightWidth >> 16;
        w = HeightWidth & 0xffff;
    }
    gosTexture* ptex = new gosTexture(Format, Hints, w, h, Name);

    if(!ptex->createHardwareTexture()) {
        STOP(("Failed to create texture\n"));
        return INVALID_TEXTURE_ID;
    }

    return g_gos_renderer->addTexture(ptex);
}
DWORD __stdcall gos_NewTextureFromMemory( gos_TextureFormat Format, const char* FileName, BYTE* pBitmap, DWORD Size, DWORD Hints/*=0*/, gos_RebuildFunction pFunc/*=0*/, void *pInstance/*=0*/)
{
    gosASSERT(pFunc == 0);

    gosTexture* ptex = new gosTexture(Format, FileName, Hints, pBitmap, Size, true);
    if(!ptex->createHardwareTexture()) {
        STOP(("Failed to create texture\n"));
        return INVALID_TEXTURE_ID;
    }

    return g_gos_renderer->addTexture(ptex);
}

DWORD __stdcall gos_NewTextureFromFile( gos_TextureFormat Format, const char* FileName, DWORD Hints/*=0*/, gos_RebuildFunction pFunc/*=0*/, void *pInstance/*=0*/)
{
    gosTexture* ptex = new gosTexture(Format, FileName, Hints, NULL, 0, false);
    if(!ptex->createHardwareTexture()) {
        STOP(("Failed to create texture\n"));
        return INVALID_TEXTURE_ID;
    }
    return g_gos_renderer->addTexture(ptex);
}
void __stdcall gos_DestroyTexture( DWORD Handle )
{
    g_gos_renderer->deleteTexture(Handle);
}

void __stdcall gos_LockTexture( DWORD Handle, DWORD MipMapSize, bool ReadOnly, TEXTUREPTR* TextureInfo )
{
    // "Locking" reads the texture back into a CPU-side copy; gos_UnLockTexture
    // uploads it again unless it was locked read-only. Only mip 0 is supported.
    gosASSERT(MipMapSize == 0);
    int mip_level = 0; //func(MipMapSize);

    gosTextureInfo info;
    int pitch = 0;
    gosTexture* ptex = g_gos_renderer->getTexture(Handle);
    ptex->getTextureInfo(&info);
    BYTE* pdata = ptex->Lock(mip_level, ReadOnly, &pitch);

    TextureInfo->pTexture = (DWORD*)pdata;
    TextureInfo->Width = info.width_;
    TextureInfo->Height = info.height_;
    TextureInfo->Pitch = pitch;
    TextureInfo->Type = info.format_;

    //gosASSERT(0 && "Not implemented");
}

void __stdcall gos_UnLockTexture( DWORD Handle )
{
    gosTexture* ptex = g_gos_renderer->getTexture(Handle);
    ptex->Unlock();

    //gosASSERT(0 && "Not implemented");
}

void __stdcall gos_PushRenderStates()
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->pushRenderStates();
} 

void __stdcall gos_PopRenderStates()
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->popRenderStates();
}

void __stdcall gos_RenderIndexedArray( gos_VERTEX* pVertexArray, DWORD NumberVertices, WORD* lpwIndices, DWORD NumberIndices )
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->drawIndexedTris(pVertexArray, NumberVertices, lpwIndices, NumberIndices);
}

void __stdcall gos_RenderIndexedArray( gos_VERTEX_2UV* pVertexArray, DWORD NumberVertices, WORD* lpwIndices, DWORD NumberIndices )
{
   gosASSERT(0 && "not implemented");
}

void __stdcall gos_RenderIndexedArray(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl, const float* mvp)
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->drawIndexedTris(ib, vb, vdecl, mvp);
}

void __stdcall gos_RenderIndexedArray(HGOSBUFFER ib, HGOSBUFFER vb, HGOSVERTEXDECLARATION vdecl)
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->drawIndexedTris(ib, vb, vdecl);
}

void __stdcall gos_SetRenderState( gos_RenderState RenderState, int Value )
{
    gosASSERT(g_gos_renderer);
    // gos_BlendDecal mode is not suported (currently texture color always modulated with vertex color)
    //gosASSERT(RenderState!=gos_State_TextureMapBlend || (Value == gos_BlendDecal));
    g_gos_renderer->setRenderState(RenderState, Value);
}

void __stdcall gos_SetScreenMode( DWORD Width, DWORD Height, DWORD bitDepth/*=16*/, DWORD Device/*=0*/, bool disableZBuffer/*=0*/, bool AntiAlias/*=0*/, bool RenderToVram/*=0*/, bool GotoFullScreen/*=0*/, int DirtyRectangle/*=0*/, bool GotoWindowMode/*=0*/, bool EnableStencil/*=0*/, DWORD Renderer/*=0*/)
{
    gosASSERT(g_gos_renderer);
    gosASSERT((GotoFullScreen && !GotoWindowMode) || (!GotoFullScreen&&GotoWindowMode) || (!GotoFullScreen&&!GotoWindowMode));

    g_gos_renderer->setScreenMode(Width, Height, bitDepth, GotoFullScreen, AntiAlias);
}

void __stdcall gos_SetupViewport( bool FillZ, float ZBuffer, bool FillBG, DWORD BGColor, float top, float left, float bottom, float right, bool ClearStencil/*=0*/, DWORD StencilValue/*=0*/)
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->setupViewport(FillZ, ZBuffer, FillBG, BGColor, top, left, bottom, right, ClearStencil, StencilValue);
}


void __stdcall gos_SetRenderViewport(float x, float y, float w, float h)
{
    gosASSERT(g_gos_renderer);
	//glViewport(x, y, w, h);
	g_gos_renderer->setRenderViewport(vec4(x, y, w, h));
}

void __stdcall gos_GetRenderViewport(float* x, float* y, float* w, float* h)
{
    gosASSERT(x && y && w && h);
    gosASSERT(g_gos_renderer);
	vec4 vp = g_gos_renderer->getRenderViewport();
	*x = vp.x;
	*y = vp.y;
	*w = vp.z;
	*h = vp.w;
}


void __stdcall gos_TextDraw( const char *Message, ... )
{

	if (!Message || !strlen(Message)) {
        SPEW(("GRAPHICS", "Trying to draw zero legth string\n"));
        return;
    }

	va_list	ap;
    va_start(ap, Message);

    static const int MAX_TEXT_LEN = 4096;
	char text[MAX_TEXT_LEN] = {0};

	vsnprintf(text, MAX_TEXT_LEN - 1, Message, ap);

	size_t len = strlen(text);
	text[len] = '\0';

    va_end(ap);

    gosASSERT(g_gos_renderer);
    g_gos_renderer->drawText(text);
}

// Corners go clockwise: (Left,Top) (Right,Top) (Right,Bottom) (Left,Bottom).
void __stdcall gos_TextDrawBackground( int Left, int Top, int Right, int Bottom, DWORD Color )
{
    gosASSERT(g_gos_renderer);

    //PAUSE((""));

    gos_VERTEX v[4];
    v[0].x = (float)Left;
    v[0].y = (float)Top;
    v[0].z = 0;
	v[0].argb = Color;
	v[0].frgb = 0;
	v[0].u = 0;	
	v[0].v = 0;	
    memcpy(&v[1], &v[0], sizeof(gos_VERTEX));
    memcpy(&v[2], &v[0], sizeof(gos_VERTEX));
    memcpy(&v[3], &v[0], sizeof(gos_VERTEX));
    v[1].x = (float)Right;
    v[1].u = 1.0f;

    v[2].x = (float)Right;
    v[2].y = (float)Bottom;
    v[2].u = 1.0f;
    v[2].v = 1.0f;

    v[3].y = (float)Bottom;
    v[3].v = 1.0f;

    if(g_disable_quads == false )
        g_gos_renderer->drawQuads(v, 4);
}

void __stdcall gos_TextSetAttributes( HGOSFONT3D FontHandle, DWORD Foreground, float Size, bool WordWrap, bool Proportional, bool Bold, bool Italic, DWORD WrapType/*=0*/, bool DisableEmbeddedCodes/*=0*/)
{
    gosASSERT(g_gos_renderer);

    gosTextAttribs& ta = g_gos_renderer->getTextAttributes();
    ta.FontHandle = FontHandle;
    ta.Foreground = Foreground;
    ta.Size = Size;
    ta.WordWrap = WordWrap;
    ta.Proportional = Proportional;
    ta.Bold = Bold;
    ta.Italic = Italic;
    ta.WrapType = WrapType;
    ta.DisableEmbeddedCodes = DisableEmbeddedCodes;
}

void __stdcall gos_TextSetPosition( int XPosition, int YPosition )
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->setTextPos(XPosition, YPosition);
}

void __stdcall gos_TextSetRegion( int Left, int Top, int Right, int Bottom )
{
    gosASSERT(g_gos_renderer);
    g_gos_renderer->setTextRegion(Left, Top, Right, Bottom);
}

void __stdcall gos_TextStringLength( DWORD* Width, DWORD* Height, const char *fmt, ... )
{
    gosASSERT(Width && Height);

    if(!fmt) {
        SPEW(("GRAPHICS", "No text to calculate length!"));
        *Width = 1;
        *Height = 1;
        return;
    }

    const int   MAX_TEXT_LEN = 4096;
	char        text[MAX_TEXT_LEN] = {0};
	va_list	    ap;

    va_start(ap, fmt);
	vsnprintf(text, MAX_TEXT_LEN - 1, fmt, ap);
    va_end(ap);

	size_t len = strlen(text);
    text[len] = '\0';

    const gosTextAttribs& ta = g_gos_renderer->getTextAttributes();
    const gosFont* font = ta.FontHandle;
    gosASSERT(font);

    int num_newlines = 0;
    int max_width = 0;
    int cur_width = 0;
    const char* txtptr = text;

    while(*txtptr) {
        if(*txtptr == '\n') {
            num_newlines++;
            max_width = max_width > cur_width ? max_width : cur_width;
            cur_width = 0;
        } else {
            const int cw = font->getCharAdvance(*txtptr);
            cur_width += cw;
        }
        txtptr++;
    }
    max_width = max_width > cur_width ? max_width : cur_width;

    *Width = max_width;
    *Height = (num_newlines + 1) * font->getMaxCharHeight();
}

////////////////////////////////////////////////////////////////////////////////
size_t __stdcall gos_GetMachineInformation( MachineInfo mi, int Param1/*=0*/, int Param2/*=0*/, int Param3/*=0*/, int Param4/*=0*/)
{
    // Values the game only uses to pick default detail settings; modern GPUs
    // exceed all of the original thresholds, so report generous constants.
    if(mi == gos_Info_GetDeviceLocalMemory)
        return 1024*1024*1024;
    if(mi == gos_Info_GetDeviceAGPMemory)
        return 512*1024*1024; 
    if (mi == gos_Info_CanMultitextureDetail)
        return true;
    if(mi == gos_Info_NumberDevices)
        return 1;
    if(mi == gos_Info_GetDeviceName)
        return (size_t)gos_render::device()->name();
    if(mi == gos_Info_ValidMode) {
        int xres = Param2;
        int yres = Param3;
        int bpp = Param4;
        return graphics::is_mode_supported(xres, yres, bpp) ? 1 : 0;
    }
    if(mi == gos_Info_GetIMECaretStatus)
        return 1;

    return 0;
}

int gos_GetWindowDisplayIndex()
{   
    gosASSERT(g_gos_renderer);
    
    return graphics::get_window_display_index(g_gos_renderer->getRenderContextHandle());
}

int gos_GetNumDisplayModes(int DisplayIndex)
{
    return graphics::get_num_display_modes(DisplayIndex);
}

bool gos_GetDisplayModeByIndex(int DisplayIndex, int ModeIndex, int* XRes, int* YRes, int* BitDepth)
{
    return graphics::get_display_mode_by_index(DisplayIndex, ModeIndex, XRes, YRes, BitDepth);
}


////////////////////////////////////////////////////////////////////////////////
// GPU Buffers management code
////////////////////////////////////////////////////////////////////////////////

static gos_render::BufferKind toBufferKind(gosBUFFER_TYPE type)
{
	switch (type)
	{
		case gosBUFFER_TYPE::VERTEX: return gos_render::BufferKind::Vertex;
		case gosBUFFER_TYPE::INDEX: return gos_render::BufferKind::Index;
		case gosBUFFER_TYPE::UNIFORM: return gos_render::BufferKind::Uniform;
		default:
			gosASSERT(0 && "unknown buffer type");
	}
	return gos_render::BufferKind::Vertex;
}

static gos_render::BufferUsage toBufferUsage(gosBUFFER_USAGE usage)
{
	switch (usage)
	{
		case gosBUFFER_USAGE::STREAM_DRAW: return gos_render::BufferUsage::Stream;
		case gosBUFFER_USAGE::STATIC_DRAW: return gos_render::BufferUsage::Static;
		case gosBUFFER_USAGE::DYNAMIC_DRAW: return gos_render::BufferUsage::Dynamic;
		default:
			gosASSERT(0 && "unknown buffer usage");
	}
	return gos_render::BufferUsage::Dynamic;
}

gosBuffer* __stdcall gos_CreateBuffer(gosBUFFER_TYPE type, gosBUFFER_USAGE usage, int element_size, uint32_t count, void* buffer_data)
{
	gosBuffer* pbuffer = new gosBuffer();
	pbuffer->buffer_ = gos_render::device()->createBuffer(toBufferKind(type), toBufferUsage(usage),
	                                                       buffer_data, (size_t)element_size * count);
	pbuffer->element_size_ = element_size;
	pbuffer->count_ = count;
	pbuffer->type_ = type;
	pbuffer->usage_ = usage;

    gosASSERT(g_gos_renderer);
	g_gos_renderer->addBuffer(pbuffer);

	return pbuffer;
}

uint32_t gos_GetBufferSizeBytes(HGOSBUFFER buffer)
{
	gosASSERT(buffer);
    return buffer->element_size_ * buffer->count_;
}

void __stdcall gos_DestroyBuffer(gosBuffer* buffer)
{
	gosASSERT(buffer);
    gosASSERT(g_gos_renderer);
	bool rv = g_gos_renderer->deleteBuffer(buffer);
    (void)rv;
	gos_render::device()->destroyBuffer(buffer->buffer_); // used to leak
	delete buffer;
}

void __stdcall gos_BindBufferBase(gosBuffer* buffer, uint32_t slot)
{
	gosASSERT(buffer);

	gosASSERT(buffer->type_ == gosBUFFER_TYPE::UNIFORM);
	gos_render::device()->bindUniformBuffer((int)slot, buffer->buffer_);
}

void __stdcall gos_UpdateBuffer(HGOSBUFFER buffer, void* data, size_t offset, size_t num_bytes)
{
	gosASSERT(buffer);
    gosASSERT(buffer->element_size_ * buffer->count_ >= num_bytes);
	(void)offset; // whole buffer replaced, as before
	g_gos_renderer->flushBatch(); // pending draws see the old contents
	gos_render::device()->updateBuffer(buffer->buffer_, toBufferKind(buffer->type_), data, num_bytes);
}

HGOSVERTEXDECLARATION __stdcall gos_CreateVertexDeclaration(gosVERTEX_FORMAT_RECORD* records, int count)
{
	gosASSERT(records && count > 0);
    gosASSERT(g_gos_renderer);
	gosVertexDeclaration* vdecl = gosVertexDeclaration::create(records, count);
	g_gos_renderer->addVertexDeclaration(vdecl);
	return vdecl;
}

void __stdcall gos_DestroyVertexDeclaration(HGOSVERTEXDECLARATION vdecl)
{
	gosASSERT(vdecl);
    gosASSERT(g_gos_renderer);
	bool rv = g_gos_renderer->deleteVertexDeclaration(vdecl);
    (void)rv;
	gosVertexDeclaration::destroy(vdecl);

}

HGOSRENDERMATERIAL __stdcall gos_getRenderMaterial(const char* material)
{
	gosASSERT(material);
	gosASSERT(g_gos_renderer);
	return g_gos_renderer->getRenderMaterial(material);
}

void __stdcall gos_ApplyRenderMaterial(HGOSRENDERMATERIAL material)
{
	gosASSERT(material);

	//setup commoin stuff
	gos_SetCommonMaterialParameters(material);

	material->apply();
	material->setSamplerUnit(gosMesh::s_tex1, 0);
	material->setUniformBlock("lights_data", 0);
}

void __stdcall gos_SetRenderMaterialParameterFloat4(HGOSRENDERMATERIAL material, const char* name, const float* v)
{
	gosASSERT(material);
	gosASSERT(v);
	material->getShader()->setFloat4(name, v);
}

void __stdcall gos_SetRenderMaterialParameterMat4(HGOSRENDERMATERIAL material, const char* name, const float* m)
{
	gosASSERT(material);
	gosASSERT(m);
	material->getShader()->setMat4(name, m);
}

void __stdcall gos_SetRenderMaterialUniformBlockBindingPoint(HGOSRENDERMATERIAL material, const char* name, uint32_t slot)
{
	gosASSERT(material && name);
	material->setUniformBlock(name, slot);
}

void __stdcall gos_SetCommonMaterialParameters(HGOSRENDERMATERIAL material)
{
	gosASSERT(material);
	gosASSERT(g_gos_renderer);

	const mat4& projection = getGosRenderer()->getProj2Screen();
	const vec4& vp = getGosRenderer()->getRenderViewport();

	// TODO: make typed parameters !!!!!!!!!!!!!!! not just float* pointers, helps track errors
	gos_SetRenderMaterialParameterMat4(material, "projection_", projection);
	gos_SetRenderMaterialParameterFloat4(material, "vp", vp);
}


#include "gameos_graphics_debug.cpp"
