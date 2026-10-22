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
// Behind its vptr the object has the layout of the original's DX7Rasterizer (DX7RasterizerLayout, task P2.5):
// the engine's GERasterizer fields, built by the engine's own constructor, and the plugin's list of screen
// records with the Direct3D device each screen renders with.  The device and scene lifecycle (task P2.6.5, dx7_scene.cpp)
// creates and releases that device, binds the render target and presents.  The state setters (task P2.6.4, dx7_state.cpp)
// call the engine's own GERasterizer::SetX like the original does, so the engine's per-screen state, which
// it reads through its non-virtual accessors, is what the plugin keeps.
#ifndef EEWEB_NULL_RASTERIZER_H
#define EEWEB_NULL_RASTERIZER_H

#include "ddraw7_min.h"
#include "dx7_screen.h"
#include "ge_interfaces.h"
#include "ge_layouts.h"
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
    long ActivateScreen(unsigned long, GETextureManager *, GEWindow *, unsigned long, bool, unsigned long) override;  // 0
    long ActivateRenderingSurface(unsigned long, GETextureManager *, GEWindow *, unsigned long, unsigned long) override { return EEWEB_NOT_SUPPORTED; }  // 1
    long AddLight(GELight &light, unsigned long, unsigned long &) override;  // 2
    long AddValidScreen(GEScreen *) override;  // 3
    long BeginScene(GESurface *, unsigned long) override;  // 4
    bool CanDoCurrentState(GETexture *, unsigned long) override;  // 5
    long Clear(GEColor const &, GERectangle const &, unsigned long) override;  // 6
    long ClearZBuffer(float, GERectangle const &, unsigned long) override;  // 7
    GEMovie * CreateMovie(FSFileSpec const &, unsigned long) override { return 0; }  // 9
    GEMovie * CreateMovie(FSFileSpec const &, unsigned long, GERectangle const &, bool) override { return 0; }  // 8
    GEPolygonArray * CreatePolygonArray(unsigned long) override;  // 10 (dx7_arrays.cpp)
    GEVertexArray * CreateVertexArray(unsigned long, unsigned long) override;  // 11 (dx7_arrays.cpp)
    long Deactivate() override;  // 12
    long Draw(GEBitmap *, unsigned long) override;  // 21 (dx7_draw2d.cpp)
    long Draw(GELine *, unsigned long, unsigned long) override;  // 20 (dx7_draw2d.cpp)
    long Draw(GEModel *, unsigned long, float, bool) override;  // 19 (dx7_model.cpp)
    long Draw(GEPoint *, unsigned long, unsigned long) override;  // 18 (dx7_draw2d.cpp)
    long Draw(GERhombus *, unsigned long) override;  // 17 (dx7_draw2d.cpp)
    long Draw(GEText *, GESurface *) override;  // 16 (dx7_text.cpp)
    long Draw(GEText *, GERectangle const &, GESurface *) override;  // 15 (dx7_text.cpp)
    long Draw(GETerrainMesh &, unsigned long, bool) override;  // 14 (dx7_terrain.cpp)
    long Draw(GEPolygonArray &, unsigned long, GEVertexArray &, unsigned long, GETexture *, unsigned long, bool) override;  // 13 (dx7_draw3d.cpp)
    long Draw3DLine(std::vector<std::pair<GE3DPoint, float>, std::allocator<std::pair<GE3DPoint, float>>> &, GETexture *, float, unsigned long, bool) override;  // 22 (dx7_draw3d.cpp)
    long DrawGridLines(GETerrainMesh &, std::vector<std::pair<U2DPoint<long>, U2DPoint<long>>, std::allocator<std::pair<U2DPoint<long>, U2DPoint<long>>>> &, GETexture *, float, unsigned long, bool) override { return EEWEB_NOT_SUPPORTED; }  // 23
    long DrawSky(GEModel *, unsigned long, float, bool) override;  // 24 (dx7_model.cpp)
    long DrawTerrainGrid(GETerrainMesh &mesh, unsigned long, bool) override { return Verdict(EEWebCheckTerrainMesh(&mesh), EEWEB_NOT_SUPPORTED); }  // 25
    long DrawTerrainHighlight(GETerrainMesh &mesh, U2DRectangle<long> const &, unsigned long, bool, GEColor const &, float) override { return Verdict(EEWebCheckTerrainMesh(&mesh), EEWEB_NOT_SUPPORTED); }  // 26
    long DrawWater(GETerrainMesh &mesh, unsigned long, bool) override { return Verdict(EEWebCheckTerrainMesh(&mesh), EEWEB_NOT_SUPPORTED); }  // 27
    long EndScene(unsigned long) override;  // 28
    long GetAvailableVideoMemory(unsigned long, unsigned long &) override;  // 29
    long GetTotalVideoMemory(unsigned long, unsigned long &) override;  // 30
    unsigned long GetCaps(unsigned long, unsigned long) override;  // 31
    bool GetCanModulateTextureAlpha(unsigned long) override;  // 32
    bool GetDirtyRectangleSupport(unsigned long) override;  // 33
    void Light(GETerrainMesh &mesh, unsigned long) override { Note(EEWebCheckTerrainMesh(&mesh)); }  // 34
    long RemoveAllLights(unsigned long) override;  // 35
    long RemoveLight(unsigned long, unsigned long) override;  // 36
    void SetAlphaTesting(unsigned long, unsigned long) override;  // 37
    void SetAlphaTestingCompareState(unsigned long, unsigned long) override;  // 38
    void SetAlphaTestingRefValue(float, unsigned long) override;  // 39
    void SetAmbientColor(GEColor const &, unsigned long) override;  // 40
    void SetAmbientIntensity(float, unsigned long) override;  // 41
    void SetAntiAliasing(unsigned long, unsigned long) override;  // 42
    void SetBlending(unsigned long, unsigned long, unsigned long) override;  // 43
    void SetColorKeying(unsigned long, unsigned long) override;  // 44
    void SetCulling(unsigned long) override;  // 45
    void SetDebugColor(GEColor const &) override;  // 46
    void SetDisplayNoTextureMode(bool, unsigned long) override;  // 47
    void SetDisplayOverdrawMode(bool, unsigned long) override;  // 48
    void SetDithering(unsigned long, unsigned long) override;  // 49
    void SetDrawStyle(unsigned long, unsigned long) override;  // 50
    void SetFog(unsigned long, unsigned long) override;  // 51
    void SetFogColor(GEColor const &, unsigned long) override;  // 52
    void SetFogRange(float, float, unsigned long) override;  // 53
    void SetFogType(unsigned long, unsigned long) override;  // 54
    void SetFogValue(float, unsigned long) override;  // 55
    void SetLighting(unsigned long, unsigned long) override;  // 56
    void SetProjectionMode(unsigned long, unsigned long) override;  // 57
    void SetShading(unsigned long, unsigned long) override;  // 58
    void SetShadingColorSource(unsigned long, unsigned long) override;  // 59
    void SetSpecular(unsigned long, unsigned long) override;  // 60
    void SetTexture(GETexture *, unsigned long) override;  // 61
    void SetViewport(GEViewport const &, unsigned long) override;  // 62
    void SetWaitForVSync(bool, unsigned long) override;  // 63
    void SetZBuffer(unsigned long, unsigned long) override;  // 64
    void SetZCompare(unsigned long, unsigned long) override;  // 65
    long ShowScene(unsigned long) override;  // 66
    void TransformToCamera(GETerrainMesh &, GEViewport &, float, bool, bool) override;  // 67 (dx7_terrain.cpp)
    long UpdateLight(unsigned long, GELight &, unsigned long) override;  // 68
