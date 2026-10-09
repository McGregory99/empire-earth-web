#include "null_rasterizer.h"

#include "rasterizer_abi.h"

namespace {

long g_destroyed;     // destructor runs of any EEWebNullRasterizer in this DLL
long g_baseDestroyed; // destructor runs of any EEWebRasterizerBase in this DLL

EEWebRasterizerBase* MakeNullRasterizer()
{
    return new EEWebNullRasterizer;
}

// Called through a volatile pointer so the optimizer sees an unknown dynamic type.
EEWebRasterizerBase* (*volatile g_factory)() = &MakeNullRasterizer;

} // namespace

EEWebRasterizerBase::EEWebRasterizerBase()
    : m_activations(0), m_deactivations(0), m_selfChecks(0), m_selfCheckFailures(0)
{
}

EEWebRasterizerBase::~EEWebRasterizerBase()
{
    ++g_baseDestroyed;
}

long EEWebRasterizerBase::Draw(long mode)
{
    return EEWEB_DRAW_PLAIN_BASE + mode;
}

long EEWebRasterizerBase::Draw(float scale, long mode)
{
    return EEWEB_DRAW_SCALED_BASE + mode + static_cast<long>(scale);
}

long EEWebRasterizerBase::Describe()
{
    return EEWEB_DESCRIBE_RESULT;
}

long EEWebRasterizerBase::Deactivate()
{
    ++m_deactivations;
    return 0;
}

long EEWebRasterizerBase::GetCounter(long which) const
{
    switch (which) {
    case EEWEB_COUNTER_ACTIVATIONS:
        return m_activations;
    case EEWEB_COUNTER_DEACTIVATIONS:
        return m_deactivations;
    case EEWEB_COUNTER_SELF_CHECKS:
        return m_selfChecks;
    case EEWEB_COUNTER_SELF_CHECK_FAILURES:
        return m_selfCheckFailures;
    case EEWEB_COUNTER_DESTROYED:
        return g_destroyed;
    case EEWEB_COUNTER_BASE_DESTROYED:
        return g_baseDestroyed;
    }
    return -1;
}

void EEWebRasterizerBase::NoteSelfCheck(bool passed)
{
    ++m_selfChecks;
    if (!passed) {
        ++m_selfCheckFailures;
    }
}

EEWebNullRasterizer::EEWebNullRasterizer()
{
}

EEWebNullRasterizer::~EEWebNullRasterizer()
{
    ++g_destroyed;
}

long EEWebNullRasterizer::Activate()
{
    ++m_activations;
    return EEWEB_ACTIVATE_FAILED; // nothing to render with: activation always fails
}

long EEWebNullRasterizer::GetScreenCount() const
{
    return 0;
}

long EEWebNullRasterizer::Draw(long mode)
{
    return EEWEB_DRAW_PLAIN_BASE + mode;
}

long EEWebNullRasterizer::Draw(float scale, long mode)
{
    return EEWEB_DRAW_SCALED_BASE + mode + static_cast<long>(scale);
}

void RunClassSelfCheck(EEWebRasterizerBase& shared)
{
    EEWebRasterizerBase* temp = g_factory();
    bool ok = temp != 0;
    if (temp) {
        ok = ok && temp->GetScreenCount() == 0;
        ok = ok && temp->Draw(3L) == EEWEB_DRAW_PLAIN_BASE + 3;
        ok = ok && temp->Draw(2.5f, 3L) == EEWEB_DRAW_SCALED_BASE + 3 + 2;
        ok = ok && temp->Describe() == EEWEB_DESCRIBE_RESULT;
        ok = ok && dynamic_cast<EEWebNullRasterizer*>(temp) != 0;
        ok = ok && dynamic_cast<EEWebNullRasterizer*>(&shared) != 0;
        const long destroyedBefore = g_destroyed;
        delete temp; // virtual call through the deleting-destructor slot, then operator delete
        ok = ok && g_destroyed == destroyedBefore + 1;
    }
    shared.NoteSelfCheck(ok);
}
