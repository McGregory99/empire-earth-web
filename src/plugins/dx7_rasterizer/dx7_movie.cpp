// The movies (task P2.6.7k): the two CreateMovie overloads of the rasterizer (slots 9 and 8) and the DX7Movie they build.
//
// A DX7Movie plays a Bink movie on a thread of its own and hands the decoded frames to the game through a frame buffer, a surface it
// makes on the screen it plays on.  The engine sees a GEMovie: 8 virtual slots (the destructor, GetFilename and the flag getters, RenderFrame
// and Stop) behind a UReferenceCounter's count at offset 0x4.  The object has the 0x60 bytes of the original's (the member comments give
// the original's names where its assertion strings tell them); the thread, the critical section and the events are the engine's own
// (TSScheduler), the decoding is binkw32.dll's.  tests/dx7_rasterizer/movie_checks.h compares both on a scripted Bink (fakebink.c).
//
// The flow, as in the original:
//   - the constructor sets the fields, makes the process-wide movie critical section when there is none, and Initialize builds the
//     name (volume + directory + file of the FSFileSpec), takes the thread's reference (the count goes to 2), and makes the movie's
//     critical section, the thread (PlayBink) and its two events; the thread starts at once;
//   - PlayBink holds the process-wide critical section for the whole movie (two movies never play together), tells Bink about the
//     mixer's DirectSound object when it is a new one, opens the file, makes the frame buffer, and for every frame waits for Bink,
//     decodes, copies into the locked frame buffer, flags the frame ready and wakes RenderFrame; it ends when stopped, when it
//     reached the last frame of a movie that does not loop, or when the frame buffer could not be made;
//   - RenderFrame waits for a frame, then blits the frame buffer on the screen's render target; Stop raises the stop flag and waits
//     for the thread's last act.
// Two things the original does and this build keeps because they are observable: the rectangle given to the constructor with a size is
// replaced by the video's own size (Initialize clears the flag that says the rectangle is the caller's), and the thread drops its
// reference to the movie and then sets the event Stop waits for (so a movie whose last reference the thread drops is gone by then).
#include "null_rasterizer.h"

#include "dx7_surface.h"
#include "engine_imports.h"
#include "rasterizer_abi.h"

extern "C" {
__declspec(dllimport) void __stdcall Sleep(unsigned long milliseconds);
__declspec(dllimport) void* __stdcall GetModuleHandleA(const char* name);
__declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
}

// ---- the engine's scheduler, events, string formatting and floating-point control --------------------------------------------------------

class TSThread {
public:
    long SetPriority(long priority); // ?SetPriority@TSThread@@QAEJJ@Z
};

class TSEvent {
public:
    long Wait(unsigned long milliseconds); // ?Wait@TSEvent@@QAEJK@Z (0xffffffff: for ever)
    long Set();                            // ?Set@TSEvent@@QAEJXZ
    long Clear();                          // ?Clear@TSEvent@@QAEJXZ
};

class TSCriticalSection;

class TSScheduler {
public:
    static long CreateCriticalSection(TSCriticalSection*& section);
    static void DestroyCriticalSection(TSCriticalSection* section);
    static long CreateThread(TSThread*& thread, unsigned long(*procedure)(void*), void* argument, unsigned long, bool);
    static long DestroyThread(TSThread* thread);
    static long CreateEventA(TSEvent*& event, bool);
    static void DestroyEvent(TSEvent* event);
};

void UMSetFloatingPointPrecision(unsigned long precision);
unsigned long UMGetFloatingPointPrecision();
void UMSetFloatingPointRoundingMode(unsigned long mode);
unsigned long UMGetFloatingPointRoundingMode();
void ESVerify(const char* file, unsigned long line, const char* expression);

extern "C" {
void __attribute__((thiscall)) EEWebUStringConstruct(void* self) __asm__("??0UString@@QAE@XZ");
long EEWebUStringFormat(void* destination, const char* format, ...) __asm__("?sprintf@@YAJAAVUString@@PBDZZ");
}

// ---- Bink --------------------------------------------------------------------------------------------------------------------------

// The fields of Bink's BINK structure the player reads.
struct BINK {
    unsigned long width;
    unsigned long height;
    unsigned long frames;
    unsigned long frameNumber;
};

