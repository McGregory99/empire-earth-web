// The model draws (task P2.6.7h): DrawModelHierarchy (slot 75, 0x10004358), Draw(GEModel *) (19) and DrawSky (24) and the four vertex
// copies DrawModelHierarchy uses.  tests/dx7_rasterizer/model_checks.h compares both builds on recorded steps (the copies are called
// through the names the DLLs export them under).
//
// DrawModelHierarchy(model, screen, detail, orthographic): the model's graphics transform, applied to its parent's (the engine's
// static transform stack, GERasterizer::sTransformStack, holds {transform, scale} for every model being drawn; the first model's
// scale is its own, a child's the product with its parent's), is sent to the engine's visibility test (perspective or orthographic,
// answering "visible" and "needs clipping"; the second becomes render state 0x88, NORMALIZENORMALS).  A visible model picks its
// level of detail (a child always draws level 0; the others from the engine's pixel size of the model, 800 / the viewport's pixel
// width and the detail argument: level = round((1 - min(1, size * 800 / width / detail)) * levels), at most levels - 1), sends the
// world transform and the render state, and draws every material that has polygons at that level: the scene polygon count grows,
// the texture is bound, the material (diffuse = colour with the opacity of material * model, ambient = colour, the rest zero) is
// sent, the material's vertices are copied into the ring (the frames blended when the model's fraction is not zero), unlocked and
// drawn as an indexed triangle list.  The answer is the last Direct3D or unlock status (the render state's status first); the
// children are drawn while the answer is zero, whether or not the model itself was visible.
//
// Differs from the original, deliberately (it dereferences them): a missing record, device, state, vertex array, polygon array, or
// ring, a hierarchy deeper than 64 levels, and a null child are refused or skipped; the answer for the refusal is EEWEB_NOT_SUPPORTED
// (or EEWEB_LAYOUT_VIOLATION when the model fails its canary).
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

// The engine's own functions, bare: the static transform stack and the model tests (their results are consumed in x87 registers).
extern "C" {
void __attribute__((thiscall)) EEWebTransformationAssign(void* self, const void* other) __asm__("??4GETransformation@@QAEAAV0@ABV0@@Z");
float __attribute__((thiscall)) EEWebGetPixelSize(void* self, void* model, void* viewport, void* transformation, float scale, int orthographic)
    __asm__("?GetPixelSize@GERasterizer@@QAEMPAVGEModel@@PAVGEViewport@@AAVGETransformation@@M_N@Z");
void __attribute__((thiscall)) EEWebIsPerspectiveVisible(void* self, void* model, float scale, void* viewport, void* transformation, unsigned char* visible,
                                                          unsigned char* second)
    __asm__("?IsPerspectiveModelVisible@GERasterizer@@IAEXPAVGEModel@@MPAVGEViewport@@AAVGETransformation@@AA_N3@Z");
void __attribute__((thiscall)) EEWebIsOrthographicVisible(void* self, void* model, void* viewport, void* transformation, unsigned char* visible,
                                                           unsigned char* second)
    __asm__("?IsOrthographicModelVisible@GERasterizer@@IAEXPAVGEModel@@PAVGEViewport@@AAVGETransformation@@AA_N3@Z");
__declspec(dllimport) StdVectorLayout EEWebTransformStack
    __asm__("?sTransformStack@GERasterizer@@1V?$vector@UTransformStack@GERasterizer@@V?$allocator@UTransformStack@GERasterizer@@@std@@@std@@A");

// round((1 - min(1, pixelSize * (800 / width) / detail)) * levels): the original's sequence, with the engine's pixel size kept in the
// x87 register it comes back in.  cdecl: self, model, viewport, transformation, scale, orthographic, width, detail, levels, the address of
// GetPixelSize and the address of the constant 800.0f.
long __attribute__((cdecl)) EEWebLevelOfDetail(void* self, void* model, void* viewport, void* transformation, float scale, int orthographic, float width, float detail,
                                               long levels, void* getPixelSize, const float* constant);
}

