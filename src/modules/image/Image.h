#pragma once

#include "common/Module.h"
#include "image/ImageData.h"
#include "filesystem/File.h"

#include <list>

namespace eve
{
namespace image
{

/**
 * This module is responsible for decoding files such as PNG, GIF, JPEG
 * into raw pixel data, as well as parsing compressed formats which are designed
 * to be uploaded to the GPU and rendered without being un-compressed.
 * This module does not know how to draw images on screen; only love.graphics
 * knows that.
 **/
class Image : public Module
{
public:
    Module_REG(Image);

	Image();
	virtual ~Image();

	/**
	 * Creates new ImageData from FileData.
	 * @param data The FileData containing the encoded image data.
	 * @return The new ImageData.
	 **/
	ImageData *newImageData(Data *data);

	/**
	 * Creates empty ImageData with the given size.
	 * @param width The width of the ImageData.
	 * @param height The height of the ImageData.
	 * @return The new ImageData.
	 **/
	ImageData *newImageData(int width, int height, PixelFormat format = PIXELFORMAT_RGBA8);

	/**
	 * Creates empty ImageData with the given size.
	 * @param width The width of the ImageData.
	 * @param height The height of the ImageData.
	 * @param data The data to load into the ImageData.
	 * @param own Whether the new ImageData should take ownership of the data or
	 *        copy it.
	 * @return The new ImageData.
	 **/
	ImageData *newImageData(int width, int height, PixelFormat format, void *data, bool own = false);

	/**
	 * Creates new CompressedImageData from FileData.
	 * @param data The FileData containing the compressed image data.
	 * @return The new CompressedImageData.
	 **/
	// CompressedImageData *newCompressedData(Data *data);

	/**
	 * Determines whether a FileData is Compressed image data or not.
	 * @param data The FileData to test.
	 **/
	bool isCompressed(Data *data);

	std::vector<ref<ImageData>> newCubeFaces(ImageData *src);
	std::vector<ref<ImageData>> newVolumeLayers(ImageData *src);

	const std::list<FormatHandler *> &getFormatHandlers() const;

private:

	ImageData *newPastedImageData(ImageData *src, int sx, int sy, int w, int h);

	// Image format handlers we can use for decoding and encoding ImageData.
	std::list<FormatHandler *> formatHandlers;

}; // Image

} // image
} // eve

