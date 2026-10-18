// The null rasterizer, what RPIMGetRasterizer hands out until the real DX7TnLRasterizer is
// reimplemented (tasks P2.6/P2.7).  It has the shape of the original's object: it derives from the
// generated DX7RasterizerAbi (GERasterizer's 78 vtable slots and the 4 that DX7Rasterizer adds, in the
// original's order; see ../../engine_abi/ge_interfaces.h) and overrides every slot with a null
// operation, so the harness can drive it through the positional vtable like the engine would.
//
// What it does (task P2.6.1): it activates by enumerating the DirectDraw devices and creating a DX7Screen
// for each one that has a hardware T&L Direct3D 7 device, so PIMIsValid() says "valid" on a machine that has
// one.  The drawing operations are still null: operations that return a status say "not supported", the ones
// that return a pointer return null.
//
// This class has no engine fields behind its vptr: the layouts GERasterizer and DX7Rasterizer give the
// real object are task P2.5.
#ifndef EEWEB_NULL_RASTERIZER_H
#define EEWEB_NULL_RASTERIZER_H

#include "dx7_screen.h"
#include "ge_interfaces.h"
#include "layout_canaries.h"
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
    long AddLight(GELight &light, unsigned long, unsigned long &) override { return Verdict(EEWebCheckLight(&light), EEWEB_NOT_SUPPORTED); }  // 2
    long AddValidScreen(GEScreen *) override;  // 3
    long BeginScene(GESurface *, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 4
    bool CanDoCurrentState(GETexture *texture, unsigned long) override { if (texture) Note(EEWebCheckTexture(texture)); return false; }  // 5
    long Clear(GEColor const &, GERectangle const &, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 6
    long ClearZBuffer(float, GERectangle const &, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 7
    GEMovie * CreateMovie(FSFileSpec const &, unsigned long) override { return 0; }  // 9
    GEMovie * CreateMovie(FSFileSpec const &, unsigned long, GERectangle const &, bool) override { return 0; }  // 8
    GEPolygonArray * CreatePolygonArray(unsigned long) override { return 0; }  // 10
    GEVertexArray * CreateVertexArray(unsigned long, unsigned long) override { return 0; }  // 11
    long Deactivate() override;  // 12
    long Draw(GEBitmap *, unsigned long) override;  // 21
    long Draw(GELine *, unsigned long, unsigned long) override;  // 20
    long Draw(GEModel *model, unsigned long, float, bool) override { return Verdict(EEWebCheckModel(model), EEWEB_NOT_SUPPORTED); }  // 19
    long Draw(GEPoint *, unsigned long, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 18
    long Draw(GERhombus *, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 17
    long Draw(GEText *text, GESurface *) override { return Verdict(EEWebCheckText(text), EEWEB_NOT_SUPPORTED); }  // 16
    long Draw(GEText *text, GERectangle const &, GESurface *) override { return Verdict(EEWebCheckText(text), EEWEB_NOT_SUPPORTED); }  // 15
    long Draw(GETerrainMesh &mesh, unsigned long, bool) override { return Verdict(EEWebCheckTerrainMesh(&mesh), EEWEB_NOT_SUPPORTED); }  // 14
    long Draw(GEPolygonArray &, unsigned long, GEVertexArray &, unsigned long, GETexture *, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 13
    long Draw3DLine(std::vector<std::pair<GE3DPoint, float>, std::allocator<std::pair<GE3DPoint, float>>> &, GETexture *, float, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 22
    long DrawGridLines(GETerrainMesh &, std::vector<std::pair<U2DPoint<long>, U2DPoint<long>>, std::allocator<std::pair<U2DPoint<long>, U2DPoint<long>>>> &, GETexture *, float, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 23
    long DrawSky(GEModel *model, unsigned long, float, bool) override { return Verdict(EEWebCheckModel(model), EEWEB_NOT_SUPPORTED); }  // 24
    long DrawTerrainGrid(GETerrainMesh &mesh, unsigned long, bool) override { return Verdict(EEWebCheckTerrainMesh(&mesh), EEWEB_NOT_SUPPORTED); }  // 25
    long DrawTerrainHighlight(GETerrainMesh &mesh, U2DRectangle<long> const &, unsigned long, bool, GEColor const &, float) override { return Verdict(EEWebCheckTerrainMesh(&mesh), EEWEB_NOT_SUPPORTED); }  // 26
    long DrawWater(GETerrainMesh &mesh, unsigned long, bool) override { return Verdict(EEWebCheckTerrainMesh(&mesh), EEWEB_NOT_SUPPORTED); }  // 27
    long EndScene(unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 28
    long GetAvailableVideoMemory(unsigned long, unsigned long &) override;  // 29
    long GetTotalVideoMemory(unsigned long, unsigned long &) override;  // 30
    unsigned long GetCaps(unsigned long, unsigned long) override { return 0; }  // 31
    bool GetCanModulateTextureAlpha(unsigned long) override { return false; }  // 32
    bool GetDirtyRectangleSupport(unsigned long) override { return false; }  // 33
    void Light(GETerrainMesh &mesh, unsigned long) override { Note(EEWebCheckTerrainMesh(&mesh)); }  // 34
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
    void SetTexture(GETexture *texture, unsigned long) override { if (texture) Note(EEWebCheckTexture(texture)); }  // 61
    void SetViewport(GEViewport const &, unsigned long) override {}  // 62
    void SetWaitForVSync(bool, unsigned long) override {}  // 63
    void SetZBuffer(unsigned long, unsigned long) override {}  // 64
    void SetZCompare(unsigned long, unsigned long) override {}  // 65
    long ShowScene(unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 66
    void TransformToCamera(GETerrainMesh &mesh, GEViewport &viewport, float, bool, bool) override { if (Note(EEWebCheckTerrainMesh(&mesh)) == EEWEB_LAYOUT_OK) Note(EEWebCheckViewport(&viewport)); }  // 67
    long UpdateLight(unsigned long, GELight &light, unsigned long) override { return Verdict(EEWebCheckLight(&light), EEWEB_NOT_SUPPORTED); }  // 68
protected:
    long ActivateRasterizer() override;  // 70
    long ActivateTheScreen(unsigned long, GESurface *, GESurface *, GETextureManager *, GEWindow *) override { return EEWEB_NOT_SUPPORTED; }  // 71
    long slot72(GETexture *) override { return EEWEB_NOT_SUPPORTED; }  // 72
    long slot73(GETexture *) override { return EEWEB_NOT_SUPPORTED; }  // 73
    void DestroyTextureHandle(GETexture *) override {}  // 74
    long DrawModelHierarchy(GEModel *model, unsigned long, float, bool) override { return Verdict(EEWebCheckModel(model), EEWEB_NOT_SUPPORTED); }  // 75
    long slot76(GETexture *) override { return EEWEB_NOT_SUPPORTED; }  // 76
    void SetRenderTarget(GESurface *, unsigned long) override {}  // 77
public:
    bool GetIsScreenValid(DX7Screen *) override;  // 78
protected:
    long CreateVertexBuffers(unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 79
    long DrawTerrainMaterial(GEViewport *, std::vector<GETerrainMesh::GETMPolygon *, std::allocator<GETerrainMesh::GETMPolygon *>> &, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 80
    long GetVideoMemory(unsigned long, unsigned long &, unsigned long &) override;  // 81

private:
    // The layout canaries' bookkeeping: Note counts a refusal (and returns the canary's status); Verdict
    // turns a refusal into the status EEWEB_LAYOUT_VIOLATION and otherwise returns `result`.
    long Note(long canaryStatus);
    long Verdict(long canaryStatus, long result);

    // Called for each DirectDraw device ActivateRasterizer finds (see null_rasterizer.cpp).
    static int __attribute__((stdcall)) EnumerateDevice(GUID_ *, char *, char *, void *, void *);
    void ClearScreens();

    // Right behind the vptr: the harness reads them from there (EEWEB_COUNTERS_OFFSET).
    volatile long m_counters[EEWEB_COUNTER_COUNT];

    // The valid screens, in the order they were added: this class's own list, standing in for the
    // engine-side vector of the base class (GERasterizerLayout.screens) until the object has the base layout.
    DX7Screen **m_screens;
    unsigned long m_screenCount;
    unsigned long m_screenCapacity;
};

// Exercises the class the way the engine will: allocates a temporary through an opaque factory (so the
// calls cannot be devirtualized), calls it through the base class, casts it with RTTI and deletes it
// through the virtual destructor.  The outcome is recorded on `shared`.
void RunClassSelfCheck(EEWebNullRasterizer& shared);

#endif
