// Arithmetic the rasterizer shares with the original's /QIfist code: the x87 conversion with the control word's rounding,
// the truncating conversion of a colour channel and the "floor" the original computes from a rounded value.  Used by the
// state setters (dx7_state.cpp) and the scene calls (dx7_scene.cpp).
#ifndef EEWEB_DX7_MATH_H
#define EEWEB_DX7_MATH_H

namespace dx7math {

// The x87 conversion with the control word's rounding (to nearest even): what the original's /QIfist code does (and what the
// run-time's lrintf would, which this DLL does not link).
inline long RoundToNearest(float value)
{
    long rounded;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(rounded) : "m"(value) : "st");
    return rounded;
}

// The same through the C run-time's truncating conversion, which sees the exact product.
inline unsigned long TruncatedChannel(float channel)
{
    return static_cast<unsigned long>(static_cast<long>(static_cast<double>(channel) * 255.0));
}

// floor() the way the original's SetViewport computes it: the value rounded to nearest, then one less when the rounding went
// up.  The comparison treats the rounded integer as unsigned (the original loads it as a 64-bit value with a zero high
// word), so a negative value is always taken one lower.
inline long FloorLikeOriginal(float value)
{
    long rounded = RoundToNearest(value);
    if (static_cast<double>(static_cast<unsigned long>(rounded)) > static_cast<double>(value)) {
        --rounded;
    }
    return rounded;
}

} // namespace dx7math

#endif
