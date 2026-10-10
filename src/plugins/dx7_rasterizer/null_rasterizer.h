// The null rasterizer, what RPIMGetRasterizer hands out until the real DX7TnLRasterizer is
// reimplemented (tasks P2.6/P2.7).  It has the shape of the original's object: it derives from the
// generated DX7RasterizerAbi (GERasterizer's 78 vtable slots and the 4 that DX7Rasterizer adds, in the
// original's order; see ../../engine_abi/ge_interfaces.h) and overrides every slot with a null
// operation, so the harness can drive it through the positional vtable like the engine would.
//
// The engine never reaches these methods: PIMIsValid() says "not valid" and the module is unloaded.  The
// operations that return a status say "not supported", the ones that return a pointer return null.  Only
// ActivateRasterizer, Deactivate, the destructor and the last two Draw overloads do anything observable;
// they count their calls where the harness can read them (rasterizer_abi.h) or return a marker.
//
// This class has no engine fields behind its vptr: the layouts GERasterizer and DX7Rasterizer give the
// real object are task P2.5.
#ifndef EEWEB_NULL_RASTERIZER_H
#define EEWEB_NULL_RASTERIZER_H

#include "ge_interfaces.h"
#include "rasterizer_abi.h"

class EEWebNullRasterizer : public DX7RasterizerAbi {
public:
    EEWebNullRasterizer();
    ~EEWebNullRasterizer();  // slot 69: the deleting destructor of the vtable, taking a flags argument

    // Not virtual, like GERasterizer::Activate() and GetScreenCount() in the engine, which PIMIsValid uses.
    long Activate();                         // calls the virtual ActivateRasterizer()
    unsigned long GetScreenCount() const;
    void NoteSelfCheck(bool passed);

