// The engine's narrow string class, declared only so that the compiler emits the engine's own
// MSVC-decorated constructor and destructor calls.  The class is built and destroyed by code in
// "Low-Level Engine.dll" (imports: lle_imports.def); the layout below is just its size and
// vtable pointer: { vtable, char* chars, unsigned length, unsigned capacity }.
#ifndef EEWEB_ENGINE_USTRING_H
#define EEWEB_ENGINE_USTRING_H

class UString {
public:
    UString(const char* text);   // ??0UString@@QAE@PBD@Z
    virtual ~UString();          // ??1UString@@UAE@XZ

private:
    char m_engineOwned[12];
};

static_assert(sizeof(UString) == 16, "the engine's UString is 16 bytes");

// Placement new, because this DLL has no standard library headers.
inline void* operator new(__SIZE_TYPE__, void* where)
{
    return where;
}

#endif