extern "C" {
__declspec(dllimport) BINK* __stdcall BinkOpen(const char* name, unsigned long flags) __asm__("__BinkOpen@8");
__declspec(dllimport) void __stdcall BinkClose(BINK* bink) __asm__("__BinkClose@4");
__declspec(dllimport) void __stdcall BinkNextFrame(BINK* bink) __asm__("__BinkNextFrame@4");
__declspec(dllimport) void* __stdcall BinkOpenDirectSound(unsigned long parameter) __asm__("__BinkOpenDirectSound@4");
__declspec(dllimport) unsigned long __stdcall BinkDDSurfaceType(void* surface) __asm__("__BinkDDSurfaceType@4");
__declspec(dllimport) int __stdcall BinkCopyToBuffer(BINK* bink, void* destination, long pitch, unsigned long height, unsigned long x, unsigned long y,
                                                    unsigned long flags) __asm__("__BinkCopyToBuffer@28");
__declspec(dllimport) long __stdcall BinkWait(BINK* bink) __asm__("__BinkWait@4");
__declspec(dllimport) int __stdcall BinkSetSoundSystem(void* open, void* directSound) __asm__("__BinkSetSoundSystem@8");
__declspec(dllimport) int __stdcall BinkDoFrame(BINK* bink) __asm__("__BinkDoFrame@4");
}

typedef unsigned long ulong;

namespace {

TSCriticalSection* g_movieSection; // the original's sMovieCriticalSection: the one movie that plays holds it
void* g_directSound;               // the original's sDirectSoundInterface: the DirectSound object Bink was told about last

// GERasterizer::sActiveRasterizer and SEMixer::sActiveMixer, private statics of the engine's classes: looked up by their decorated names.
void* EngineVariable(const char* name)
{
    void* engine = GetModuleHandleA("Low-Level Engine.dll");
    void* variable = engine ? GetProcAddress(engine, name) : 0;
    return variable ? *static_cast<void**>(variable) : 0;
}

GERasterizerLayout* ActiveRasterizer()
{
    return static_cast<GERasterizerLayout*>(EngineVariable("?sActiveRasterizer@GERasterizer@@1PAV1@A"));
}

void* ActiveMixer()
{
    return EngineVariable("?sActiveMixer@SEMixer@@0PAV1@A");
}

// The engine's record of one screen of the active rasterizer.
GERScreenStateLayout* ScreenState(ulong screen)
{
    GERasterizerLayout* rasterizer = ActiveRasterizer();
    return rasterizer ? static_cast<GERScreenStateLayout**>(rasterizer->screens.first)[screen] : 0;
}

// FSFileSpec's three string getters the movie uses, by vtable slot (the engine's class is not modelled): directory (5), file (7), volume (10).
const UStringLayout* SpecString(const FSFileSpec& spec, int slot)
{
    typedef const UStringLayout*(__attribute__((thiscall)) * Getter)(const FSFileSpec*);
    Getter* const* const table = reinterpret_cast<Getter* const*>(&spec);
    return (*table)[slot](&spec);
}

void Dereference(GESurface* surface)
{
    reinterpret_cast<UReferenceCounter*>(surface)->Dereference();
}

} // namespace

class DX7Movie : public GEMovie {
public:
    DX7Movie(const FSFileSpec& file, ulong screen);
    DX7Movie(const FSFileSpec& file, ulong screen, const GERectangleLayout& rectangle, bool looping);

    UString GetFilename() const override;                 // 1
    bool GetIsLooping() const override { return m_looping; }  // 2
    bool GetIsPlaying() const override { return m_playing; }  // 3
    bool GetIsReady() const override { return m_ready; }      // 4
    bool GetJustStarted() const override { return m_justStarted; }  // 5
    long RenderFrame() override;                          // 6
    void Stop() override;                                 // 7

private:
    ~DX7Movie() override;                                 // 0
    void Initialize(const FSFileSpec& file, ulong screen, bool looping);
    long CreateFrameBuffer(const BINK* bink);
    bool ShouldStop() const { return m_stop || m_finished; }
    static ulong __cdecl PlayBink(void* parameter);
    static void LayoutIsTheOriginals();

