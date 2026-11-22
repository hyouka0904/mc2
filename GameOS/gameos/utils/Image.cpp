#include "utils/Image.h"

#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include <cassert>

//#include "math/vec.h"
//#include "system/types.h"
//#include "system/defines.h"


#ifdef _WIN32
#pragma warning(disable : 4996)
#else
#define stricmp strcasecmp
#endif


void Image::clear(){
	delete[] pixels;
	pixels = NULL;
	width = height = 0;
	format = FORMAT_NONE;
}

// Pack header tightly
#pragma pack (push, 1)

struct TGAHeader {
	unsigned char  descLen;
	unsigned char  cmapType;
	unsigned char  imageType;
	unsigned short cmapStart;
	unsigned short cmapEntries;
	unsigned char  cmapBits;
	unsigned short xOffset;
	unsigned short yOffset;
	unsigned short width;
	unsigned short height;
	unsigned char  bpp;
	unsigned char  attrib;
};

struct BMPHeader {
	unsigned short bfType;
	unsigned int   bfSize;
	unsigned short bfReserved1;
	unsigned short bfReserved2;
	unsigned int   bfOffBits;
	unsigned int   biSize;
	unsigned int   biWidth;
	unsigned int   biHeight;
	unsigned short biPlanes;
	unsigned short biBitCount;
	unsigned int   biCompression;
	unsigned int   biSizeImage;
	unsigned int   biXPelsPerMeter;
	unsigned int   biYPelsPerMeter;
	unsigned int   biClrUsed;
	unsigned int   biClrImportant;
};

enum class TGADataType {
	kNoImageData = 0,
	kUncompressedColorMapped = 1,
	kUncompressedRGB = 2,
	kUncompressedBlackAndWhite = 3,
	kRLEColorMapped = 9,
	kRLERGB = 10,
	kCompressedBlackAndWhite = 11,
	kCompressedColorMappedHuffman = 32,
	kCompressedColorMappedHuffman4PassQuadTree = 33,
};

template <typename DATA_TYPE>
inline void swapChannels(DATA_TYPE *pixels, int nPixels, const int channels, const int ch0, const int ch1){
	do {
		DATA_TYPE tmp = pixels[ch1];
		pixels[ch1] = pixels[ch0];
		pixels[ch0] = tmp;
		pixels += channels;
	} while (--nPixels);
}


static FORMAT getFormatFromBpp(const unsigned int bpp) {
	switch (bpp) {
	case 8:
		return FORMAT_I8;
		break;
	case 16:
		return FORMAT_RGBA8;
		break;
	case 24:
		return FORMAT_RGB8;
		break;
	case 32:
		return FORMAT_RGBA8;
		break;
    default:
        assert(0 && "bad bpp");
        return FORMAT_RGBA8;
	}
}

Image::Image(void)
{
	pixels = 0;
	width = height = 0;
	format = FORMAT_NONE;
}

Image::~Image(void)
{
	delete[] pixels;
}

unsigned char *Image::create(const FORMAT fmt, const int w, const int h){
	format = fmt;
	width  = w;
	height = h;

	int size = w*h;
	delete[] pixels;
	pixels = new unsigned char[size];

	return pixels;
}

bool Image::loadFromFile(const char *fileName){
	const char *ext = strrchr(fileName, '.');
	if (ext == NULL) return false;

	ext++;
	if (stricmp(ext, "tga") == 0)
	{
		return loadTGA(fileName);
	}
	else if (stricmp(ext, "bmp") == 0)
	{
		return loadBMP(fileName);
	}
	return false;
} 

bool Image::loadTGA(const char *fileName){
	clear();

	FILE *file;

	if ((file = fopen(fileName, "rb")) == NULL) return false;
	return loadTGA(file);
}

bool Image::loadTGA(FILE* file)
{
	TGAHeader header;

	if (fread(&header, sizeof(header), 1, file) != 1) {
		fclose(file);
		return false;
	}
	// Skip past the description if there's any
	if (header.descLen) fseek(file, header.descLen, SEEK_CUR);

	int pixelSize = header.bpp / 8;

	bool rv = false;
	unsigned char *readPixels = 0;

	if (header.imageType == (unsigned char)TGADataType::kUncompressedRGB) {

		int size = header.width * header.height * pixelSize;
		readPixels = new unsigned char[size];
		if (fread(readPixels, size, 1, file) == 1)
			rv = loadTGA(&header, readPixels);
	}
	else if(header.imageType == (unsigned char)TGADataType::kRLERGB) {

		size_t fpos = ftell(file);
		fseek(file, 0, SEEK_END);
		size_t fsize = ftell(file) - fpos;
		fseek(file, fpos, SEEK_SET);

		readPixels = new unsigned char[fsize];
		if (fread(readPixels, fsize, 1, file) == 1)
			rv = loadCompressedTGA(&header, readPixels, fsize);
	}

	fclose(file);
	delete[] readPixels;

    return rv;
}

