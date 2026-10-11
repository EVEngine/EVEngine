#include "image/ImageData.h"

#include "common/Diagnostic.h"

#include <cmath>
#include <limits>

namespace eve::image {

Result<int> ImageData::scalePcg(int newWidth, int newHeight, ImageScaleFilter filter) {
	const auto invalid = [](const char* message) {
		return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message, "image.scalePcg"));
	};
	if (newWidth <= 0 || newHeight <= 0)
		return invalid("Pcg texture scale requires positive destination dimensions");
	if (filter != ImageScaleFilter::Point && filter != ImageScaleFilter::Bilinear)
		return invalid("Pcg texture scale requires point or bilinear filtering");
	if (getWidth() <= 0 || getHeight() <= 0)
		return invalid("Pcg texture scale requires a non-empty source image");
	if (filter == ImageScaleFilter::Bilinear && (getWidth() < 2 || getHeight() < 2))
		return invalid("Pcg bilinear scale requires both source dimensions to be at least two");
	const auto count = std::uint64_t(newWidth) * std::uint64_t(newHeight);
	if (count > std::uint64_t(std::numeric_limits<int>::max()))
		return invalid("Pcg texture scale destination exceeds the supported pixel count");

	ImageData candidate(newWidth, newHeight, getFormat());
	const float ratioX = filter == ImageScaleFilter::Bilinear
	                         ? float(getWidth() - 1) / float(newWidth)
	                         : float(getWidth()) / float(newWidth);
	const float ratioY = filter == ImageScaleFilter::Bilinear
	                         ? float(getHeight() - 1) / float(newHeight)
	                         : float(getHeight()) / float(newHeight);
	const auto lerp = [](const Colorf& first, const Colorf& second, float amount) {
		return Colorf{first.r + (second.r - first.r) * amount, first.g + (second.g - first.g) * amount,
		              first.b + (second.b - first.b) * amount, first.a + (second.a - first.a) * amount};
	};
	for (int y = 0; y < newHeight; ++y) {
		if (filter == ImageScaleFilter::Point) {
			const int sourceY = int(ratioY * float(y));
			for (int x = 0; x < newWidth; ++x)
				candidate.setPixel(x, y, getPixel(int(ratioX * float(x)), sourceY));
			continue;
		}
		const float sourceY = float(y) * ratioY;
		const int yFloor = int(std::floor(sourceY));
		const float yLerp = sourceY - float(yFloor);
		for (int x = 0; x < newWidth; ++x) {
			const float sourceX = float(x) * ratioX;
			const int xFloor = int(std::floor(sourceX));
			const float xLerp = sourceX - float(xFloor);
			const auto topLeft = getPixel(xFloor, yFloor);
			const auto topRight = getPixel(xFloor + 1, yFloor);
			const auto bottomLeft = getPixel(xFloor, yFloor + 1);
			const auto bottomRight = getPixel(xFloor + 1, yFloor + 1);
			const auto top = lerp(topLeft, topRight, xLerp);
			const auto bottom = lerp(bottomLeft, bottomRight, xLerp);
			candidate.setPixel(x, y, lerp(top, bottom, yLerp));
		}
	}
	adopt(candidate);
	return Result<int>::success(int(count));
}

} // namespace eve::image
