#include "null_rasterizer.h"

#include "rasterizer_abi.h"

namespace {

long g_destroyed; // destructor runs of any EEWebNullRasterizer in this DLL (read by the self-check only)

DX7RasterizerAbi* MakeNullRasterizer()
{
    return new EEWebNullRasterizer;
}

// Called through a volatile pointer so the optimizer sees an unknown dynamic type.
DX7RasterizerAbi* (*volatile g_factory)() = &MakeNullRasterizer;

} // namespace

EEWebNullRasterizer::EEWebNullRasterizer()
{
    // The harness reads the counters at this offset, behind the vptr.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(__builtin_offsetof(EEWebNullRasterizer, m_counters) == EEWEB_COUNTERS_OFFSET,
                  "the counters must sit right behind the vptr");
#pragma clang diagnostic pop
    for (int k = 0; k < EEWEB_COUNTER_COUNT; ++k) {
        m_counters[k] = 0;
    }
}

EEWebNullRasterizer::~EEWebNullRasterizer()
{
    ++g_destroyed;
    ++m_counters[EEWEB_COUNTER_DESTROYED];
}

long EEWebNullRasterizer::Activate()
{
    return ActivateRasterizer();
}

unsigned long EEWebNullRasterizer::GetScreenCount() const
{
    return 0;
}

void EEWebNullRasterizer::NoteSelfCheck(bool passed)
{
    ++m_counters[EEWEB_COUNTER_SELF_CHECKS];
    if (!passed) {
        ++m_counters[EEWEB_COUNTER_SELF_CHECK_FAILURES];
    }
}

long EEWebNullRasterizer::ActivateRasterizer()
{
    ++m_counters[EEWEB_COUNTER_ACTIVATIONS];
    return EEWEB_ACTIVATE_FAILED; // nothing to render with: activation always fails
}

long EEWebNullRasterizer::Deactivate()
{
    ++m_counters[EEWEB_COUNTER_DEACTIVATIONS];
    return 0;
}

long EEWebNullRasterizer::Draw(GEBitmap*, unsigned long mode)
{
    return EEWEB_DRAW_BITMAP_BASE + static_cast<long>(mode);
}

long EEWebNullRasterizer::Draw(GELine*, unsigned long first, unsigned long second)
{
    return EEWEB_DRAW_LINE_BASE + static_cast<long>(first + second);
}

void RunClassSelfCheck(EEWebNullRasterizer& shared)
{
    DX7RasterizerAbi* made = g_factory();
    EEWebNullRasterizer* temp = dynamic_cast<EEWebNullRasterizer*>(made);
    bool ok = temp != 0;
    if (temp) {
        ok = ok && temp->GetScreenCount() == 0;
        ok = ok && made->Draw(static_cast<GEBitmap*>(0), 3UL) == EEWEB_DRAW_BITMAP_BASE + 3;
        ok = ok && made->Draw(static_cast<GELine*>(0), 2UL, 3UL) == EEWEB_DRAW_LINE_BASE + 5;
        ok = ok && !made->GetIsScreenValid(0);
        ok = ok && dynamic_cast<EEWebNullRasterizer*>(static_cast<DX7RasterizerAbi*>(&shared)) != 0;
        const long destroyedBefore = g_destroyed;
        delete temp; // virtual call through the deleting-destructor slot, then operator delete
        ok = ok && g_destroyed == destroyedBefore + 1;
    }
    shared.NoteSelfCheck(ok);
}