bool Image::loadTGA(const unsigned char* mem, size_t len)
{
    assert(mem);
    assert(len > sizeof(TGAHeader));

	TGAHeader header;

	memcpy(&header, mem, sizeof(header));
    mem += sizeof(header);
    len -= sizeof(header);

	// Skip past the description if there's any
	if (header.descLen) {
        mem += header.descLen;
        len -= header.descLen;
    }

	unsigned int pixelSize = header.bpp / 8;
	int size = header.width * header.height * pixelSize;

	unsigned char *readPixels = 0;
	bool rv = false;


	if (header.imageType == (unsigned char)TGADataType::kUncompressedRGB) {
		assert(len >= header.width * header.height * pixelSize);
		readPixels = new unsigned char[size];
		memcpy(readPixels, mem, size);
		rv = loadTGA(&header, readPixels);
		delete[] readPixels;
	}
	else if(header.imageType == (unsigned char)TGADataType::kRLERGB) {
		rv = loadCompressedTGA(&header, mem, len);
	}

    return rv;
}

bool Image::loadTGA(const TGAHeader* header, unsigned char* readPixels)
{
	delete[] pixels; // the image may be reused for another load
	pixels = NULL;
	width  = header->width;
	height = header->height;

	format = getFormatFromBpp(header->bpp);

	int pixelSize = header->bpp / 8;

	unsigned char *dest, *src = readPixels + width * (height - 1) * pixelSize;

	int x, y;
	switch (header->bpp) {
		case 8:
			dest = pixels = new unsigned char[width * height];
			for (y = 0; y < height; y++){
				memcpy(dest, src, width);
				dest += width;
				src -= width;
			}
			break;
		case 16:
			dest = pixels = new unsigned char[width * height * 4];
			for (y = 0; y < height; y++){
				for (x = 0; x < width; x++){
					unsigned short tempPixel = *((unsigned short *) src);

					dest[0] = ((tempPixel >> 10) & 0x1F) << 3;
					dest[1] = ((tempPixel >>  5) & 0x1F) << 3;
					dest[2] = ((tempPixel      ) & 0x1F) << 3;
					dest[3] = ((tempPixel >> 15) ? 0xFF : 0);
					dest += 4;
					src += 2;
				}
				src -= 4 * width;
			}
			break;
		case 24:
			dest = pixels = new unsigned char[width * height * 3];
			for (y = 0; y < height; y++){
				for (x = 0; x < width; x++){
					*dest++ = src[2];
					*dest++ = src[1];
					*dest++ = src[0];
					src += 3;
				}
				src -= 6 * width;
			}
			break;
		case 32:
			dest = pixels = new unsigned char[width * height * 4];
			for (y = 0; y < height; y++){
				for (x = 0; x < width; x++){
					*dest++ = src[2];
					*dest++ = src[1];
					*dest++ = src[0];
					*dest++ = src[3];
					src += 4;
				}
				src -= 8 * width;
			}
			break;
	}

	if (header->attrib & 0x20) flip();

	return true;
}


static void mergeBytes(unsigned char *dest, const unsigned char *src, int pixelSize)
{
	if (pixelSize == 1) {
		*dest = *src;
	}
	else if (pixelSize == 4) {
		*dest++ = src[2];
		*dest++ = src[1];
		*dest++ = src[0];
		*dest++ = src[3];
	}
	else if (pixelSize == 3) {
		*dest++ = src[2];
		*dest++ = src[1];
		*dest++ = src[0];
	}
	else if (pixelSize == 2) {
		const unsigned short tempPixel = (unsigned short)(src[0] | (src[1] << 8));
		dest[0] = ((tempPixel >> 10) & 0x1F) << 3;
		dest[1] = ((tempPixel >> 5) & 0x1F) << 3;
		dest[2] = ((tempPixel) & 0x1F) << 3;
		dest[3] = ((tempPixel >> 15) ? 0xFF : 0);
	}
	else {
		assert(0 && "Unsupported pixelSize");
	}
}