    // Offset 0x4 is the engine's (UReferenceCounter); from 0x8 the layout is the original's.
    ulong m_refCount;                 // 0x04
    DX7Movie* m_self;                 // 0x08  mPlayBinkParameter: the thread's argument is the address of these two fields
    unsigned char m_looping;          // 0x0c  (and the byte behind it, which the thread reads as the second field)
    [[maybe_unused]] unsigned char m_pad0d[3];
    TSCriticalSection* m_frameAccess; // 0x10  mFrameAccess
    TSThread* m_thread;               // 0x14  mThread
    TSEvent* m_frameReady;            // 0x18  mFrameReadyEvent: set for every frame, RenderFrame waits for it
    TSEvent* m_stopped;               // 0x1c  mMovieStopped: set by the thread's last act, Stop waits for it
    GESurface* m_frame;               // 0x20  the frame buffer (a surface of the screen)
    GERectangleLayout m_source;       // 0x24  the part of the frame buffer that is video
    GERectangleLayout m_destination;  // 0x34  where it goes on the render target
    alignas(4) char m_name[16];       // 0x44  an engine UString: the file name
    ulong m_screen;                   // 0x54
    volatile unsigned char m_playing;     // 0x58
    volatile unsigned char m_stop;        // 0x59  Stop was called
    volatile unsigned char m_finished;    // 0x5a  the last frame was shown, or the frame buffer could not be made
    volatile unsigned char m_ready;       // 0x5b  a frame was copied
    volatile unsigned char m_justStarted; // 0x5c  the thread has not opened the file yet
    volatile unsigned char m_ownRectangle; // 0x5d  the constructor was given a rectangle (Initialize clears it again)
    [[maybe_unused]] unsigned char m_pad5e[2];
};


namespace {

UStringLayout* NameOf(char* storage)
{
    return reinterpret_cast<UStringLayout*>(storage);
}

} // namespace

// The layout the engine and the original fix, checked at compile time (a member, because the fields are private).
void DX7Movie::LayoutIsTheOriginals()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(sizeof(DX7Movie) == 0x60, "a DX7Movie is 0x60 bytes");
    static_assert(__builtin_offsetof(DX7Movie, m_refCount) == 0x04 && __builtin_offsetof(DX7Movie, m_self) == 0x08 &&
                      __builtin_offsetof(DX7Movie, m_looping) == 0x0c && __builtin_offsetof(DX7Movie, m_frameAccess) == 0x10 &&
                      __builtin_offsetof(DX7Movie, m_frame) == 0x20 && __builtin_offsetof(DX7Movie, m_source) == 0x24 &&
                      __builtin_offsetof(DX7Movie, m_destination) == 0x34 && __builtin_offsetof(DX7Movie, m_name) == 0x44 &&
                      __builtin_offsetof(DX7Movie, m_screen) == 0x54 && __builtin_offsetof(DX7Movie, m_playing) == 0x58 &&
                      __builtin_offsetof(DX7Movie, m_ownRectangle) == 0x5d,
                  "the movie's fields are where the original's are");
#pragma clang diagnostic pop
}

DX7Movie::DX7Movie(const FSFileSpec& file, ulong screen)
{
    m_refCount = 1;
    m_source.right = m_source.bottom = m_source.left = m_source.top = 0.0f;
    m_destination.right = m_destination.bottom = m_destination.left = m_destination.top = 0.0f;
    EEWebUStringConstruct(m_name);
    if (!g_movieSection) {
        TSScheduler::CreateCriticalSection(g_movieSection);
    }
    Initialize(file, screen, false);
}

DX7Movie::DX7Movie(const FSFileSpec& file, ulong screen, const GERectangleLayout& rectangle, bool looping)
{
    m_refCount = 1;
    m_source.right = m_source.bottom = m_source.left = m_source.top = 0.0f;
    m_destination.right = m_destination.bottom = m_destination.left = m_destination.top = 0.0f;
    EEWebUStringConstruct(m_name);
    if (!g_movieSection) {
        TSScheduler::CreateCriticalSection(g_movieSection);
    }
    m_ownRectangle = 1;
    m_destination = rectangle;
    Initialize(file, screen, looping);
}

DX7Movie::~DX7Movie()
{
    TSScheduler::DestroyEvent(m_stopped);
    TSScheduler::DestroyEvent(m_frameReady);
    TSScheduler::DestroyThread(m_thread);
    TSScheduler::DestroyCriticalSection(m_frameAccess);
    reinterpret_cast<UString*>(m_name)->~UString();
}

