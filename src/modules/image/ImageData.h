
#pragma once

#include "common/Data.h"
#include "common/Resource.h"

#include "medialoader/image/pixelformat.h"
#include "medialoader/image/floattypes.h"
#include "medialoader/image/Color.h"
#include "medialoader/image/FormatHandler.h"

#include <cstdint>

namespace eve
{
namespace filesystem {
class FileData;
}
namespace image
{

/**
 * @brief Represents raw pixel data.
 **/
class ImageData : public Resource
{
public:
	using float16 = medialoader::float16;
	using Colorf = medialoader::Colorf;
	using FormatHandler = medialoader::FormatHandler;

	union Pixel
	{
		uint8_t   rgba8[4];
		uint16_t  rgba16[4];
		float16   rgba16f[4];
		float     rgba32f[4];
		uint16_t  packed16;
		uint32_t  packed32;
	};

	typedef void (*PixelSetFunction)(const Colorf &c, Pixel *p);
	typedef void (*PixelGetFunction)(const Pixel *p, Colorf &c);

	ImageData(Data *data);
	ImageData(int width, int height, std::string format = "RGBA8");
	ImageData(int width, int height, std::string format, void *data, bool own);
	ImageData(const ImageData &c);
	virtual ~ImageData();

	/**
	 * @brief Paste part of one ImageData onto another. The subregion defined by the top-left
	 * corner (sx, sy) and the size (sw,sh) will be pasted to (dx,dy) in this ImageData.
	 * @param dx The destination x-coordinate.
	 * @param dy The destination y-coordinate.
	 * @param sx The source x-coordinate.
	 * @param sy The source y-coordinate.
	 * @param sw The source width.
	 * @param sh The source height.
	 **/
	void paste(ImageData *src, int dx, int dy, int sx, int sy, int sw, int sh);

	/**
	 * @brief Rotate pixels around the image center and return a new ImageData.
	 *
	 * Uses inverse mapping (iterate destination → sample source) so every output
	 * pixel is filled — the standard approach for image rotation.
	 *
	 * Angle convention matches Math::rotate2*: positive radians appear clockwise
	 * with the engine's Y-down screen coordinates (same as LÖVE draw rotation).
	 *
	 * @param radians Rotation angle in radians.
	 * @param filter  "nearest" (pixel art / sharp), "linear" (bilinear), or
	 *                "rotsprite" (Xenowhirl: Scale2x×3 → offset search → NN downscale).
	 * @param expand  If true, grow the canvas to fit the full rotated AABB;
	 *                if false, keep the source size (edges may be clipped).
	 * @return Caller-owned ImageData in the same pixel format; out-of-source
	 *         samples remain transparent black. RotSprite never invents colors
	 *         (picks existing palette entries via Scale2x / nearest).
	 **/
	ImageData *rotate(float radians, std::string filter = "nearest", bool expand = true) const;

	/**
	 * @brief Checks whether a position is inside this ImageData. Useful for checking bounds.
	 * @param x The position along the x-axis.
	 * @param y The position along the y-axis.
	 **/
	bool inside(int x, int y) const;

	/**
	 * @brief Sets the pixel at location (x,y).
	 * @param x The location along the x-axis.
	 * @param y The location along the y-axis.
	 * @param p The color to use for the given location.
	 **/
	void setPixel(int x, int y, const Colorf &p);

	/**
	 * @brief Gets the pixel at location (x,y).
	 * @param x The location along the x-axis.
	 * @param y The location along the y-axis.
	 * @return The color for the given location.
	 **/
	void getPixel(int x, int y, Colorf &c) const;
	Colorf getPixel(int x, int y) const;

	/**
	 * @brief Encodes raw pixel data into a given format.
	 * @param f The file to save the encoded image data to.
	 * @param format The format of the encoded data.
	 **/
	filesystem::FileData *encode(FormatHandler::EncodedFormat format, const char *filename, bool writefile) const;

	// Implements ImageDataBase.
	ImageData *clone() const;
	void *getData() const;
	size_t getSize() const;
	bool isSRGB() const;

	int getWidth() const;
	int getHeight() const;
	std::string getFormat() const;

	size_t getPixelSize() const;

	PixelSetFunction getPixelSetFunction() const { return pixelSetFunction; }
	PixelGetFunction getPixelGetFunction() const { return pixelGetFunction; }

	static bool validPixelFormat(std::string format);
	static bool canPaste(std::string src, std::string dst);

	static PixelSetFunction getPixelSetFunction(std::string format);
	static PixelGetFunction getPixelGetFunction(std::string format);

	static bool getConstant(const char *in, FormatHandler::EncodedFormat &out);
	static bool getConstant(FormatHandler::EncodedFormat in, const char *&out);
	static std::vector<std::string> getConstants(FormatHandler::EncodedFormat);

private:

	// Create imagedata. Initialize with data if not null.
	void create(int width, int height, std::string format, void *data = nullptr);

	// Decode and load an encoded format.
	void decode(Data *data);

	// The actual data.
	unsigned char *data = nullptr;

	int width, height;
	std::string format;

	// The format handler that was used to decode the ImageData. We need to know
	// this so we can properly delete memory allocated by the decoder.
	FormatHandler* decodeHandler;

	PixelSetFunction pixelSetFunction;
	PixelGetFunction pixelGetFunction;

}; // ImageData

} // image
} // eve
