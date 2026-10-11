#include "image/ImageData.h"

#include <zeroerr/unittest.h>

using eve::image::ImageData;
using eve::image::ImageScaleFilter;

TEST_CASE("image.pcgScale.pointPreservesSourceMappingAndFailureAtomicity") {
	ImageData image(3, 2, "RGBA32F");
	for (int y = 0; y < 2; ++y)
		for (int x = 0; x < 3; ++x)
			image.setPixel(x, y, ImageData::Colorf{float(x + y * 3), float(y), 0, 1});
	auto scaled = image.scalePcg(2, 4, ImageScaleFilter::Point);
	REQUIRE(scaled.ok());
	CHECK(scaled.value() == 8);
	CHECK(image.getWidth() == 2);
	CHECK(image.getHeight() == 4);
	CHECK(image.getPixel(0, 0).r == 0);
	CHECK(image.getPixel(1, 0).r == 1);
	CHECK(image.getPixel(0, 2).r == 3);
	CHECK(image.getPixel(1, 3).r == 4);
	const auto before = image.getPixel(1, 3);
	CHECK(!image.scalePcg(0, 2, ImageScaleFilter::Point).ok());
	CHECK(image.getWidth() == 2);
	CHECK(image.getPixel(1, 3).r == before.r);
}

TEST_CASE("image.pcgScale.bilinearKeepsPcgNonEdgeSampling") {
	ImageData image(2, 2, "RGBA32F");
	image.setPixel(0, 0, ImageData::Colorf{0, 0, 0, 1});
	image.setPixel(1, 0, ImageData::Colorf{1, 0, 0, 1});
	image.setPixel(0, 1, ImageData::Colorf{0, 1, 0, 1});
	image.setPixel(1, 1, ImageData::Colorf{1, 1, 0, 1});
	auto scaled = image.scalePcg(2, 2, ImageScaleFilter::Bilinear);
	REQUIRE(scaled.ok());
	CHECK(scaled.value() == 4);
	CHECK(image.getPixel(1, 0).r == 0.5F);
	CHECK(image.getPixel(0, 1).g == 0.5F);
	CHECK(image.getPixel(1, 1).r == 0.5F);
	CHECK(image.getPixel(1, 1).g == 0.5F);
}