UString DX7Movie::GetFilename() const
{
    return *reinterpret_cast<const UString*>(m_name);
}

void DX7Movie::Initialize(const FSFileSpec& file, ulong screen, bool looping)
{
    m_frame = 0;
    m_playing = 0;
    m_stop = 0;
    m_finished = 0;
    m_ready = 0;
    m_ownRectangle = 0;
    m_screen = screen;
    m_self = this;
    m_looping = looping;
    m_justStarted = 1;
    m_thread = 0;
    const UStringLayout* last = SpecString(file, 7);
    const UStringLayout* middle = SpecString(file, 5);
    const UStringLayout* first = SpecString(file, 10);
    EEWebUStringFormat(m_name, "%s%s%s", reinterpret_cast<const char*>(first->chars), reinterpret_cast<const char*>(middle->chars), reinterpret_cast<const char*>(last->chars));
    m_refCount++; // the thread's reference
    static const char kSource[] = "C:\\SSSI Development\\Low-Level Engine\\Graphics Engine\\DX7 Display Plug-ins\\Common\\DX7Movie.cpp";
    if (TSScheduler::CreateCriticalSection(m_frameAccess) != 0) {
        ESVerify(kSource, 0xbb, "TSScheduler::CreateCriticalSection(this->mFrameAccess) == keseOK");
    }
    if (TSScheduler::CreateThread(m_thread, &DX7Movie::PlayBink, &m_self, 0, false) != 0) {
        ESVerify(kSource, 0xbc, "TSScheduler::CreateThread(this->mThread, DX7Movie::PlayBink, (void*)&(this->mPlayBinkParameter)) == keseOK");
    }
    if (TSScheduler::CreateEventA(m_frameReady, false) != 0) {
        ESVerify(kSource, 0xbd, "TSScheduler::CreateEvent(this->mFrameReadyEvent, false) == keseOK");
    }
    if (TSScheduler::CreateEventA(m_stopped, false) != 0) {
        ESVerify(kSource, 0xbe, "TSScheduler::CreateEvent(this->mMovieStopped, false) == keseOK");
    }
    m_stopped->Clear();
}

// The frame buffer: an off-screen surface (type 4) the size of the video in the pixel format and depth of the screen's render target.  A
// movie without a rectangle of its own is shown at the video's size.
long DX7Movie::CreateFrameBuffer(const BINK* bink)
{
    GERScreenStateLayout* state = ScreenState(m_screen);
    GEScreen* screen = reinterpret_cast<GEScreen*>(state->screen);
    GESurface* target = reinterpret_cast<GESurface*>(state->secondarySurface);
    GESurfaceDescriptionLayout description;
    __builtin_memset(&description, 0, sizeof description);
    description.type = 4;
    description.pixelFormat = target->GetPixelFormat();
    description.pixelDepth = target->GetPixelDepth();
    description.width = bink->width;
    description.height = bink->height;
    if (!m_ownRectangle) {
        m_destination.top = 0.0f;
        m_destination.left = 0.0f;
        m_destination.bottom = static_cast<float>(bink->height);
        m_destination.right = static_cast<float>(bink->width);
    }
    const long result = screen->CreateSurface(reinterpret_cast<GESurfaceDescription&>(description), &m_frame);
    if (result == 0) {
        m_source.left = 0.0f;
        m_source.top = 0.0f;
        m_source.right = static_cast<float>(static_cast<long>(bink->width));
        m_source.bottom = static_cast<float>(static_cast<long>(bink->height));
    }
    return result;
}

long DX7Movie::RenderFrame()
{
    long result = 0;
    if (!m_justStarted && !m_playing) {
        return 7;
    }
    m_frameReady->Wait(0xffffffffUL);
    EEWebCriticalSectionEnter(m_frameAccess);
    GESurface* target = reinterpret_cast<GESurface*>(ScreenState(m_screen)->secondarySurface);
    if (m_playing && m_frame) {
        reinterpret_cast<GESurfaceLayout*>(m_frame)->refCount++;
        GERectangleLayout destination;
        if (!m_ownRectangle) {
            destination.left = 0.0f;
            destination.top = 0.0f;
            destination.right = static_cast<float>(static_cast<long>(target->GetWidth()));
            destination.bottom = static_cast<float>(static_cast<long>(target->GetHeight()));
        } else {
            destination = m_destination;
        }
        result = m_frame->CopyTo(target, reinterpret_cast<GERectangle&>(m_source), reinterpret_cast<GERectangle&>(destination), false);
        Dereference(m_frame);
    }
    EEWebCriticalSectionLeave(m_frameAccess);
    return result;
}