// Decodes an RLE-compressed (type 10) TGA. The result has the same layout as
// the uncompressed path in loadTGA(): top row first, channels swizzled from
// BGR(A) to RGB(A), 16-bit pixels expanded to RGBA8.
//
// Each packet starts with a header byte: the low 7 bits hold (count - 1); if
// the high bit is set the next pixel is repeated `count` times (run-length
// packet), otherwise `count` raw pixels follow.
bool Image::loadCompressedTGA(const TGAHeader* header, const unsigned char* mem, size_t len) {

	const int srcPixelSize = header->bpp / 8;
	if (srcPixelSize < 1 || srcPixelSize > 4)
		return false;

	const FORMAT fmt = getFormatFromBpp(header->bpp);
	const int dstPixelSize = getBytesPerPixel(fmt);
	const size_t numPixels = (size_t)header->width * header->height;

	unsigned char* decoded = new unsigned char[numPixels * dstPixelSize];
	size_t pixel = 0;
	size_t offset = 0;

	while (pixel < numPixels) {
		if (offset >= len)
			break;
		const unsigned char packet = mem[offset++];
		const size_t count = (size_t)(packet & 0x7f) + 1;
		const bool isRun = (packet & 0x80) != 0;

		// Never write past the image, even if the data claims more pixels.
		if (count > numPixels - pixel)
			break;

		if (isRun) {
			if (len - offset < (size_t)srcPixelSize)
				break;
			for (size_t i = 0; i < count; i++)
				mergeBytes(decoded + (pixel + i) * dstPixelSize, mem + offset, srcPixelSize);
			offset += srcPixelSize;
		}
		else {
			if (len - offset < count * srcPixelSize)
				break;
			for (size_t i = 0; i < count; i++)
				mergeBytes(decoded + (pixel + i) * dstPixelSize, mem + offset + i * srcPixelSize, srcPixelSize);
			offset += count * srcPixelSize;
		}
		pixel += count;
	}

	if (pixel != numPixels) {
		delete[] decoded;
		return false;
	}

	delete[] pixels;
	pixels = decoded;
	width  = header->width;
	height = header->height;
	format = fmt;

	// Pixels are now in file order; bottom-up files (the default, attribute
	// bit 5 clear) need flipping to match the uncompressed path.
	if (!(header->attrib & 0x20)) flip();

	return true;
}

bool Image::loadBMP(const char *fileName){
	clear();

	FILE *file = fopen(fileName, "rb");
	if (file == NULL) return false;

	return loadBMP(file);
}

bool Image::loadBMP(FILE* file)
{
	// Read the header
	BMPHeader header;
	if (fread(&header, sizeof(header), 1, file) != 1 || header.bfType != 0x4D42){
		fclose(file);
		return false;
	}

    // seek to start of image data
    fseek(file, header.bfOffBits, SEEK_SET);

	width    = header.biWidth;
	height   = header.biHeight;

	switch (header.biBitCount){
		case 8:
			// No support for RLE compressed bitmaps
			if (header.biCompression){
				fclose(file);
				return false;
			}
			format = FORMAT_RGB8;
			pixels = new unsigned char[width * height * 3];

			unsigned char palette[1024];
			if (fread(palette, sizeof(palette), 1, file) != 1)
				memset(palette, 0, sizeof(palette));

			// 8-bit paletted bitmap
			unsigned char *tmp;
			tmp = new unsigned char[width];
			for (int i = height - 1; i >= 0; i--){
				unsigned char *dest = pixels + i * width * 3;
				if (fread(tmp, width, 1, file) != 1)
					memset(tmp, 0, width);
				int len = width;
				do {
					unsigned char *src = palette + ((*tmp++) << 2);
					*dest++ = src[2];
					*dest++ = src[1];
					*dest++ = src[0];
				} while (--len);
				tmp -= width;
			}
			delete[] tmp;
			break;
		case 24:
		case 32:
			int nChannels;
			if (header.biBitCount == 24){
				nChannels = 3;
				format = FORMAT_RGB8;
			} else {
				nChannels = 4;
				format = FORMAT_RGBA8;
			}
			pixels = new unsigned char[width * height * nChannels];
			for (int i = height - 1; i >= 0; i--){
				unsigned char *dest = pixels + i * width * nChannels;
				if (fread(dest, width * nChannels, 1, file) != 1)
					memset(dest, 0, width * nChannels);
				swapChannels(dest, width, nChannels, 0, 2);
			}
			break;
		default:
			fclose(file);
			return false;
	}

	fclose(file);

	return true;
}


bool Image::flip(){

	int lineWidth = getBytesPerPixel(format) * width;
	unsigned char *newPixels = new unsigned char[lineWidth * height];

	for (int i = 0; i < height; i++){
		memcpy(newPixels + i * lineWidth, pixels + (height - 1 - i) * lineWidth, lineWidth);
	}

	delete[] pixels;
	pixels = newPixels;

	return true;
}
