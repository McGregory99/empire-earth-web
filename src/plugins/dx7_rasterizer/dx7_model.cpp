// The model draws (task P2.6.7h).  This file holds the four vertex copies DrawModelHierarchy uses; tests/dx7_rasterizer/model_checks.h
// calls them through the names the DLLs export them under and compares both builds on recorded steps.
//
// A copy writes the `vertexLength` vertices of a material, starting at its `vertexStart`, from the model's vertex arrays into the ring
// (36-byte vertices: position, normal, colour, texture coordinate).  CopyStdVertices reads a standard array (32-byte vertices without
// colour): the first six words are copied, the colour becomes 0xffffffff and the texture coordinate follows.  CopyStdColorVertices
// reads a colour array and copies the bytes.  The smoothing variants take the position and the normal from the model's current and
// next frame, blended as (1 - f) * current + f * next with f the model's fractional frame position (the products and the sum in
// extended precision, rounded once when stored); the colour and the texture coordinate come from the current frame.  Nothing is
// checked: the original does not look at the frame numbers, the array sizes or the destination either.
#include "null_rasterizer.h"

#include "engine_imports.h"
#include "rasterizer_abi.h"

typedef unsigned long ulong;

namespace {

// (1 - f) * a + f * b with the intermediate values in extended precision, rounded to a float at the store.
float Blend(float a, float b, float f)
{
    float result;
    __asm__ volatile("fld1\n\tfsubs %3\n\tfmuls %1\n\tflds %3\n\tfmuls %2\n\tfaddp\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b), "m"(f) : "st");
    return result;
}

const char* FrameData(const GEModelLayout* model, ulong frame)
{
    return *reinterpret_cast<const char* const*>(reinterpret_cast<const char*>(model->vertexArrays[frame]) + 4);
}

const ulong kWhite = 0xffffffffUL;

} // namespace

void EEWebNullRasterizer::CopyStdVertices(const GEModelLayout* model, const GEMaterialLayout* material, void* destination)
{
    const ulong* from = reinterpret_cast<const ulong*>(FrameData(model, model->currentFrame) + material->vertexStart * 32);
    ulong* to = static_cast<ulong*>(destination);
    for (ulong i = 0; i < material->vertexLength; i++, from += 8, to += 9) {
        for (int word = 0; word < 6; word++) {
            to[word] = from[word];
        }
        to[6] = kWhite;
        to[7] = from[6];
        to[8] = from[7];
    }
}

void EEWebNullRasterizer::CopyStdColorVertices(const GEModelLayout* model, const GEMaterialLayout* material, void* destination)
{
    __builtin_memcpy(destination, FrameData(model, model->currentFrame) + material->vertexStart * 36, material->vertexLength * 36);
}

void EEWebNullRasterizer::CopyAndSmoothStdVertices(const GEModelLayout* model, const GEMaterialLayout* material, void* destination)
{
    const float f = model->fractionalFramePosition;
    const ulong* current = reinterpret_cast<const ulong*>(FrameData(model, model->currentFrame) + material->vertexStart * 32);
    const ulong* next = reinterpret_cast<const ulong*>(FrameData(model, model->nextFrame) + material->vertexStart * 32);
    ulong* to = static_cast<ulong*>(destination);
    for (ulong i = 0; i < material->vertexLength; i++, current += 8, next += 8, to += 9) {
        for (int word = 0; word < 6; word++) {
            float blended = Blend(reinterpret_cast<const float*>(current)[word], reinterpret_cast<const float*>(next)[word], f);
            to[word] = *reinterpret_cast<ulong*>(&blended);
        }
        to[6] = kWhite;
        to[7] = current[6];
        to[8] = current[7];
    }
}

void EEWebNullRasterizer::CopyAndSmoothStdColorVertices(const GEModelLayout* model, const GEMaterialLayout* material, void* destination)
{
    const float f = model->fractionalFramePosition;
    const ulong* current = reinterpret_cast<const ulong*>(FrameData(model, model->currentFrame) + material->vertexStart * 36);
    const ulong* next = reinterpret_cast<const ulong*>(FrameData(model, model->nextFrame) + material->vertexStart * 36);
    ulong* to = static_cast<ulong*>(destination);
    for (ulong i = 0; i < material->vertexLength; i++, current += 9, next += 9, to += 9) {
        for (int word = 0; word < 6; word++) {
            float blended = Blend(reinterpret_cast<const float*>(current)[word], reinterpret_cast<const float*>(next)[word], f);
            to[word] = *reinterpret_cast<ulong*>(&blended);
        }
        to[6] = current[6];
        to[7] = current[7];
        to[8] = current[8];
    }
}
