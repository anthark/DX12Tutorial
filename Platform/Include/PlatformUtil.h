#pragma once

#include "PlatformPoint.h"

// NearestPowerOf2
PLATFORM_API UINT NearestPowerOf2(UINT val);

// Integer number division with rounding up
template <typename T>
T DivUp(
    T a,                         ///< [in] What to divide
    T b                          ///< [in] Divider
)
{
    static_assert(std::numeric_limits<T>::is_integer, "T must be of integer type");

    return (a + b - 1) / b;
}

// Simple AABB structure
template <typename T>
struct AABB
{
    AABB()
        : bbMax{ -std::numeric_limits<T>::max(), -std::numeric_limits<T>::max(), -std::numeric_limits<T>::max() }
        , bbMin{ std::numeric_limits<T>::max(), std::numeric_limits<T>::max(), std::numeric_limits<T>::max() }
    {}

    inline void Add(const Point3<T>& p)
    {
        bbMin.x = std::min(bbMin.x, p.x);
        bbMin.y = std::min(bbMin.y, p.y);
        bbMin.z = std::min(bbMin.z, p.z);

        bbMax.x = std::max(bbMax.x, p.x);
        bbMax.y = std::max(bbMax.y, p.y);
        bbMax.z = std::max(bbMax.z, p.z);
    }

    inline Point3<T> GetSize() const { return bbMax - bbMin; }
    inline const bool IsEmpty() const { return bbMin.x >= bbMax.x; }

    Point3<T> bbMin;
    Point3<T> bbMax;
};

// Strip file extension
PLATFORM_API std::tstring StripExtension(const std::tstring& filename);
// Short filename (no path)
PLATFORM_API std::tstring ShortFilename(const std::tstring& filename);
// Parent folder name
PLATFORM_API std::tstring GetParentName(const std::tstring& filename);
// Convert default std::tstring to std::string
PLATFORM_API std::string ToString(const std::tstring& src);
