/**
 * Copyright (c) 2006-2021 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#include "ImageData.h"
#include "Image.h"
#include "filesystem/Filesystem.h"
#include "medialoader/image/pixelformat.h"

#include "common/Exception.h"

#include <algorithm>  // min/max
#include <cmath>
#include <cstring>    // memcpy

using namespace medialoader;

namespace eve {
namespace image {

ImageData::ImageData(Data *data) : Resource(""), decodeHandler(nullptr) { decode(data); }

ImageData::ImageData(int width, int height, std::string format) : Resource(""), width(width), height(height), format(format), decodeHandler(nullptr) {
    if (!validPixelFormat(format)) throw eve::Exception("Unsupported pixel format for ImageData");

    create(width, height, format);

    // Set to black/transparency.
    memset(data, 0, getSize());
}

ImageData::ImageData(int width, int height, std::string format, void *data, bool own)
    : Resource(""), width(width), height(height), format(format), decodeHandler(nullptr)
{
    if (!validPixelFormat(format)) throw eve::Exception("Unsupported pixel format for ImageData");

    if (own)
        this->data = (unsigned char *)data;
    else
        create(width, height, format, data);
}

ImageData::ImageData(const ImageData &c) : Resource(c.getUri()), decodeHandler(nullptr) {
    width  = c.width;   
    height = c.height;
    format = c.format;

    create(width, height, format, c.getData());
}

ImageData::~ImageData() {
    if (decodeHandler)
        decodeHandler->freeRawPixels(data);
    else
        delete[] data;
}

eve::image::ImageData *ImageData::clone() const {
    try {
        return new ImageData(*this);
    } catch (std::bad_alloc &) {
        throw eve::Exception("Out of memory");
    }
}

void ImageData::create(int width, int height, std::string format, void *data) {
    size_t datasize = width * height * getPixelFormatSize(getPixelFormatFromName(format));

    try {
        this->data = new unsigned char[datasize];
    } catch (std::bad_alloc &) {
        throw eve::Exception("Out of memory");
    }

    if (data) memcpy(this->data, data, datasize);

    decodeHandler = nullptr;
    this->format  = format;

    pixelSetFunction = getPixelSetFunction(format);
    pixelGetFunction = getPixelGetFunction(format);
}

void ImageData::decode(Data *data) {
    FormatHandler              *decoder = nullptr;
    FormatHandler::DecodedImage decodedimage;

    auto module = Image::create();

    if (module == nullptr) throw eve::Exception("eve.image must be loaded in order to decode an ImageData.");

    for (FormatHandler *handler : module->getFormatHandlers()) {
        if (handler->canDecode((const char*)data->getData(), data->getSize())) {
            decoder = handler;
            break;
        }
    }

    if (decoder) decodedimage = decoder->decode((const char*)data->getData(), data->getSize());

    if (decodedimage.data == nullptr) {
        auto filedata = dynamic_cast<filesystem::FileData *>(data);

        if (filedata != nullptr) {
            const std::string &name = filedata->getFilename();
            throw eve::Exception("Could not decode file '%s' to ImageData: unsupported file format", name.c_str());
        } else
            throw eve::Exception("Could not decode data to ImageData: unsupported encoded format");
    }

    if (decodedimage.size != decodedimage.width * decodedimage.height * getPixelFormatSize(decodedimage.format)) {
        decoder->freeRawPixels(decodedimage.data);
        throw eve::Exception("Could not convert image!");
    }

    // Clean up any old data.
    if (decodeHandler)
        decodeHandler->freeRawPixels(this->data);
    else
        delete[] this->data;

    this->width  = decodedimage.width;
    this->height = decodedimage.height;
    this->data   = decodedimage.data;
    // PixelFormat enum must not be assigned into std::string (that becomes a single char).
    this->format = [pf = decodedimage.format]() -> std::string {
        switch (pf) {
            case PIXELFORMAT_R8: return "R8";
            case PIXELFORMAT_RG8: return "RG8";
            case PIXELFORMAT_RGBA8:
            case PIXELFORMAT_sRGBA8: return "RGBA8";
            case PIXELFORMAT_R16: return "R16";
            case PIXELFORMAT_RG16: return "RG16";
            case PIXELFORMAT_RGBA16: return "RGBA16";
            case PIXELFORMAT_R16F: return "R16F";
            case PIXELFORMAT_RG16F: return "RG16F";
            case PIXELFORMAT_RGBA16F: return "RGBA16F";
            case PIXELFORMAT_R32F: return "R32F";
            case PIXELFORMAT_RG32F: return "RG32F";
            case PIXELFORMAT_RGBA32F: return "RGBA32F";
            case PIXELFORMAT_RGBA4: return "RGBA4";
            case PIXELFORMAT_RGB5A1: return "RGB5A1";
            case PIXELFORMAT_RGB565: return "RGB565";
            case PIXELFORMAT_RGB10A2: return "RGB10A2";
            case PIXELFORMAT_RG11B10F: return "RG11B10F";
            default: return "RGBA8";
        }
    }();

    decodeHandler = decoder;

    pixelSetFunction = getPixelSetFunction(format);
    pixelGetFunction = getPixelGetFunction(format);
}

eve::filesystem::FileData *ImageData::encode(FormatHandler::EncodedFormat encodedFormat, const char *filename,
                                             bool writefile) const {
    FormatHandler              *encoder = nullptr;
    FormatHandler::EncodedImage encodedimage;
    FormatHandler::DecodedImage rawimage;

    rawimage.width  = width;
    rawimage.height = height;
    rawimage.size   = getSize();
    rawimage.data   = data;
    rawimage.format = getPixelFormatFromName(format);

    auto module = Image::create();

    if (module == nullptr) throw eve::Exception("eve.image must be loaded in order to encode an ImageData.");

    for (FormatHandler *handler : module->getFormatHandlers()) {
        if (handler->canEncode(getPixelFormatFromName(format), encodedFormat)) {
            encoder = handler;
            break;
        }
    }

    if (encoder != nullptr) {
        encodedimage = encoder->encode(rawimage, encodedFormat);
    }

    if (encoder == nullptr || encodedimage.data == nullptr) {
        throw eve::Exception("No suitable image encoder for %s format.", format.c_str());
    }

    eve::filesystem::FileData *filedata = nullptr;

    try {
        filedata = new eve::filesystem::FileData(filename, encodedimage.size);
    } catch (eve::Exception &) {
        encoder->freeRawPixels(encodedimage.data);
        throw;
    }

    memcpy(filedata->getData(), encodedimage.data, encodedimage.size);
    encoder->freeRawPixels(encodedimage.data);

    if (writefile) {
        auto fs = eve::filesystem::Filesystem::create();

        if (fs == nullptr) {
            // filedata->release();
            throw eve::Exception("eve.filesystem must be loaded in order to write an encoded ImageData to a file.");
        }

        try {
            fs->write(filename, filedata->getData(), filedata->getSize());
        } catch (eve::Exception &) {
            // filedata->release();
            throw;
        }
    }

    return filedata;
}

size_t ImageData::getSize() const { return size_t(getWidth() * getHeight()) * getPixelSize(); }

void *ImageData::getData() const { return data; }

bool ImageData::isSRGB() const { return false; }

bool ImageData::inside(int x, int y) const { return x >= 0 && x < getWidth() && y >= 0 && y < getHeight(); }

int ImageData::getWidth() const { return width; }

int ImageData::getHeight() const { return height; }

std::string ImageData::getFormat() const { return format; }

static float clamp01(float x) { return std::min(std::max(x, 0.0f), 1.0f); }

static void setPixelR8(const Colorf &c, ImageData::Pixel *p) { p->rgba8[0] = (uint8_t)(clamp01(c.r) * 255.0f + 0.5f); }

static void setPixelRG8(const Colorf &c, ImageData::Pixel *p) {
    p->rgba8[0] = (uint8_t)(clamp01(c.r) * 255.0f + 0.5f);
    p->rgba8[1] = (uint8_t)(clamp01(c.g) * 255.0f + 0.5f);
}

static void setPixelRGBA8(const Colorf &c, ImageData::Pixel *p) {
    p->rgba8[0] = (uint8_t)(clamp01(c.r) * 255.0f + 0.5f);
    p->rgba8[1] = (uint8_t)(clamp01(c.g) * 255.0f + 0.5f);
    p->rgba8[2] = (uint8_t)(clamp01(c.b) * 255.0f + 0.5f);
    p->rgba8[3] = (uint8_t)(clamp01(c.a) * 255.0f + 0.5f);
}

static void setPixelR16(const Colorf &c, ImageData::Pixel *p) {
    p->rgba16[0] = (uint16_t)(clamp01(c.r) * 65535.0f + 0.5f);
}

static void setPixelRG16(const Colorf &c, ImageData::Pixel *p) {
    p->rgba16[0] = (uint16_t)(clamp01(c.r) * 65535.0f + 0.5f);
    p->rgba16[1] = (uint16_t)(clamp01(c.g) * 65535.0f + 0.5f);
}

static void setPixelRGBA16(const Colorf &c, ImageData::Pixel *p) {
    p->rgba16[0] = (uint16_t)(clamp01(c.r) * 65535.0f + 0.5f);
    p->rgba16[1] = (uint16_t)(clamp01(c.b) * 65535.0f + 0.5f);
    p->rgba16[2] = (uint16_t)(clamp01(c.g) * 65535.0f + 0.5f);
    p->rgba16[3] = (uint16_t)(clamp01(c.a) * 65535.0f + 0.5f);
}

static void setPixelR16F(const Colorf &c, ImageData::Pixel *p) { p->rgba16f[0] = float32to16(c.r); }

static void setPixelRG16F(const Colorf &c, ImageData::Pixel *p) {
    p->rgba16f[0] = float32to16(c.r);
    p->rgba16f[1] = float32to16(c.g);
}

static void setPixelRGBA16F(const Colorf &c, ImageData::Pixel *p) {
    p->rgba16f[0] = float32to16(c.r);
    p->rgba16f[1] = float32to16(c.g);
    p->rgba16f[2] = float32to16(c.b);
    p->rgba16f[3] = float32to16(c.a);
}

static void setPixelR32F(const Colorf &c, ImageData::Pixel *p) { p->rgba32f[0] = c.r; }

static void setPixelRG32F(const Colorf &c, ImageData::Pixel *p) {
    p->rgba32f[0] = c.r;
    p->rgba32f[1] = c.g;
}

static void setPixelRGBA32F(const Colorf &c, ImageData::Pixel *p) {
    p->rgba32f[0] = c.r;
    p->rgba32f[1] = c.g;
    p->rgba32f[2] = c.b;
    p->rgba32f[3] = c.a;
}

static void setPixelRGBA4(const Colorf &c, ImageData::Pixel *p) {
    // LSB->MSB: [a, b, g, r]
    uint16_t r    = (uint16_t)(clamp01(c.r) * 0xF + 0.5);
    uint16_t g    = (uint16_t)(clamp01(c.g) * 0xF + 0.5);
    uint16_t b    = (uint16_t)(clamp01(c.b) * 0xF + 0.5);
    uint16_t a    = (uint16_t)(clamp01(c.a) * 0xF + 0.5);
    p->packed16 = (r << 12) | (g << 8) | (b << 4) | (a << 0);
}

static void setPixelRGB5A1(const Colorf &c, ImageData::Pixel *p) {
    // LSB->MSB: [a, b, g, r]
    uint16_t r    = (uint16_t)(clamp01(c.r) * 0x1F + 0.5);
    uint16_t g    = (uint16_t)(clamp01(c.g) * 0x1F + 0.5);
    uint16_t b    = (uint16_t)(clamp01(c.b) * 0x1F + 0.5);
    uint16_t a    = (uint16_t)(clamp01(c.a) * 0x1 + 0.5);
    p->packed16 = (r << 11) | (g << 6) | (b << 1) | (a << 0);
}

static void setPixelRGB565(const Colorf &c, ImageData::Pixel *p) {
    // LSB->MSB: [b, g, r]
    uint16_t r    = (uint16_t)(clamp01(c.r) * 0x1F + 0.5);
    uint16_t g    = (uint16_t)(clamp01(c.g) * 0x3F + 0.5);
    uint16_t b    = (uint16_t)(clamp01(c.b) * 0x1F + 0.5);
    p->packed16 = (r << 11) | (g << 5) | (b << 0);
}

static void setPixelRGB10A2(const Colorf &c, ImageData::Pixel *p) {
    // LSB->MSB: [r, g, b, a]
    uint32_t r    = (uint32_t)(clamp01(c.r) * 0x3FF + 0.5);
    uint32_t g    = (uint32_t)(clamp01(c.g) * 0x3FF + 0.5);
    uint32_t b    = (uint32_t)(clamp01(c.b) * 0x3FF + 0.5);
    uint32_t a    = (uint32_t)(clamp01(c.a) * 0x3 + 0.5);
    p->packed32 = (r << 0) | (g << 10) | (b << 20) | (a << 30);
}

static void setPixelRG11B10F(const Colorf &c, ImageData::Pixel *p) {
    // LSB->MSB: [r, g, b]
    float11 r   = float32to11(c.r);
    float11 g   = float32to11(c.g);
    float10 b   = float32to10(c.b);
    p->packed32 = (r << 0) | (g << 11) | (b << 22);
}

static void getPixelR8(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba8[0] / 255.0f;
    c.g = 0.0f;
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRG8(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba8[0] / 255.0f;
    c.g = p->rgba8[1] / 255.0f;
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRGBA8(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba8[0] / 255.0f;
    c.g = p->rgba8[1] / 255.0f;
    c.b = p->rgba8[2] / 255.0f;
    c.a = p->rgba8[3] / 255.0f;
}

static void getPixelR16(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba16[0] / 65535.0f;
    c.g = 0.0f;
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRG16(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba16[0] / 65535.0f;
    c.g = p->rgba16[1] / 65535.0f;
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRGBA16(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba16[0] / 65535.0f;
    c.g = p->rgba16[1] / 65535.0f;
    c.b = p->rgba16[2] / 65535.0f;
    c.a = p->rgba16[3] / 65535.0f;
}

static void getPixelR16F(const ImageData::Pixel *p, Colorf &c) {
    c.r = float16to32(p->rgba16f[0]);
    c.g = 0.0f;
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRG16F(const ImageData::Pixel *p, Colorf &c) {
    c.r = float16to32(p->rgba16f[0]);
    c.g = float16to32(p->rgba16f[1]);
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRGBA16F(const ImageData::Pixel *p, Colorf &c) {
    c.r = float16to32(p->rgba16f[0]);
    c.g = float16to32(p->rgba16f[1]);
    c.b = float16to32(p->rgba16f[2]);
    c.a = float16to32(p->rgba16f[3]);
}

static void getPixelR32F(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba32f[0];
    c.g = 0.0f;
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRG32F(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba32f[0];
    c.g = p->rgba32f[1];
    c.b = 0.0f;
    c.a = 1.0f;
}

static void getPixelRGBA32F(const ImageData::Pixel *p, Colorf &c) {
    c.r = p->rgba32f[0];
    c.g = p->rgba32f[1];
    c.b = p->rgba32f[2];
    c.a = p->rgba32f[3];
}

static void getPixelRGBA4(const ImageData::Pixel *p, Colorf &c) {
    // LSB->MSB: [a, b, g, r]
    c.r = ((p->packed16 >> 12) & 0xF) / (float)0xF;
    c.g = ((p->packed16 >> 8) & 0xF) / (float)0xF;
    c.b = ((p->packed16 >> 4) & 0xF) / (float)0xF;
    c.a = ((p->packed16 >> 0) & 0xF) / (float)0xF;
}

static void getPixelRGB5A1(const ImageData::Pixel *p, Colorf &c) {
    // LSB->MSB: [a, b, g, r]
    c.r = ((p->packed16 >> 11) & 0x1F) / (float)0x1F;
    c.g = ((p->packed16 >> 6) & 0x1F) / (float)0x1F;
    c.b = ((p->packed16 >> 1) & 0x1F) / (float)0x1F;
    c.a = ((p->packed16 >> 0) & 0x1) / (float)0x1;
}

static void getPixelRGB565(const ImageData::Pixel *p, Colorf &c) {
    // LSB->MSB: [b, g, r]
    c.r = ((p->packed16 >> 11) & 0x1F) / (float)0x1F;
    c.g = ((p->packed16 >> 5) & 0x3F) / (float)0x3F;
    c.b = ((p->packed16 >> 0) & 0x1F) / (float)0x1F;
    c.a = 1.0f;
}

static void getPixelRGB10A2(const ImageData::Pixel *p, Colorf &c) {
    // LSB->MSB: [r, g, b, a]
    c.r = ((p->packed32 >> 0) & 0x3FF) / (float)0x3FF;
    c.g = ((p->packed32 >> 10) & 0x3FF) / (float)0x3FF;
    c.b = ((p->packed32 >> 20) & 0x3FF) / (float)0x3FF;
    c.a = ((p->packed32 >> 30) & 0x3) / (float)0x3;
}

static void getPixelRG11B10F(const ImageData::Pixel *p, Colorf &c) {
    // LSB->MSB: [r, g, b]
    c.r = float11to32((float11)((p->packed32 >> 0) & 0x7FF));
    c.g = float11to32((float11)((p->packed32 >> 11) & 0x7FF));
    c.b = float10to32((float10)((p->packed32 >> 22) & 0x3FF));
    c.a = 1.0f;
}

void ImageData::setPixel(int x, int y, const Colorf &c) {
    if (!inside(x, y)) throw eve::Exception("Attempt to set out-of-range pixel!");

    size_t pixelsize = getPixelSize();
    Pixel *p         = (Pixel *)(data + ((y * width + x) * pixelsize));

    if (pixelSetFunction == nullptr) throw eve::Exception("Unhandled pixel format %s in ImageData::setPixel", format.c_str());

    pixelSetFunction(c, p);
}

void ImageData::getPixel(int x, int y, Colorf &c) const {
    if (!inside(x, y)) throw eve::Exception("Attempt to get out-of-range pixel!");

    size_t       pixelsize = getPixelSize();
    const Pixel *p         = (const Pixel *)(data + ((y * width + x) * pixelsize));

    if (pixelGetFunction == nullptr) throw eve::Exception("Unhandled pixel format %s in ImageData::getPixel", format.c_str());

    pixelGetFunction(p, c);
}

Colorf ImageData::getPixel(int x, int y) const {
    Colorf c;
    getPixel(x, y, c);
    return c;
}

namespace {

enum class RotateFilter {
	Nearest,
	Linear,
};

RotateFilter parseRotateFilter(const std::string &name) {
	if (name == "nearest" || name == "Nearest" || name == "NEAREST")
		return RotateFilter::Nearest;
	if (name == "linear" || name == "Linear" || name == "LINEAR" ||
	    name == "bilinear" || name == "Bilinear" || name == "BILINEAR")
		return RotateFilter::Linear;
	throw eve::Exception("Unsupported ImageData rotate filter '%s' (use \"nearest\" or \"linear\")",
	                     name.c_str());
}

void rotatePoint(float dx, float dy, float c, float s, float &ox, float &oy) {
	// Same convention as Math::rotate2X/Y.
	ox = dx * c - dy * s;
	oy = dx * s + dy * c;
}

void inverseRotatePoint(float dx, float dy, float c, float s, float &ox, float &oy) {
	// Inverse of Math::rotate2*: apply -angle (cos'=c, sin'=-s).
	ox = dx * c + dy * s;
	oy = -dx * s + dy * c;
}

Colorf sampleNearest(const ImageData *src, float sx, float sy) {
	const int rx = (int)std::floor(sx + 0.5f);
	const int ry = (int)std::floor(sy + 0.5f);
	if (!src->inside(rx, ry))
		return Colorf{0.f, 0.f, 0.f, 0.f};
	return src->getPixel(rx, ry);
}

Colorf sampleBilinear(const ImageData *src, float sx, float sy) {
	const int x0 = (int)std::floor(sx);
	const int y0 = (int)std::floor(sy);
	const int x1 = x0 + 1;
	const int y1 = y0 + 1;
	const float fx = sx - (float)x0;
	const float fy = sy - (float)y0;
	const float ifx = 1.f - fx;
	const float ify = 1.f - fy;

	auto sampleOrZero = [&](int x, int y) -> Colorf {
		if (!src->inside(x, y))
			return Colorf{0.f, 0.f, 0.f, 0.f};
		return src->getPixel(x, y);
	};

	const Colorf c00 = sampleOrZero(x0, y0);
	const Colorf c10 = sampleOrZero(x1, y0);
	const Colorf c01 = sampleOrZero(x0, y1);
	const Colorf c11 = sampleOrZero(x1, y1);

	const float w00 = ifx * ify;
	const float w10 = fx * ify;
	const float w01 = ifx * fy;
	const float w11 = fx * fy;

	Colorf out;
	out.r = w00 * c00.r + w10 * c10.r + w01 * c01.r + w11 * c11.r;
	out.g = w00 * c00.g + w10 * c10.g + w01 * c01.g + w11 * c11.g;
	out.b = w00 * c00.b + w10 * c10.b + w01 * c01.b + w11 * c11.b;
	out.a = w00 * c00.a + w10 * c10.a + w01 * c01.a + w11 * c11.a;
	return out;
}

} // namespace

ImageData *ImageData::rotate(float radians, std::string filter, bool expand) const {
	if (pixelGetFunction == nullptr || pixelSetFunction == nullptr)
		throw eve::Exception("Unhandled pixel format %s in ImageData::rotate", format.c_str());

	const RotateFilter mode = parseRotateFilter(filter);

	const float srcW = (float)width;
	const float srcH = (float)height;
	// Pixel-center coordinates: centers of the image.
	const float srcCx = srcW * 0.5f;
	const float srcCy = srcH * 0.5f;

	const float c = std::cos(radians);
	const float s = std::sin(radians);

	int dstW = width;
	int dstH = height;
	float dstCx = srcCx;
	float dstCy = srcCy;

	if (expand) {
		// Forward-map the four corners (pixel edges) to compute the AABB.
		const float corners[4][2] = {
		    {-srcCx, -srcCy},
		    {srcW - srcCx, -srcCy},
		    {-srcCx, srcH - srcCy},
		    {srcW - srcCx, srcH - srcCy},
		};
		float minX = 0.f, maxX = 0.f, minY = 0.f, maxY = 0.f;
		for (int i = 0; i < 4; ++i) {
			float ox, oy;
			rotatePoint(corners[i][0], corners[i][1], c, s, ox, oy);
			if (i == 0) {
				minX = maxX = ox;
				minY = maxY = oy;
			} else {
				minX = std::min(minX, ox);
				maxX = std::max(maxX, ox);
				minY = std::min(minY, oy);
				maxY = std::max(maxY, oy);
			}
		}
		dstW = std::max(1, (int)std::ceil(maxX - minX));
		dstH = std::max(1, (int)std::ceil(maxY - minY));
		dstCx = dstW * 0.5f;
		dstCy = dstH * 0.5f;
	}

	ImageData *dst = nullptr;
	try {
		dst = new ImageData(dstW, dstH, format);
	} catch (std::bad_alloc &) {
		throw eve::Exception("Out of memory");
	}

	// Near-identity: copy when angle is ~0 and canvas unchanged.
	if (!expand && std::fabs(radians) < 1e-7f) {
		std::memcpy(dst->getData(), data, getSize());
		return dst;
	}

	for (int yt = 0; yt < dstH; ++yt) {
		const float dy = ((float)yt + 0.5f) - dstCy;
		for (int xt = 0; xt < dstW; ++xt) {
			const float dx = ((float)xt + 0.5f) - dstCx;
			float relX, relY;
			inverseRotatePoint(dx, dy, c, s, relX, relY);
			// Map back to source pixel-center space.
			const float sx = relX + srcCx - 0.5f;
			const float sy = relY + srcCy - 0.5f;

			Colorf color;
			if (mode == RotateFilter::Nearest)
				color = sampleNearest(this, sx, sy);
			else
				color = sampleBilinear(this, sx, sy);

			if (color.a != 0.f || color.r != 0.f || color.g != 0.f || color.b != 0.f)
				dst->setPixel(xt, yt, color);
		}
	}

	return dst;
}

union Row {
    uint8_t   *u8;
    uint16_t  *u16;
    float16 *f16;
    float   *f32;
};

static void pasteRGBA8toRGBA16(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.u16[i] = (uint16_t)src.u8[i] << 8u;
}

static void pasteRGBA8toRGBA16F(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.f16[i] = float32to16(src.u8[i] / 255.0f);
}

static void pasteRGBA8toRGBA32F(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.f32[i] = src.u8[i] / 255.0f;
}

static void pasteRGBA16toRGBA8(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.u8[i] = src.u16[i] >> 8u;
}

static void pasteRGBA16toRGBA16F(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.f16[i] = float32to16(src.u16[i] / 65535.0f);
}

static void pasteRGBA16toRGBA32F(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.f32[i] = src.u16[i] / 65535.0f;
}

static void pasteRGBA16FtoRGBA8(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.u8[i] = (uint8_t)(clamp01(float16to32(src.f16[i])) * 255.0f + 0.5f);
}

static void pasteRGBA16FtoRGBA16(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.u16[i] = (uint16_t)(clamp01(float16to32(src.f16[i])) * 65535.0f + 0.5f);
}

static void pasteRGBA16FtoRGBA32F(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.f32[i] = float16to32(src.f16[i]);
}

static void pasteRGBA32FtoRGBA8(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.u8[i] = (uint8_t)(clamp01(src.f32[i]) * 255.0f + 0.5f);
}

static void pasteRGBA32FtoRGBA16(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.u16[i] = (uint16_t)(clamp01(src.f32[i]) * 65535.0f + 0.5f);
}

static void pasteRGBA32FtoRGBA16F(Row src, Row dst, int w) {
    for (int i = 0; i < w * 4; i++) dst.f16[i] = float32to16(src.f32[i]);
}

void ImageData::paste(ImageData *src, int dx, int dy, int sx, int sy, int sw, int sh) {
    std::string dstformat = getFormat();
    std::string srcformat = src->getFormat();

    int srcW = src->getWidth();
    int srcH = src->getHeight();
    int dstW = getWidth();
    int dstH = getHeight();

    size_t srcpixelsize = src->getPixelSize();
    size_t dstpixelsize = getPixelSize();

    // Check bounds; if the data ends up completely out of bounds, get out early.
    if (sx >= srcW || sx + sw < 0 || sy >= srcH || sy + sh < 0 || dx >= dstW || dx + sw < 0 || dy >= dstH ||
        dy + sh < 0)
        return;

    // Normalize values to the inside of both images.
    if (dx < 0) {
        sw += dx;
        sx -= dx;
        dx = 0;
    }
    if (dy < 0) {
        sh += dy;
        sy -= dy;
        dy = 0;
    }
    if (sx < 0) {
        sw += sx;
        dx -= sx;
        sx = 0;
    }
    if (sy < 0) {
        sh += sy;
        dy -= sy;
        sy = 0;
    }

    if (dx + sw > dstW) sw = dstW - dx;

    if (dy + sh > dstH) sh = dstH - dy;

    if (sx + sw > srcW) sw = srcW - sx;

    if (sy + sh > srcH) sh = srcH - sy;

    uint8_t *s = (uint8_t *)src->getData();
    uint8_t *d = (uint8_t *)getData();

    auto getfunction = src->pixelGetFunction;
    auto setfunction = pixelSetFunction;

    // If the dimensions match up, copy the entire memory stream in one go
    if (srcformat == dstformat && (sw == dstW && dstW == srcW && sh == dstH && dstH == srcH)) {
        memcpy(d, s, srcpixelsize * sw * sh);
    } else if (sw > 0) {
        // Otherwise, copy each row individually.
        for (int i = 0; i < sh; i++) {
            Row rowsrc = {s + (sx + (i + sy) * srcW) * srcpixelsize};
            Row rowdst = {d + (dx + (i + dy) * dstW) * dstpixelsize};

            if (srcformat == dstformat)
                memcpy(rowdst.u8, rowsrc.u8, srcpixelsize * sw);

            else if (srcformat == "RGBA8" && dstformat == "RGBA16")
                pasteRGBA8toRGBA16(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA8" && dstformat == "RGBA16F")
                pasteRGBA8toRGBA16F(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA8" && dstformat == "RGBA32F")
                pasteRGBA8toRGBA32F(rowsrc, rowdst, sw);

            else if (srcformat == "RGBA16" && dstformat == "RGBA8")
                pasteRGBA16toRGBA8(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA16" && dstformat == "RGBA16F")
                pasteRGBA16toRGBA16F(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA16" && dstformat == "RGBA32F")
                pasteRGBA16toRGBA32F(rowsrc, rowdst, sw);

            else if (srcformat == "RGBA16F" && dstformat == "RGBA8")
                pasteRGBA16FtoRGBA8(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA16F" && dstformat == "RGBA16")
                pasteRGBA16FtoRGBA16(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA16F" && dstformat == "RGBA32F")
                pasteRGBA16FtoRGBA32F(rowsrc, rowdst, sw);

            else if (srcformat == "RGBA32F" && dstformat == "RGBA8")
                pasteRGBA32FtoRGBA8(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA32F" && dstformat == "RGBA16")
                pasteRGBA32FtoRGBA16(rowsrc, rowdst, sw);
            else if (srcformat == "RGBA32F" && dstformat == "RGBA16F")
                pasteRGBA32FtoRGBA16F(rowsrc, rowdst, sw);

            else {
                // Slow path: convert src -> Colorf -> dst.
                Colorf c;
                for (int x = 0; x < sw; x++) {
                    auto srcp = (const Pixel *)(rowsrc.u8 + x * srcpixelsize);
                    auto dstp = (Pixel *)(rowdst.u8 + x * dstpixelsize);
                    getfunction(srcp, c);
                    setfunction(c, dstp);
                }
            }
        }
    }
}


size_t ImageData::getPixelSize() const { return getPixelFormatSize(getPixelFormatFromName(format)); }

bool ImageData::validPixelFormat(std::string format) {
    if (format == "R8") return true;
    if (format == "RG8") return true;
    if (format == "RGBA8") return true;
    if (format == "R16") return true;
    if (format == "RG16") return true;
    if (format == "RGBA16") return true;
    if (format == "R16F") return true;
    if (format == "RG16F") return true;
    if (format == "RGBA16F") return true;
    if (format == "R32F") return true;
    if (format == "RG32F") return true;
    if (format == "RGBA32F") return true;
    if (format == "RGBA4") return true;
    if (format == "RGB5A1") return true;
    if (format == "RGB565") return true;
    if (format == "RGB10A2") return true;
    if (format == "RG11B10F") return true;
    return false;
}

bool ImageData::canPaste(std::string src, std::string dst) {
    if (src == dst) return true;

    if (!(src == "RGBA8" || src == "RGBA16" || src == "RGBA16F" || src == "RGBA32F")) return false;

    if (!(dst == "RGBA8" || dst == "RGBA16" || dst == "RGBA16F" || dst == "RGBA32F")) return false;

    return true;
}

ImageData::PixelSetFunction ImageData::getPixelSetFunction(std::string format) {
    if (format == "R8") return setPixelR8;
    if (format == "RG8") return setPixelRG8;
    if (format == "RGBA8") return setPixelRGBA8;
    if (format == "R16") return setPixelR16;
    if (format == "RG16") return setPixelRG16;
    if (format == "RGBA16") return setPixelRGBA16;
    if (format == "R16F") return setPixelR16F;
    if (format == "RG16F") return setPixelRG16F;
    if (format == "RGBA16F") return setPixelRGBA16F;
    if (format == "R32F") return setPixelR32F;
    if (format == "RG32F") return setPixelRG32F;
    if (format == "RGBA32F") return setPixelRGBA32F;
    if (format == "RGBA4") return setPixelRGBA4;
    if (format == "RGB5A1") return setPixelRGB5A1;
    if (format == "RGB565") return setPixelRGB565;
    if (format == "RGB10A2") return setPixelRGB10A2;
    if (format == "RG11B10F") return setPixelRG11B10F;
    return nullptr;
}

ImageData::PixelGetFunction ImageData::getPixelGetFunction(std::string format) {
    if (format == "R8") return getPixelR8;
    if (format == "RG8") return getPixelRG8;
    if (format == "RGBA8") return getPixelRGBA8;
    if (format == "R16") return getPixelR16;
    if (format == "RG16") return getPixelRG16;
    if (format == "RGBA16") return getPixelRGBA16;
    if (format == "R16F") return getPixelR16F;
    if (format == "RG16F") return getPixelRG16F;
    if (format == "RGBA16F") return getPixelRGBA16F;
    if (format == "R32F") return getPixelR32F;
    if (format == "RG32F") return getPixelRG32F;
    if (format == "RGBA32F") return getPixelRGBA32F;
    if (format == "RGBA4") return getPixelRGBA4;
    if (format == "RGB5A1") return getPixelRGB5A1;
    if (format == "RGB565") return getPixelRGB565;
    if (format == "RGB10A2") return getPixelRGB10A2;
    if (format == "RG11B10F") return getPixelRG11B10F;
    return nullptr;
}

}  // namespace image
}  // namespace eve
