// The polygon and vertex arrays (task P2.6.7a): CreatePolygonArray and CreateVertexArray (slots 10 and 11) and the three classes
// they build, DX7PolygonArray, DX7StandardVertexArray and DX7StdColorVertexArray.  The engine reads and writes them only through
// their virtual methods (models, bitmaps, lines and the draw slots), so what has to match the original is the object's words
// (vtable, block pointer, counts: DX7PolygonArray 0x10 bytes, the vertex arrays 0xc), the block's layout and every method's answer.
//
// tests/dx7_rasterizer/array_checks.h compares both builds on 44 recorded steps (the words, the heap size of the block, its bytes,
// the answers).  What the original does and this build keeps:
//   - DX7PolygonArray(count): a block of count * 6 bytes (three 16-bit indices per polygon), the count at 0xc and three times the
//     count at 0x8; an index is stored as its low 16 bits and read back unsigned; SizeOf is that number times 2 plus 12;
//   - DX7StandardVertexArray(count): 32 bytes per vertex (point at 0x0, normal at 0xc, texture coordinate at 0x18); no colour,
//     the colour reads (1, 1, 1) and the opacity 1.0, SetColor and SetOpacity do nothing, SetVertex ignores colour and opacity;
//     type 0, SizeOf is count * 32 + 8;
//   - DX7StdColorVertexArray(count): 36 bytes per vertex (point at 0x0, normal at 0xc, packed ARGB colour at 0x18, texture
//     coordinate at 0x1c); a channel is packed as the x87 conversion (to nearest even) of the channel times 255 stored as a float, not clamped,
//     so a value outside 0..1 spills into the next byte; SetColor keeps the alpha byte, SetOpacity the colour bytes; a channel is
//     read back as the byte times the single-precision 1/255 (rounded once, as the original's fmul); type 1, SizeOf is
//     count * 36 + 8;
//   - CopyFrom copies min(own count, source count) vertices of the array's own stride, raw, whatever kind the source is;
//   - CreateVertexArray answers null for a kind other than 0 and 1.
// No index or vertex is range checked (the original does not either).
#include "null_rasterizer.h"

typedef unsigned long ulong;

namespace {

// x87 helpers.  The original multiplies, stores the product as a float and converts that with the control word's rounding.
inline long RoundScaled(float channel)
{
    const float scale = 255.0f;
    float product;
    long rounded;
    __asm__ volatile("flds %2\n\tfmuls %3\n\tfstps %1\n\tflds %1\n\tfistpl %0" : "=m"(rounded), "=m"(product) : "m"(channel), "m"(scale) : "st");
    return rounded;
}

inline float UnpackByte(ulong byte)
{
    const unsigned bits = 0x3b808081u; // 1/255 as a float
    float inverse;
    __builtin_memcpy(&inverse, &bits, 4);
    long value = static_cast<long>(byte);
    float result;
    __asm__ volatile("fildl %1\n\tfmuls %2\n\tfstps %0" : "=m"(result) : "m"(value), "m"(inverse) : "st");
    return result;
}

class DX7PolygonArray : public GEPolygonArray {
public:
    explicit DX7PolygonArray(ulong count)
        : m_indices(static_cast<unsigned short*>(::operator new(count * 6))), m_allocated(count * 3), m_count(count)
    {
    }
    ~DX7PolygonArray() { ::operator delete(m_indices); }

    ulong GetSize() const override { return m_count; }
    ulong GetVertexIndex1(ulong polygon) const override { return m_indices[polygon * 3]; }
    ulong GetVertexIndex2(ulong polygon) const override { return m_indices[polygon * 3 + 1]; }
    ulong GetVertexIndex3(ulong polygon) const override { return m_indices[polygon * 3 + 2]; }
    void SetVertexIndex1(ulong polygon, ulong index) override { m_indices[polygon * 3] = static_cast<unsigned short>(index); }
    void SetVertexIndex2(ulong polygon, ulong index) override { m_indices[polygon * 3 + 1] = static_cast<unsigned short>(index); }
    void SetVertexIndex3(ulong polygon, ulong index) override { m_indices[polygon * 3 + 2] = static_cast<unsigned short>(index); }
    void SetVertexIndices(ulong polygon, ulong first, ulong second, ulong third) override
    {
        m_indices[polygon * 3] = static_cast<unsigned short>(first);
        m_indices[polygon * 3 + 1] = static_cast<unsigned short>(second);
        m_indices[polygon * 3 + 2] = static_cast<unsigned short>(third);
    }
    ulong SizeOf() const override { return m_allocated * 2 + 12; }

private:
    unsigned short* m_indices; // 0x4
    ulong m_allocated;         // 0x8: three indices per polygon
    ulong m_count;             // 0xc
};

// The two vertex arrays share the point, the normal and the copy; the stride and the colour differ.
template <ulong Stride>
class VertexArrayBase : public GEVertexArray {
public:
    explicit VertexArrayBase(ulong count) : m_data(static_cast<char*>(::operator new(count * Stride))), m_count(count) {}
    ~VertexArrayBase() { ::operator delete(m_data); }

    void CopyFrom(GEVertexArray const& source) override
    {
        const VertexArrayBase& other = reinterpret_cast<const VertexArrayBase&>(source);
        ulong count = other.m_count <= m_count ? other.m_count : m_count;
        __builtin_memcpy(m_data, other.m_data, count * Stride);
    }
    void GetNormal(GEVector& normal, ulong index) const override { Copy(reinterpret_cast<float*>(&normal), At(index) + 3, 3); }
    void GetPoint(GE3DPoint& point, ulong index) const override { Copy(reinterpret_cast<float*>(&point), At(index), 3); }
    ulong GetSize() const override { return m_count; }
    void SetNormal(GEVector const& normal, ulong index) override { Copy(At(index) + 3, reinterpret_cast<const float*>(&normal), 3); }
    void SetPoint(GE3DPoint const& point, ulong index) override { Copy(At(index), reinterpret_cast<const float*>(&point), 3); }
    ulong SizeOf() const override { return m_count * Stride + 8; }

protected:
    float* At(ulong index) const { return reinterpret_cast<float*>(m_data + index * Stride); }
    static void Copy(float* to, const float* from, int count)
    {
        for (int i = 0; i < count; i++) {
            to[i] = from[i];
        }
    }