protected:
    long ActivateRasterizer() override;  // 70
    long ActivateTheScreen(unsigned long, GESurface *, GESurface *, GETextureManager *, GEWindow *) override;  // 71
    long CreateTextureHandle(GETexture *) override { return 0; }  // 72: the original returns 0 and does nothing (the engine calls it when a texture gets a surface)
    long DemoteTexture(GETexture *) override { return 0; }  // 73: same code
    void DestroyTextureHandle(GETexture *) override {}  // 74
    long DrawModelHierarchy(GEModel *, unsigned long, float, bool) override;  // 75 (dx7_model.cpp)
    long PromoteTexture(GETexture *) override { return 0; }  // 76: same code
    void SetRenderTarget(GESurface *, unsigned long) override;  // 77
public:
    bool GetIsScreenValid(DX7Screen *) override;  // 78
protected:
    long CreateVertexBuffers(unsigned long) override;  // 79
    long DrawTerrainMaterial(GEViewport *, std::vector<GETerrainMesh::GETMPolygon *, std::allocator<GETerrainMesh::GETMPolygon *>> &, unsigned long) override;  // 80 (dx7_terrain.cpp)
    long GetVideoMemory(unsigned long, unsigned long &, unsigned long &) override;  // 81

private:
    // The layout canaries' bookkeeping: Note counts a refusal (and returns the canary's status); Verdict
    // turns a refusal into the status EEWEB_LAYOUT_VIOLATION and otherwise returns `result`.
    bool BindTexture(GETexture *texture, unsigned long screen);  // SetTexture's body (dx7_texture.cpp); false when nothing could be done
    long Note(long canaryStatus);
    long Verdict(long canaryStatus, long result);

    // Called for each DirectDraw device ActivateRasterizer finds (see null_rasterizer.cpp).
    static int __attribute__((stdcall)) EnumerateDevice(GUID_ *, char *, char *, void *, void *);
    void ClearScreens();
    void ReleaseDeviceObjects(DX7ScreenRecordLayout* record, unsigned long index); // texture, vertex buffer, lights, device, Direct3D

    // The object's two views of itself (ge_layouts.h): the engine's base class and the plugin's own fields.
    GERasterizerLayout *Base() { return reinterpret_cast<GERasterizerLayout *>(this); }
    DX7RasterizerLayout *Own() { return reinterpret_cast<DX7RasterizerLayout *>(this); }
    unsigned long RecordCount();
    DX7ScreenRecordLayout *Record(unsigned long screen);       // null when there is no such screen
    GERScreenStateLayout *State(unsigned long screen);         // the engine's record of the screen, null when there is none
    IDirect3DDevice7 *Device(unsigned long screen);            // null until the screen has a device
    DX7Screen *ScreenOf(unsigned long screen);
    void AddRecord(DX7ScreenRecordLayout *record);

    // The projection set-up the draws call before drawing (dx7_state.cpp): the original's protected Set2DMode, Set3DMode and
    // ConvertToD3DMatrix, which the DLL also exports under their original names for the test harness (eewebrasterizer.def).
    long Set2DMode(unsigned long screen);
    long Set3DMode(unsigned long screen);
    void ConvertToD3DMatrix(const GETransformationLayout &transformation, float *matrix);

    // The vertex copies the model draw uses (dx7_model.cpp; protected members of DX7Rasterizer, exported under the original's names for the
    // harness): the `vertexLength` vertices of `material` from the model's arrays into the ring at `destination` (36-byte vertices); the
    // smoothing variants blend the current and the next frame by the model's fraction.
    void CopyStdVertices(const GEModelLayout *model, const GEMaterialLayout *material, void *destination);
    void CopyStdColorVertices(const GEModelLayout *model, const GEMaterialLayout *material, void *destination);
    void CopyAndSmoothStdVertices(const GEModelLayout *model, const GEMaterialLayout *material, void *destination);
    void CopyAndSmoothStdColorVertices(const GEModelLayout *model, const GEMaterialLayout *material, void *destination);

    // The ring vertex buffer's helpers the draws use (dx7_vertex_ring.cpp): lock `count` vertices of the screen's buffer, hand back the
    // buffer's start (the caller adds cursor * 0x24); the DLL exports them under the original's names for the harness.
    long LockVB(unsigned long count, unsigned long screen, void *&vertices);
    long UnlockVB(unsigned long screen);

    // The tail every draw shares (dx7_draw2d.cpp): unlock the ring, draw the `vertices` just written (indexed by `indices`, `indexCount` of them, or
    // as a plain list of `primitive` type when `indices` is null) retrying while Direct3D is busy, and move the cursor on.  Answers the unlock's status.
    long DrawTextOn(GEText *text, const GERectangleLayout *clip, GESurface *surface);  // the text draws' body (dx7_text.cpp)
    long UnlockAndDraw(unsigned long screen, unsigned long primitive, unsigned long vertices, const unsigned short *indices, unsigned long indexCount);

    // Behind the vptr come the engine's GERasterizer fields (0x4..0x68) and the plugin's (0x68..0x7c), whose
    // bytes are the engine's and the layouts' to define; nothing of this class touches them directly.
    unsigned char m_engineFields[EEWEB_COUNTERS_OFFSET - 4];

    // Right behind them: the harness reads the counters from there (EEWEB_COUNTERS_OFFSET).
    volatile long m_counters[EEWEB_COUNTER_COUNT];
    volatile long m_engineBaseAlive; // the engine's constructor ran and its destructor has not
};

// Exercises the class the way the engine will: allocates a temporary through an opaque factory (so the
// calls cannot be devirtualized), calls it through the base class, casts it with RTTI and deletes it
// through the virtual destructor.  The outcome is recorded on `shared`.
void RunClassSelfCheck(EEWebNullRasterizer& shared);

#endif
