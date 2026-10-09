// The few things a CRT-less DLL has to supply itself.

extern "C" {

// Referenced by the compiler in any translation unit that uses floating point.
int _fltused = 0;

// Every RTTI type descriptor begins with a pointer to the vtable of std::type_info.  VC6's
// static runtime library defines that vtable (its only entry is the destructor, imported from
// msvcrt.dll, which is why the original plugins import ??1type_info@@UAE@XZ); this DLL
// does the same instead of linking a static runtime.  (clang treats an asm label as verbatim
// already; a leading "\01" would end up as a stray character in the symbol name.)
void __attribute__((thiscall)) TypeInfoDestructor(void*) __asm__("??1type_info@@UAE@XZ");

extern void* const TypeInfoVftable[1] __asm__("??_7type_info@@6B@");
void* const TypeInfoVftable[1] = {reinterpret_cast<void*>(&TypeInfoDestructor)};

} // extern "C"