__asm__(".text\n"
        ".globl _EEWebLevelOfDetail\n"
        "_EEWebLevelOfDetail:\n"
        ".intel_syntax noprefix\n"
        "push ebp\n"
        "mov ebp, esp\n"
        "sub esp, 8\n"
        "push dword ptr [ebp+28]\n"
        "push dword ptr [ebp+24]\n"
        "push dword ptr [ebp+20]\n"
        "push dword ptr [ebp+16]\n"
        "push dword ptr [ebp+12]\n"
        "mov ecx, [ebp+8]\n"
        "call dword ptr [ebp+44]\n"
        "mov eax, [ebp+48]\n"
        "fld dword ptr [eax]\n"
        "fdiv dword ptr [ebp+32]\n"
        "fmulp st(1), st\n"
        "fdiv dword ptr [ebp+36]\n"
        "fld1\n"
        "fcomp st(1)\n"
        "fnstsw ax\n"
        "sahf\n"
        "jae 1f\n"
        "fstp st(0)\n"
        "fld1\n"
        "1:\n"
        "fld1\n"
        "fsub st, st(1)\n"
        "mov eax, [ebp+40]\n"
        "mov [ebp-4], eax\n"
        "fimul dword ptr [ebp-4]\n"
        "fstp dword ptr [ebp-8]\n"
        "fstp st(0)\n"
        "fld dword ptr [ebp-8]\n"
        "fistp dword ptr [ebp-4]\n"
        "mov eax, [ebp-4]\n"
        "mov esp, ebp\n"
        "pop ebp\n"
        "ret\n"
        ".att_syntax prefix\n");

namespace {

const ulong kWorldTransform = 1;
const ulong kNormalizeNormals = 0x88;
const ulong kMaxDepth = 64;

// An element of the engine's transform stack: the transformation of a model being drawn and its accumulated scale.
struct StackEntry {
    void* transformation;
    float scale;
};

void StackPush(void* transformation, float scale)
{
    StdVectorLayout& stack = EEWebTransformStack;
    if (stack.last == stack.end) {
        ulong size = static_cast<ulong>(static_cast<char*>(stack.last) - static_cast<char*>(stack.first)) / sizeof(StackEntry);
        ulong capacity = size + (size ? size : 1);
        StackEntry* fresh = static_cast<StackEntry*>(::operator new(capacity * sizeof(StackEntry)));
        if (size) {
            __builtin_memcpy(fresh, stack.first, size * sizeof(StackEntry));
        }
        if (stack.first) {
            ::operator delete(stack.first);
        }
        stack.first = fresh;
        stack.last = fresh + size;
        stack.end = fresh + capacity;
    }
    StackEntry* entry = static_cast<StackEntry*>(stack.last);
    entry->transformation = transformation;
    entry->scale = scale;
    stack.last = entry + 1;
}

void StackPop()
{
    StdVectorLayout& stack = EEWebTransformStack;
    stack.last = static_cast<StackEntry*>(stack.last) - 1;
}

ulong StackSize()
{
    StdVectorLayout& stack = EEWebTransformStack;
    return stack.first ? static_cast<ulong>(static_cast<char*>(stack.last) - static_cast<char*>(stack.first)) / sizeof(StackEntry) : 0;
}

// a * b in extended precision, rounded once.
float Product(float a, float b)
{
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(result) : "m"(a), "m"(b) : "st");
    return result;
}



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