    char* m_data;  // 0x4
    ulong m_count; // 0x8
};

class DX7StandardVertexArray : public VertexArrayBase<32> {
public:
    explicit DX7StandardVertexArray(ulong count) : VertexArrayBase<32>(count) {}

    void GetColor(GEColor& color, ulong) const override
    {
        float* channels = reinterpret_cast<float*>(&color);
        channels[0] = channels[1] = channels[2] = 1.0f;
    }
    float GetOpacity(ulong) const override { return 1.0f; }
    void GetTextureCoordinate(GETextureCoordinate& coordinate, ulong index) const override { Copy(reinterpret_cast<float*>(&coordinate), At(index) + 6, 2); }
    ulong GetType() const override { return 0; }
    void SetColor(GEColor const&, ulong) override {}
    void SetOpacity(float, ulong) override {}
    void SetTextureCoordinate(GETextureCoordinate const& coordinate, ulong index) override { Copy(At(index) + 6, reinterpret_cast<const float*>(&coordinate), 2); }
    void SetVertex(GEColor const&, GE3DPoint const& point, GEVector const& normal, GETextureCoordinate const& coordinate, float, ulong index) override
    {
        float* vertex = At(index);
        Copy(vertex, reinterpret_cast<const float*>(&point), 3);
        Copy(vertex + 3, reinterpret_cast<const float*>(&normal), 3);
        Copy(vertex + 6, reinterpret_cast<const float*>(&coordinate), 2);
    }
};

class DX7StdColorVertexArray : public VertexArrayBase<36> {
public:
    explicit DX7StdColorVertexArray(ulong count) : VertexArrayBase<36>(count) {}

    void GetColor(GEColor& color, ulong index) const override
    {
        ulong argb = Argb(index);
        float* channels = reinterpret_cast<float*>(&color);
        channels[0] = UnpackByte((argb >> 16) & 0xff);
        channels[1] = UnpackByte((argb >> 8) & 0xff);
        channels[2] = UnpackByte(argb & 0xff);
    }
    float GetOpacity(ulong index) const override { return UnpackByte(Argb(index) >> 24); }
    void GetTextureCoordinate(GETextureCoordinate& coordinate, ulong index) const override { Copy(reinterpret_cast<float*>(&coordinate), At(index) + 7, 2); }
    ulong GetType() const override { return 1; }
    void SetColor(GEColor const& color, ulong index) override
    {
        const float* channels = reinterpret_cast<const float*>(&color);
        ulong red = static_cast<ulong>(RoundScaled(channels[0]));
        ulong green = static_cast<ulong>(RoundScaled(channels[1]));
        ulong blue = static_cast<ulong>(RoundScaled(channels[2]));
        ulong& word = Argb(index);
        word = ((red << 8 | green) << 8) | (word & 0xff000000UL) | blue;
    }
    void SetOpacity(float opacity, ulong index) override
    {
        ulong& word = Argb(index);
        word = (word & 0xffffffUL) | (static_cast<ulong>(RoundScaled(opacity)) << 24);
    }
    void SetTextureCoordinate(GETextureCoordinate const& coordinate, ulong index) override { Copy(At(index) + 7, reinterpret_cast<const float*>(&coordinate), 2); }
    void SetVertex(GEColor const& color, GE3DPoint const& point, GEVector const& normal, GETextureCoordinate const& coordinate, float opacity, ulong index) override
    {
        float* vertex = At(index);
        const float* channels = reinterpret_cast<const float*>(&color);
        Copy(vertex, reinterpret_cast<const float*>(&point), 3);
        Copy(vertex + 3, reinterpret_cast<const float*>(&normal), 3);
        ulong alpha = static_cast<ulong>(RoundScaled(opacity));
        ulong red = static_cast<ulong>(RoundScaled(channels[0]));
        ulong green = static_cast<ulong>(RoundScaled(channels[1]));
        ulong blue = static_cast<ulong>(RoundScaled(channels[2]));
        Argb(index) = (((alpha << 8 | red) << 8 | green) << 8) | blue;
        Copy(vertex + 7, reinterpret_cast<const float*>(&coordinate), 2);
    }

private:
    ulong& Argb(ulong index) const { return *reinterpret_cast<ulong*>(At(index) + 6); }
};

} // namespace

// Differs from the original on purpose: a count whose byte size does not fit in 32 bits is refused (the original asks operator new for the
// wrapped size and then writes past the small block it gets).
GEPolygonArray* EEWebNullRasterizer::CreatePolygonArray(ulong count)
{
    if (count > 0xffffffffUL / 6) {
        return 0;
    }
    return new DX7PolygonArray(count);
}

GEVertexArray* EEWebNullRasterizer::CreateVertexArray(ulong kind, ulong count)
{
    if ((kind == 0 && count > 0xffffffffUL / 32 - 1) || (kind == 1 && count > 0xffffffffUL / 36 - 1)) {
        return 0;
    }
    if (kind == 0) {
        return new DX7StandardVertexArray(count);
    }
    if (kind == 1) {
        return new DX7StdColorVertexArray(count);
    }
    return 0;
}
