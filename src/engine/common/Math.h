#pragma once


#include <climits> // for CHAR_BIT
#include <cstdlib> // for rand() and RAND_MAX

/* Definitions of useful mathematical constants
 * M_E        - e
 * M_LOG2E    - log2(e)
 * M_LOG10E   - log10(e)
 * M_LN2      - ln(2)
 * M_LN10     - ln(10)
 * M_PI       - pi
 * M_PI_2     - pi/2
 * M_PI_4     - pi/4
 * M_1_PI     - 1/pi
 * M_2_PI     - 2/pi
 * M_2_SQRTPI - 2/sqrt(pi)
 * M_SQRT2    - sqrt(2)
 * M_SQRT1_2  - 1/sqrt(2)
 */

#define EVE_M_E        2.71828182845904523536
#define EVE_M_LOG2E    1.44269504088896340736
#define EVE_M_LOG10E   0.434294481903251827651
#define EVE_M_LN2      0.693147180559945309417
#define EVE_M_LN10     2.30258509299404568402
#define EVE_M_PI       3.14159265358979323846
#define EVE_M_PI_2     1.57079632679489661923
#define EVE_M_PI_4     0.785398163397448309616
#define EVE_M_1_PI     0.318309886183790671538
#define EVE_M_2_PI     0.636619772367581343076
#define EVE_M_2_SQRTPI 1.12837916709551257390
#define EVE_M_SQRT2    1.41421356237309504880
#define EVE_M_SQRT1_2  0.707106781186547524401
#define EVE_M_TORAD	   (float)(EVE_M_PI/180.0)
#define EVE_M_TODEG    (float)(180.0/EVE_M_PI)
#define EVE_TORAD(x)   (float)(x*EVE_M_TORAD)
#define EVE_TODEG(x)   (float)(x*EVE_M_TODEG)

namespace eve
{

struct Rect
{
	int x, y;
	int w, h;

	bool operator == (const Rect &rhs) const
	{
		return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h;
	}
};

}