    // The vtable, in the declaration order of the generated header (the comment is the slot): overload
    // sets are declared in reverse slot order, which is how MSVC lays them out.
    long ActivateScreen(unsigned long, GETextureManager *, GEWindow *, unsigned long, bool, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 0
    long ActivateRenderingSurface(unsigned long, GETextureManager *, GEWindow *, unsigned long, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 1
    long AddLight(GELight &, unsigned long, unsigned long &) override { return EEWEB_NOT_SUPPORTED; }  // 2
    long AddValidScreen(GEScreen *) override { return EEWEB_NOT_SUPPORTED; }  // 3
    long BeginScene(GESurface *, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 4
    bool CanDoCurrentState(GETexture *, unsigned long) override { return false; }  // 5
    long Clear(GEColor const &, GERectangle const &, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 6
    long ClearZBuffer(float, GERectangle const &, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 7
    GEMovie * CreateMovie(FSFileSpec const &, unsigned long) override { return 0; }  // 9
    GEMovie * CreateMovie(FSFileSpec const &, unsigned long, GERectangle const &, bool) override { return 0; }  // 8
    GEPolygonArray * CreatePolygonArray(unsigned long) override { return 0; }  // 10
    GEVertexArray * CreateVertexArray(unsigned long, unsigned long) override { return 0; }  // 11
    long Deactivate() override;  // 12
    long Draw(GEBitmap *, unsigned long) override;  // 21
    long Draw(GELine *, unsigned long, unsigned long) override;  // 20
    long Draw(GEModel *, unsigned long, float, bool) override { return EEWEB_NOT_SUPPORTED; }  // 19
    long Draw(GEPoint *, unsigned long, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 18
    long Draw(GERhombus *, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 17
    long Draw(GEText *, GESurface *) override { return EEWEB_NOT_SUPPORTED; }  // 16
    long Draw(GEText *, GERectangle const &, GESurface *) override { return EEWEB_NOT_SUPPORTED; }  // 15
    long Draw(GETerrainMesh &, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 14
    long Draw(GEPolygonArray &, unsigned long, GEVertexArray &, unsigned long, GETexture *, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 13
    long Draw3DLine(std::vector<std::pair<GE3DPoint, float>, std::allocator<std::pair<GE3DPoint, float>>> &, GETexture *, float, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 22
    long DrawGridLines(GETerrainMesh &, std::vector<std::pair<U2DPoint<long>, U2DPoint<long>>, std::allocator<std::pair<U2DPoint<long>, U2DPoint<long>>>> &, GETexture *, float, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 23
    long DrawSky(GEModel *, unsigned long, float, bool) override { return EEWEB_NOT_SUPPORTED; }  // 24
    long DrawTerrainGrid(GETerrainMesh &, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 25
    long DrawTerrainHighlight(GETerrainMesh &, U2DRectangle<long> const &, unsigned long, bool, GEColor const &, float) override { return EEWEB_NOT_SUPPORTED; }  // 26
    long DrawWater(GETerrainMesh &, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 27
    long EndScene(unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 28
    long GetAvailableVideoMemory(unsigned long, unsigned long &) override { return EEWEB_NOT_SUPPORTED; }  // 29
    long GetTotalVideoMemory(unsigned long, unsigned long &) override { return EEWEB_NOT_SUPPORTED; }  // 30
    unsigned long GetCaps(unsigned long, unsigned long) override { return 0; }  // 31
    bool GetCanModulateTextureAlpha(unsigned long) override { return false; }  // 32
    bool GetDirtyRectangleSupport(unsigned long) override { return false; }  // 33
    void Light(GETerrainMesh &, unsigned long) override {}  // 34
    long RemoveAllLights(unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 35
    long RemoveLight(unsigned long, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 36
    void SetAlphaTesting(unsigned long, unsigned long) override {}  // 37
    void SetAlphaTestingCompareState(unsigned long, unsigned long) override {}  // 38
    void SetAlphaTestingRefValue(float, unsigned long) override {}  // 39
    void SetAmbientColor(GEColor const &, unsigned long) override {}  // 40
    void SetAmbientIntensity(float, unsigned long) override {}  // 41
    void SetAntiAliasing(unsigned long, unsigned long) override {}  // 42
    void SetBlending(unsigned long, unsigned long, unsigned long) override {}  // 43
    void SetColorKeying(unsigned long, unsigned long) override {}  // 44
    void SetCulling(unsigned long) override {}  // 45
    void SetDebugColor(GEColor const &) override {}  // 46
    void SetDisplayNoTextureMode(bool, unsigned long) override {}  // 47
    void SetDisplayOverdrawMode(bool, unsigned long) override {}  // 48
    void SetDithering(unsigned long, unsigned long) override {}  // 49
    void SetDrawStyle(unsigned long, unsigned long) override {}  // 50
    void SetFog(unsigned long, unsigned long) override {}  // 51
    void SetFogColor(GEColor const &, unsigned long) override {}  // 52
    void SetFogRange(float, float, unsigned long) override {}  // 53
    void SetFogType(unsigned long, unsigned long) override {}  // 54
    void SetFogValue(float, unsigned long) override {}  // 55
    void SetLighting(unsigned long, unsigned long) override {}  // 56
    void SetProjectionMode(unsigned long, unsigned long) override {}  // 57
    void SetShading(unsigned long, unsigned long) override {}  // 58
    void SetShadingColorSource(unsigned long, unsigned long) override {}  // 59
    void SetSpecular(unsigned long, unsigned long) override {}  // 60
    void SetTexture(GETexture *, unsigned long) override {}  // 61
    void SetViewport(GEViewport const &, unsigned long) override {}  // 62
    void SetWaitForVSync(bool, unsigned long) override {}  // 63
    void SetZBuffer(unsigned long, unsigned long) override {}  // 64
    void SetZCompare(unsigned long, unsigned long) override {}  // 65
    long ShowScene(unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 66
    void TransformToCamera(GETerrainMesh &, GEViewport &, float, bool, bool) override {}  // 67
    long UpdateLight(unsigned long, GELight &, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 68
protected:
    long ActivateRasterizer() override;  // 70
    long ActivateTheScreen(unsigned long, GESurface *, GESurface *, GETextureManager *, GEWindow *) override { return EEWEB_NOT_SUPPORTED; }  // 71
    long slot72(GETexture *) override { return EEWEB_NOT_SUPPORTED; }  // 72
    long slot73(GETexture *) override { return EEWEB_NOT_SUPPORTED; }  // 73
    void DestroyTextureHandle(GETexture *) override {}  // 74
    long DrawModelHierarchy(GEModel *, unsigned long, float, bool) override { return EEWEB_NOT_SUPPORTED; }  // 75
    long slot76(GETexture *) override { return EEWEB_NOT_SUPPORTED; }  // 76
    void SetRenderTarget(GESurface *, unsigned long) override {}  // 77
public:
    bool GetIsScreenValid(DX7Screen *) override { return false; }  // 78
protected:
    long CreateVertexBuffers(unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 79
    long DrawTerrainMaterial(GEViewport *, std::vector<GETerrainMesh::GETMPolygon *, std::allocator<GETerrainMesh::GETMPolygon *>> &, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 80
    long GetVideoMemory(unsigned long, unsigned long &, unsigned long &) override { return EEWEB_NOT_SUPPORTED; }  // 81

private:
    // Right behind the vptr: the harness reads them from there (EEWEB_COUNTERS_OFFSET).
    volatile long m_counters[EEWEB_COUNTER_COUNT];
};

// Exercises the class the way the engine will: allocates a temporary through an opaque factory (so the
// calls cannot be devirtualized), calls it through the base class, casts it with RTTI and deletes it
// through the virtual destructor.  The outcome is recorded on `shared`.
void RunClassSelfCheck(EEWebNullRasterizer& shared);

#endif
