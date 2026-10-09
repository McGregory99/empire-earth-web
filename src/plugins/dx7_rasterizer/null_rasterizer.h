// A small class hierarchy with the vtable features of the engine's GERasterizer family, and the
// null rasterizer built from it.  The slot layout is the contract in rasterizer_abi.h; the
// harness calls it through positional thiscall slots to prove clang reproduces the layout MSVC 6
// gave the real classes.  It will be replaced by the real GERasterizer / DX7Rasterizer shapes
// (task P2.2); until then it is what RPIMGetRasterizer hands out.
#ifndef EEWEB_NULL_RASTERIZER_H
#define EEWEB_NULL_RASTERIZER_H

class EEWebRasterizerBase {
public:
    EEWebRasterizerBase();

    virtual long Activate() = 0;                // slot 0
    virtual long GetScreenCount() const = 0;    // slot 1
    // MSVC lays an overload set out in reverse declaration order, at the position of its first
    // member: Draw(float, long) takes slot 2 and Draw(long) slot 3, with Describe() between them
    // in declaration order but after the whole set.
    virtual long Draw(long mode);               // slot 3
    virtual long Describe();                    // slot 4
    virtual long Draw(float scale, long mode);  // slot 2
    virtual ~EEWebRasterizerBase();             // slot 5: deleting destructor
    virtual long Deactivate();                  // slot 6
    virtual long GetCounter(long which) const;  // slot 7

    // Not virtual, so not part of the vtable.
    void NoteSelfCheck(bool passed);

protected:
    long m_activations;
    long m_deactivations;
    long m_selfChecks;
    long m_selfCheckFailures;
};

class EEWebNullRasterizer : public EEWebRasterizerBase {
public:
    EEWebNullRasterizer();
    ~EEWebNullRasterizer();

    long Activate();
    long GetScreenCount() const;
    long Draw(long mode);
    long Draw(float scale, long mode);
    // Describe() is inherited on purpose: an inherited slot must keep pointing at the base.
};

// Exercises the class the way the engine will: allocates a temporary through an opaque factory
// (so the calls cannot be devirtualized), calls it through the base class, casts it with RTTI
// and deletes it through the virtual destructor.  The outcome is recorded on `shared`.
void RunClassSelfCheck(EEWebRasterizerBase& shared);

#endif