void DX7Movie::Stop()
{
    m_stop = 1;
    m_stopped->Wait(0xffffffffUL);
}

// The thread.  `parameter` is the address of m_self; the byte behind it is the looping flag.
ulong DX7Movie::PlayBink(void* parameter)
{
    EEWebCriticalSectionEnter(g_movieSection);
    void* const mixer = ActiveMixer();
    void* directSound = 0;
    if (mixer) {
        // The mixer object holds the address of a function (no arguments) at offset 0xa8 that answers its DirectSound object.
        directSound = (*reinterpret_cast<void*(**)()>(static_cast<char*>(mixer) + 0xa8))();
    }
    DX7Movie* const movie = *static_cast<DX7Movie**>(parameter);
    const bool looping = static_cast<unsigned char*>(parameter)[4] != 0;
    movie->m_thread->SetPriority(0);
    UMSetFloatingPointPrecision(0x10000);
    if (mixer && directSound != g_directSound) {
        BinkSetSoundSystem(reinterpret_cast<void*>(&BinkOpenDirectSound), directSound);
        g_directSound = directSound;
    }
    BINK* const bink = BinkOpen(reinterpret_cast<const char*>(NameOf(movie->m_name)->chars), 0);
    if (bink) {
        if (movie->CreateFrameBuffer(bink) != 0) {
            movie->m_finished = 1;
        }
        movie->m_playing = 1;
        movie->m_justStarted = 0;
        while (!movie->ShouldStop()) {
            if (UMGetFloatingPointPrecision() != 0x10000) {
                UMSetFloatingPointPrecision(0x10000);
            }
            if (UMGetFloatingPointRoundingMode() != 0) {
                UMSetFloatingPointRoundingMode(0);
            }
            while (BinkWait(bink) != 0) {
                Sleep(1);
            }
            BinkDoFrame(bink);
            EEWebCriticalSectionEnter(movie->m_frameAccess);
            GESurface* const frame = movie->m_frame;
            if (frame->Lock() == 0) {
                const ulong type = BinkDDSurfaceType(static_cast<DX7Surface*>(frame)->GetDirectDrawSurface());
                const long pitch = static_cast<long>(frame->GetBytesPerRow());
                unsigned char* const memory = frame->GetMemoryAddress();
                BinkCopyToBuffer(bink, memory, pitch, bink->height, 0, 0, type);
                movie->m_ready = 1;
                movie->m_frameReady->Set();
                frame->Unlock();
            }
            EEWebCriticalSectionLeave(movie->m_frameAccess);
            if (!movie->ShouldStop() && !looping) {
                movie->m_finished = bink->frameNumber == bink->frames;
            }
            if (!movie->ShouldStop()) {
                BinkNextFrame(bink);
            }
        }
        EEWebCriticalSectionEnter(movie->m_frameAccess);
        movie->m_playing = 0;
        movie->m_frameReady->Set();
        if (movie->m_frame) {
            Dereference(movie->m_frame);
            movie->m_frame = 0;
        }
        EEWebCriticalSectionLeave(movie->m_frameAccess);
        BinkClose(bink);
        Sleep(100);
    }
    movie->m_justStarted = 0;
    reinterpret_cast<UReferenceCounter*>(movie)->Dereference();
    EEWebCriticalSectionLeave(g_movieSection);
    movie->m_stopped->Set();
    return 0;
}

// ---- the rasterizer's slots --------------------------------------------------------------------------------------------------------

GEMovie* EEWebNullRasterizer::CreateMovie(FSFileSpec const& file, ulong screen)
{
    return new DX7Movie(file, screen);
}

GEMovie* EEWebNullRasterizer::CreateMovie(FSFileSpec const& file, ulong screen, GERectangle const& rectangle, bool looping)
{
    return new DX7Movie(file, screen, reinterpret_cast<const GERectangleLayout&>(rectangle), looping);
}
