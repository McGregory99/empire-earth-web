// Makes clang compute the Microsoft vtable layout of every generated interface class, so that
// `-Xclang -fdump-vtable-layouts` prints it (see check_shapes.py).  The classes are abstract and
// never instantiated: the layout is needed as soon as the address of a virtual member is taken, so
// one member per class is enough (for DX7RasterizerAbi, one of the slots it adds to GERasterizer).
#include "ge_interfaces.h"

namespace {

template <class MemberPointer>
void force_layout(MemberPointer member)
{
    static MemberPointer volatile held;
    held = member;
}

} // namespace

void force_layouts()
{
    force_layout(&GERasterizer::Deactivate);
    force_layout(&DX7RasterizerAbi::GetIsScreenValid);
    force_layout(&GEScreen::SetMode);
    force_layout(&GESurface::Lock);
    force_layout(&GEMovie::Stop);
    force_layout(&GEVertexArray::SizeOf);
    force_layout(&GEPolygonArray::SizeOf);
}