long EEWebNullRasterizer::DrawModelHierarchy(GEModel* object, ulong screen, float detail, bool orthographic)
{
    if (Note(EEWebCheckModel(object)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    GEModelLayout* model = reinterpret_cast<GEModelLayout*>(object);
    GERScreenStateLayout* state = State(screen);
    DX7ScreenRecordLayout* record = Record(screen);
    IDirect3DDevice7* device = Device(screen);
    if (!state || !record || !device || !record->vertexBuffer || StackSize() >= kMaxDepth || !model->vertexArrays || !model->vertexArrays[0] ||
        !model->polygonArrays) {
        return EEWEB_NOT_SUPPORTED;
    }
    long result = 0;
    GETransformationLayout local;
    bool nested = StackSize() != 0;
    float scale;
    if (!nested) {
        EEWebTransformationAssign(&local, &model->graphicsTransform);
        scale = model->graphicsScale;
    } else {
        StackEntry* parent = static_cast<StackEntry*>(EEWebTransformStack.last) - 1;
        EEWebTransformationApply(parent->transformation, &model->graphicsTransform, &local);
        scale = Product(model->graphicsScale, parent->scale);
    }
    StackPush(&local, scale);
    unsigned char visible = 0, second = 0;
    if (!orthographic) {
        EEWebIsPerspectiveVisible(this, model, scale, state, &local, &visible, &second);
    } else {
        EEWebIsOrthographicVisible(this, model, state, &local, &visible, &second);
    }
    if (visible) {
        ulong lod = 0;
        if (!nested) {
            static const float kPixelConstant = 800.0f;
            const float width = state->viewport.viewRect.right - state->viewport.viewRect.left;
            ulong rounded = static_cast<ulong>(EEWebLevelOfDetail(this, model, state, &local, scale, orthographic, width, detail, static_cast<long>(model->lodCount),
                                                                  reinterpret_cast<void*>(&EEWebGetPixelSize), &kPixelConstant));
            lod = rounded < model->lodCount - 1 ? rounded : model->lodCount - 1;
        }
        float matrix[16];
        ConvertToD3DMatrix(local, matrix);
        device->vtbl->SetTransform(device, kWorldTransform, matrix);
        result = device->vtbl->SetRenderState(device, kNormalizeNormals, second ? 1 : 0);
        GEPolygonArrayLayout* polygons = model->polygonArrays[lod];
        const unsigned short* indices = polygons ? *reinterpret_cast<const unsigned short* const*>(reinterpret_cast<const char*>(polygons) + 4) : 0;
        for (ulong index = 0; indices && index < model->materialCount; index++) {
            GEMaterialLayout* material = reinterpret_cast<GEMaterialLayout*>(model->materials[index]);
            ulong polygonCount = material->polygonLength[lod];
            if (polygonCount == 0) {
                continue;
            }
            if (material->vertexLength > 0x98a || !model->vertexArrays[model->currentFrame] || !model->vertexArrays[model->nextFrame]) {
                break;
            }
            state->scenePolyCount += polygonCount;
            SetTexture(material->textures ? reinterpret_cast<GETexture*>(material->textures[screen]) : 0, screen);
            float materialData[17] = {material->color.r, material->color.g, material->color.b, Product(material->opacity, model->opacity),
                                      material->color.r, material->color.g, material->color.b, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            device->vtbl->SetMaterial(device, materialData);
            void* ring = 0;
            LockVB(material->vertexLength, screen, ring);
            if (!ring) {
                UnlockVB(screen);
                result = 1;
                break;
            }
            void* destination = static_cast<char*>(ring) + record->vertexCursor * 0x24;
            const float fraction = model->fractionalFramePosition;
            const bool coloured = reinterpret_cast<GEVertexArray*>(model->vertexArrays[0])->GetType() != 0;
            const bool still = !(fraction < 0.0f || fraction > 0.0f);
            if (coloured) {
                if (still) {
                    CopyStdColorVertices(model, material, destination);
                } else {
                    CopyAndSmoothStdColorVertices(model, material, destination);
                }
            } else if (still) {
                CopyStdVertices(model, material, destination);
            } else {
                CopyAndSmoothStdVertices(model, material, destination);
            }
            result = UnlockAndDraw(screen, 4, material->vertexLength, indices + material->polygonStart[lod] * 3, polygonCount * 3);
        }
    }
    for (ulong child = 0; child < model->subModelCount && result == 0; child++) {
        if (!model->subModels[child]) {
            continue;
        }
        result = DrawModelHierarchy(reinterpret_cast<GEModel*>(model->subModels[child]), screen, detail, orthographic);
    }
    StackPop();
    return result;
}

long EEWebNullRasterizer::Draw(GEModel* model, ulong screen, float detail, bool orthographic)
{
    if (Note(EEWebCheckModel(model)) != EEWEB_LAYOUT_OK) {
        return EEWEB_LAYOUT_VIOLATION;
    }
    if (!Record(screen) || !Device(screen)) {
        return EEWEB_NOT_SUPPORTED;
    }
    void* section = Base()->criticalSection;
    if (section) {
        EEWebCriticalSectionEnter(section);
    }
    Set3DMode(screen);
    long result = DrawModelHierarchy(model, screen, detail, orthographic);
    if (section) {
        EEWebCriticalSectionLeave(section);
    }
    return result;
}

long EEWebNullRasterizer::DrawSky(GEModel* model, ulong screen, float detail, bool orthographic)
{
    Draw(model, screen, detail, orthographic);
    return 0;
}
