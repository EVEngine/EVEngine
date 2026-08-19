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

#include "Image.h"
#include "common/Exception.h"
#include "common/Resource.h"
#include "common/config.h"

#include "medialoader/image/PNGHandler.h"
#include "medialoader/image/STBHandler.h"
#include "medialoader/image/EXRHandler.h"

#include "medialoader/image/ddsHandler.h"
#include "medialoader/image/PVRHandler.h"
#include "medialoader/image/KTXHandler.h"
#include "medialoader/image/PKMHandler.h"
#include "medialoader/image/ASTCHandler.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{
namespace image
{

Module_IMPL(Image, new Image());

Image::Image()
{
	using namespace medialoader;

	float16Init(); // Makes sure half-float conversions can be used.

	formatHandlers = {
		new PNGHandler,
		new STBHandler,
		new EXRHandler,
		new DDSHandler,
		new PVRHandler,
		new KTXHandler,
		new PKMHandler,
		new ASTCHandler,
	};
}

Image::~Image()
{
	// ImageData objects reference the FormatHandlers in our list, so we should
	// release them instead of deleting them completely here.

	// TODO： Check if this is necessary
	// for (FormatHandler *handler : formatHandlers)
	// 	handler->release();
}


ImageData *Image::newImageData(Data *data)
{
	return new ImageData(data);
}

ImageData *Image::newImageDataFromFile(std::string path)
{
	if (path.empty())
		throw eve::Exception("Image::newImageDataFromFile: empty path");

	eve::Resource *resource = eve::ResourceManager::getInstance().get(path);
	if (!resource)
		throw eve::Exception("Could not load image file: %s", path.c_str());
	return static_cast<ImageData *>(resource);
}

ImageData *Image::newImageData(int width, int height, std::string format)
{
	return new ImageData(width, height, format);
}

ImageData *Image::newImageData(int width, int height, std::string format, void *data, bool own)
{
	return new ImageData(width, height, format, data, own);
}


bool Image::isCompressed(Data *data)
{
	for (FormatHandler *handler : formatHandlers)
	{
		if (handler->canParseCompressed((const char*)data->getData(), data->getSize()))
			return true;
	}

	return false;
}

const std::list<medialoader::FormatHandler *> &Image::getFormatHandlers() const
{
	return formatHandlers;
}

ImageData *Image::newPastedImageData(ImageData *src, int sx, int sy, int w, int h)
{
	ImageData *res = newImageData(w, h, src->getFormat());
	try
	{
		res->paste(src, 0, 0, sx, sy, w, h);
	}
	catch (eve::Exception &)
	{
		// res->release();
		throw;
	}
	return res;
}

std::vector<eve::ref<ImageData>> Image::newCubeFaces(ImageData *src)
{
	// The faces array is always ordered +x, -x, +y, -y, +z, -z.
	std::vector<eve::ref<ImageData>> faces;

	int totalW = src->getWidth();
	int totalH = src->getHeight();

	if (totalW % 3 == 0 && totalH % 4 == 0 && totalW / 3 == totalH / 4)
	{
		//    +y
		// +z +x -z
		//    -y
		//    -x

		int w = totalW / 3;
		int h = totalH / 4;

		faces.emplace_back(newPastedImageData(src, 1*w, 1*h, w, h));
		faces.emplace_back(newPastedImageData(src, 1*w, 3*h, w, h));
		faces.emplace_back(newPastedImageData(src, 1*w, 0*h, w, h));
		faces.emplace_back(newPastedImageData(src, 1*w, 2*h, w, h));
		faces.emplace_back(newPastedImageData(src, 0*w, 1*h, w, h));
		faces.emplace_back(newPastedImageData(src, 2*w, 1*h, w, h));
	}
	else if (totalW % 4 == 0 && totalH % 3 == 0 && totalW / 4 == totalH / 3)
	{
		//    +y
		// -x +z +x -z
		//    -y

		int w = totalW / 4;
		int h = totalH / 3;

		faces.emplace_back(newPastedImageData(src, 2*w, 1*h, w, h));
		faces.emplace_back(newPastedImageData(src, 0*w, 1*h, w, h));
		faces.emplace_back(newPastedImageData(src, 1*w, 0*h, w, h));
		faces.emplace_back(newPastedImageData(src, 1*w, 2*h, w, h));
		faces.emplace_back(newPastedImageData(src, 1*w, 1*h, w, h));
		faces.emplace_back(newPastedImageData(src, 3*w, 1*h, w, h));
	}
	else if (totalH % 6 == 0 && totalW == totalH / 6)
	{
		// +x
		// -x
		// +y
		// -y
		// +z
		// -z

		int w = totalW;
		int h = totalH / 6;

		for (int i = 0; i < 6; i++)
			faces.emplace_back(newPastedImageData(src, 0, i * h, w, h));
	}
	else if (totalW % 6 == 0 && totalW / 6 == totalH)
	{
		// +x -x +y -y +z -z

		int w = totalW / 6;
		int h = totalH;

		for (int i = 0; i < 6; i++)
			faces.emplace_back(newPastedImageData(src, i * w, 0, w, h));
	}
	else
		throw eve::Exception("Unknown cubemap image dimensions!");

	return faces;
}

std::vector<eve::ref<ImageData>> Image::newVolumeLayers(ImageData *src)
{
	std::vector<eve::ref<ImageData>> layers;

	int totalW = src->getWidth();
	int totalH = src->getHeight();

	if (totalW % totalH == 0)
	{
		for (int i = 0; i < totalW / totalH; i++)
			layers.emplace_back(newPastedImageData(src, i * totalH, 0, totalH, totalH));
	}
	else if (totalH % totalW == 0)
	{
		for (int i = 0; i < totalH / totalW; i++)
			layers.emplace_back(newPastedImageData(src, 0, i * totalW, totalW, totalW));
	}
	else
		throw eve::Exception("Cannot extract volume layers from source ImageData.");

	return layers;
}

void Image::expose(ssq::Table &table) {
	auto cls = table.addClass(name, Image::create, false);
	expose(cls);

	// Single ImageData class for every module that hands image::ImageData*
	// to scripts (Font glyphs, Model3D embedded textures, this module).
	auto img = table.addClass<image::ImageData>(
		"ImageData", std::function<image::ImageData *()>([]() -> image::ImageData * { return nullptr; }),
		true);
	img.addFunc("getWidth", &image::ImageData::getWidth);
	img.addFunc("getHeight", &image::ImageData::getHeight);
	img.addFunc("getFormat", &image::ImageData::getFormat);
	img.addFunc("getSize", &image::ImageData::getSize);
	img.addFunc("getPixelSize", &image::ImageData::getPixelSize);
	img.addFunc("isSRGB", &image::ImageData::isSRGB);
	img.addFunc("inside", &image::ImageData::inside);
	img.addFunc("clone", &image::ImageData::clone);
	img.addFunc("paste", &image::ImageData::paste);
	img.addFunc("rotate", &image::ImageData::rotate);
	img.addFunc("getPixelR", [](image::ImageData *self, int x, int y) -> float {
		if (!self) return 0.f;
		return self->getPixel(x, y).r;
	});
	img.addFunc("getPixelG", [](image::ImageData *self, int x, int y) -> float {
		if (!self) return 0.f;
		return self->getPixel(x, y).g;
	});
	img.addFunc("getPixelB", [](image::ImageData *self, int x, int y) -> float {
		if (!self) return 0.f;
		return self->getPixel(x, y).b;
	});
	img.addFunc("getPixelA", [](image::ImageData *self, int x, int y) -> float {
		if (!self) return 0.f;
		return self->getPixel(x, y).a;
	});
	img.addFunc("setPixel", [](image::ImageData *self, int x, int y, float r, float g, float b, float a) {
		if (!self) return;
		self->setPixel(x, y, image::ImageData::Colorf{r, g, b, a});
	});
}

void Image::expose(ssq::Class &cls) {
	cls.addFunc("getName", &Image::getName);
	cls.addFunc("newImageData", static_cast<ImageData *(Image::*)(Data *)>(&Image::newImageData));
	cls.addFunc("newImageDataFromFile", &Image::newImageDataFromFile);
	cls.addFunc("newEmptyImageData",
	            [](Image *self, int width, int height, const std::string &format) -> ImageData * {
		            if (!self) return nullptr;
		            return self->newImageData(width, height, format);
	            });
	cls.addFunc("isCompressed", &Image::isCompressed);
}

} // image
} // eve
