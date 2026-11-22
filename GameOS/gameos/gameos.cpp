#include "gameos.hpp"
#include "toolos.hpp"
#include "memorymanager.hpp" // gos_Heap
#include <stdio.h>
#include <time.h>
#include <stdlib.h> // rand
#include "utils/timing.h"
#include "utils/kv_registry.h"
#include "utils/user_dir.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <system_error>
#include "platform_clock.h"
#include <string>
#include <mutex>
#include <new>
#include <vector>

#include <stdlib.h> // getenv
#include <string.h>

#include "strres.h"

////////////////////////////////////////////////////////////////////////////////
void __stdcall AddDebuggerMenuItem(char const*, bool (__stdcall *)(), void (__stdcall *)(), bool (__stdcall *)(), DWORD (__stdcall *)(char const*, DWORD))
{
    // The Windows debugger menu has no equivalent here; intentionally a no-op.
}

void __stdcall AddStatistic( const char* Name, const char* TypeName, gosType Type, void* Value, DWORD Flags )
{
    printf("STATISTICS: [%s : %s]\n", Name, TypeName);
}
void __stdcall StatisticFormat(char const* s)
{
    printf("STATISTICS: [%s]\n", s);
}
////////////////////////////////////////////////////////////////////////////////

void* DecodeJPG( const char* FileName, BYTE* Data, DWORD DataSize, DWORD* TextureWidth, DWORD* TextureHeight, bool TextureLoad, void *pDestSurf )
{
    return NULL;
}

gosEnvironment Environment;

////////////////////////////////////////////////////////////////////////////////
void EnterFullScreenMode(void)
{
    Environment.fullScreen = true;
}

void EnterWindowMode(void)
{
    Environment.fullScreen = false;
}

static bool g_gos_exit_game_os = false;

void __stdcall ExitGameOS()
{
    g_gos_exit_game_os = true;
}

bool gosExitGameOS() {
    return g_gos_exit_game_os;
}

float frameRate = 30.0f; // apparently tiny geometry needs this

__int64 __stdcall GetCycles()
{
#ifdef PLATFORM_WINDOWS
	return rdtsc();
#else
    return (__int64)platform_clock::now_ns();
#endif
}

static double g_prev_elapsed_time_value = -1;
double __stdcall gos_GetElapsedTime( int RealTime )
{
    (void)RealTime;

    const double time_sec = platform_clock::now_ns() * 1e-9;
    if(g_prev_elapsed_time_value < 0.0) {
        g_prev_elapsed_time_value = time_sec;
        return time_sec;
    }

    // Long stalls (debugger, loading) are reported as at most MaxTimeDelta
    if(Environment.MaxTimeDelta > 0 && time_sec - g_prev_elapsed_time_value > Environment.MaxTimeDelta) {
        g_prev_elapsed_time_value += Environment.MaxTimeDelta;
        return g_prev_elapsed_time_value;
    }

    g_prev_elapsed_time_value = time_sec;
    return time_sec;
}

double __stdcall gos_GetHiResTime()
{
    return platform_clock::now_ns() * 1e-9;
}

////////////////////////////////////////////////////////////////////////////////

static const int g_hepsStackSize = 128;
static gos_Heap* g_heapsStack[g_hepsStackSize];
static int g_heapStackPointer = -1;

////////////////////////////////////////////////////////////////////////////////
// Every gos_Malloc / operator new block starts with this header. Blocks
// made while a heap is current (or for an explicit heap) are linked into
// that heap, which can then report them or release them when destroyed.
struct gosAllocHeader {
    gos_Heap* heap;
    gosAllocHeader* prev;
    gosAllocHeader* next;
    size_t size;
};
static_assert(sizeof(gosAllocHeader) % 16 == 0, "keeps malloc's 16-byte alignment");
static std::mutex g_heap_mutex; // constant initialised: usable before main

static void heapLink(gos_Heap* heap, gosAllocHeader* h) {
    h->heap = heap;
    h->prev = NULL;
    h->next = (gosAllocHeader*)heap->pFirstAllocation;
    if(h->next)
        h->next->prev = h;
    heap->pFirstAllocation = h;
    heap->TrackedBytes += h->size;
    heap->TrackedAllocations++;
}

static void heapUnlink(gosAllocHeader* h) {
    gos_Heap* heap = h->heap;
    if(h->prev)
        h->prev->next = h->next;
    else
        heap->pFirstAllocation = h->next;
    if(h->next)
        h->next->prev = h->prev;
    heap->TrackedBytes -= h->size;
    heap->TrackedAllocations--;
    h->heap = NULL;
}

HGOSHEAP __stdcall gos_CreateMemoryHeap(char const* HeapName, DWORD MaximumSize/* = 0*/, HGOSHEAP parentHeap/* = ParentClientHeap*/)
{
    // plain malloc: the heap itself belongs to no heap
    gos_Heap* pheap = (gos_Heap*)malloc(sizeof(gos_Heap));
    memset(pheap, 0, sizeof(gos_Heap));
    pheap->pParent = parentHeap;
    pheap->Magic = (DWORD)(reinterpret_cast<size_t>(HeapName) & 0xffffffff);
    if(HeapName) {
        strncpy(pheap->Name, HeapName, sizeof(pheap->Name)-1);
        pheap->Name[sizeof(pheap->Name)-1] = '\0';
    }

#ifdef LAB_ONLY
    pheap->BytesAllocated = 0;
    pheap->MaximumSize = MaximumSize;
#endif
    (void)MaximumSize;

    return pheap;
}

// shouldBeEmpty = false: the caller expects the heap's remaining blocks to
// be released with it. true: they are reported as leaks and stay valid
// (they may still be in use; gos_Free works on them later).
void __stdcall gos_DestroyMemoryHeap(HGOSHEAP Heap, bool shouldBeEmpty/* = true*/)
{
    if(!Heap)
        return;
    std::vector<gosAllocHeader*> release;
    {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        if(Heap->TrackedAllocations && shouldBeEmpty)
            SPEW(("MEMORY", "heap '%s' destroyed with %u blocks (%zu bytes) still allocated\n",
                  Heap->Name, (unsigned)Heap->TrackedAllocations, Heap->TrackedBytes));
        while(Heap->pFirstAllocation) {
            gosAllocHeader* h = (gosAllocHeader*)Heap->pFirstAllocation;
            heapUnlink(h);
            if(!shouldBeEmpty)
                release.push_back(h);
        }
    }
    for(gosAllocHeader* h : release)
        free(h);
    for(int i = 0; i <= g_heapStackPointer; ++i)
        if(g_heapsStack[i] == Heap)
            g_heapsStack[i] = NULL;
    free(Heap);
}

void __stdcall gos_GetHeapUsage(HGOSHEAP Heap, size_t* bytes, DWORD* allocations)
{
    std::lock_guard<std::mutex> lock(g_heap_mutex);
    if(bytes)
        *bytes = Heap ? Heap->TrackedBytes : 0;
    if(allocations)
        *allocations = Heap ? Heap->TrackedAllocations : 0;
}

void __stdcall gos_PushCurrentHeap(HGOSHEAP Heap)
{
    gosASSERT(g_heapStackPointer < g_hepsStackSize - 1);
    g_heapsStack[++g_heapStackPointer] = Heap;
}
void __stdcall gos_PopCurrentHeap()
{
    gosASSERT(g_heapStackPointer >= 0 && g_heapStackPointer < g_hepsStackSize);
    g_heapsStack[g_heapStackPointer--] = nullptr;
}

HGOSHEAP __stdcall gos_GetCurrentHeap()
{
    if(g_heapStackPointer == -1)
        return NULL;
    return g_heapsStack[g_heapStackPointer];
}

void __stdcall gos_WalkMemoryHeap(HGOSHEAP pHeap, bool vociferous/* = false*/)
{
    if(!pHeap)
        return;
    size_t bytes = 0;
    DWORD count = 0;
    {
        // checks the list links while counting
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        for(gosAllocHeader* h = (gosAllocHeader*)pHeap->pFirstAllocation; h; h = h->next) {
            gosASSERT(h->heap == pHeap && (!h->next || h->next->prev == h));
            bytes += h->size;
            ++count;
        }
        gosASSERT(bytes == pHeap->TrackedBytes && count == pHeap->TrackedAllocations);
    }
    if(vociferous)
        SPEW(("MEMORY", "heap '%s': %u blocks, %zu bytes\n", pHeap->Name, (unsigned)count, bytes));
}

////////////////////////////////////////////////////////////////////////////////
void* operator new(size_t sz) {
    return gos_Malloc(sz, NULL);
}
void operator delete(void* ptr)
#ifndef PLATFORM_WINDOWS
noexcept 
#endif
{
    gos_Free(ptr);
}

void* operator new[](size_t sz) {
    return gos_Malloc(sz, NULL);
}
void operator delete[](void* ptr)
#ifndef PLATFORM_WINDOWS
noexcept
#endif
{
    gos_Free(ptr);
}

// Sized deallocation (C++14) and the nothrow forms. The standard library's
// versions forward to the ones above, but a replacement allocator such as
// AddressSanitizer's does not: its nothrow new skipped gos_Malloc and its
// sized delete freed gos_Malloc blocks with its own allocator.
void operator delete(void* ptr, size_t) noexcept
{
    gos_Free(ptr);
}
void operator delete[](void* ptr, size_t) noexcept
{
    gos_Free(ptr);
}
void* operator new(size_t sz, const std::nothrow_t&) noexcept
{
    return gos_Malloc(sz, NULL);
}
void* operator new[](size_t sz, const std::nothrow_t&) noexcept
{
    return gos_Malloc(sz, NULL);
}
void operator delete(void* ptr, const std::nothrow_t&) noexcept
{
    gos_Free(ptr);
}
void operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
    gos_Free(ptr);
}

void* __cdecl operator new[](size_t size, HGOSHEAP Heap)
{
    return gos_Malloc(size, Heap); // freed by delete[] -> gos_Free
}

void* __stdcall gos_Malloc(size_t bytes, HGOSHEAP Heap/* = 0*/)
{
    gosAllocHeader* h = (gosAllocHeader*)malloc(sizeof(gosAllocHeader) + bytes);
    if(!h)
        return NULL;
    h->size = bytes;
    h->heap = NULL;
    h->prev = h->next = NULL;
    gos_Heap* heap = Heap ? Heap : gos_GetCurrentHeap();
    if(heap) {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        heapLink(heap, h);
    }
    return h + 1;
}

void __stdcall gos_Free(void* ptr)
{
    if(!ptr)
        return;
    gosAllocHeader* h = (gosAllocHeader*)ptr - 1;
    if(h->heap) {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        if(h->heap) // the heap may have been destroyed meanwhile
            heapUnlink(h);
    }
    free(h);
}
////////////////////////////////////////////////////////////////////////////////

static bool g_fixed_seed_enabled = false;
static unsigned int g_fixed_seed = 0;

void gos_SetFixedRandomSeed(unsigned int seed)
{
    g_fixed_seed_enabled = true;
    g_fixed_seed = seed;
    srand(seed);
}

unsigned int __stdcall gos_GetRandomSeed()
{
    // Callers used to seed with time(NULL); with -seed every run is the same
    return g_fixed_seed_enabled ? g_fixed_seed : (unsigned int)time(NULL);
}

void __stdcall gos_srand(unsigned int seed)
{
    // The game seeds with timeGetTime(). With a fixed seed that value comes
    // from the virtual clock, so mixing it in keeps runs reproducible while
    // reseeds at different moments still produce different sequences.
    if (g_fixed_seed_enabled)
        seed ^= g_fixed_seed;
    srand(seed);
}

int __stdcall gos_rand()
{
    return rand() % (1<<15);
}
////////////////////////////////////////////////////////////////////////////////
void __stdcall gos_EnableIME(bool enabledDisabled)
{
}
void _stdcall gos_ToggleIME(bool activeInactive)
{
}
void _stdcall gos_GetIMEAppearance(gosIME_Appearance* pia)
{
}
void _stdcall gos_SetIMEAppearance(const gosIME_Appearance* pia)
{
}
void _stdcall gos_PositionIME(DWORD x, DWORD y)
{
}
void _stdcall gos_SetIMELevel(DWORD dwImeLevel)
{
}
void _stdcall gos_FinalizeStringIME()
{
}

////////////////////////////////////////////////////////////////////////////////
float __stdcall gosJoystick_GetAxis( DWORD index, GOSJoystickAxis axis )
{
    (void)index; (void)axis;
    gosASSERT(0 && "Not implemented");
    return 0.0f;
}

DWORD __stdcall gosJoystick_CountJoysticks( bool ReDetect )
{
    (void)ReDetect;
    //gosASSERT(0 && "Not implemented");
    return 0;
}

void __stdcall gosJoystick_SetPolling( DWORD index, bool yesNo, float howOften/*=0.03333f*/ )
{
    (void)index, (void)yesNo, (void)howOften;
    gosASSERT(0 && "Not implemented");
}

void __stdcall gosJoystick_GetInfo( DWORD index, gosJoystick_Info* gji )
{
    gosASSERT(gji);
	gji->lpstrName = NULL;
	gji->bAxisValid = 0;
	gji->nAxes = 0;			
	gji->nButtons = 0;	
	gji->nSliders = 0;
	gji->nPOVs = 0;				
	gji->bIsPolled = 0;		
	gji->bIsForceFeedback = 0;
}

////////////////////////////////////////////////////////////////////////////////
char* __stdcall gos_GetFormattedDate( bool Verbose, WORD Year/*=-1*/, WORD Month/*=-1*/, WORD Day/*=-1*/ )
{
    time_t tim = time(NULL);
    struct tm* gm_time_data = gmtime(&tim);
    if(Year < 0) {
        Year = 1900 + gm_time_data -> tm_year;
    }
    if(Month < 0) {
        Month = 1 + gm_time_data -> tm_mon;
    }
    if(Day < 0) {
        Day = gm_time_data -> tm_mday;
    }

    if(Year > 9999) {
        Year = 0;
    }

    if(Month > 99) {
        Month = 0;
    }

    if(Day > 99) {
        Day = 0;
    }

    if(!Verbose)
    {
        char* date = new char[16];
        sprintf(date, "%02d/%02d/%04d", Day, Month, Year);
        return date;
    }

    gm_time_data->tm_year = Year;
    gm_time_data->tm_mon = Month;
    gm_time_data->tm_mday = Day;

    time_t tt = mktime(gm_time_data);
    char* str_time = ctime(&tt);
    
    char* date = new char[strlen(str_time)+1];
    strcpy(date, str_time);
    return date;

}

////////////////////////////////////////////////////////////////////////////////
char* __stdcall gos_GetFormattedTime( WORD Hour/*=-1*/, WORD Minute/*=-1*/, WORD Second/*=-1*/ )
{
    time_t tim = time(NULL);
    struct tm* gm_time_data = gmtime(&tim);
    if(Hour < 0) {
        Hour = gm_time_data -> tm_hour;
    }
    if(Minute < 0) {
        Minute = gm_time_data -> tm_min;
    }
    if(Second < 0) {
        Second = gm_time_data -> tm_sec;
    }

    if(Hour > 99) {
       Hour = 0;
    }

    if(Minute > 99) {
        Minute = 0;
    }

    if(Second > 99) {
        Second = 0;
    }

    char* date = new char[16];
    sprintf(date, "%02d:%02d:%02d", Hour, Minute, Second);
    return date;
}

////////////////////////////////////////////////////////////////////////////////

void __stdcall gos_TerminateApplication()
{
    g_gos_exit_game_os = true;
}

void __stdcall gos_AbortTermination()
{
    g_gos_exit_game_os = false;
}
////////////////////////////////////////////////////////////////////////////////
__int64 __stdcall gos_GetDriveFreeSpace( char* Path )
{
    std::error_code ec;
    const std::filesystem::space_info info = std::filesystem::space(Path && *Path ? Path : ".", ec);
    if (ec) {
        SPEW(("gos_GetDriveFreeSpace", "%s: %s\n", Path ? Path : "", ec.message().c_str()));
        return 0;
    }
    return (__int64)info.available;
}

DWORD __stdcall gos_EnableSetting( gosSetting Setting, DWORD Value )
{
    // stub now implement later
    return 0;
}
////////////////////////////////////////////////////////////////////////////////
// registry read/write
static const char* g_user_reg_file = ".registry";

// The registry is emulated by a small key/value file in the user data
// directory (see utils/kv_registry.h for the format).
static bool get_registry_path(std::string& path)
{
    char user_dir[1024];
    if(!gos_GetUserDataDirectory(user_dir, sizeof(user_dir)))
        return false;
    path = std::string(user_dir) + "/" + g_user_reg_file;
    return true;
}

void __stdcall gos_LoadDataFromRegistry( const char* keyName, void* pData, DWORD* szData, bool HKLM/*=false*/)
{
    gosASSERT(keyName && pData && szData);
    std::string path;
    if(!get_registry_path(path)) {
        *szData = 0;
        return;
    }
    uint32_t size = *szData;
    kv_registry::load(kv_registry::readFile(path), keyName, pData, &size);
    *szData = size;
}

void __stdcall gos_SaveDataToRegistry( const char* keyName,  void* pData,  DWORD szData )
{
    gosASSERT(keyName && (pData || szData == 0));
    std::string path;
    if(!kv_registry::isValidKey(keyName) || !get_registry_path(path)) {
        SPEW(("REG", "gos_SaveDataToRegistry: cannot store key %s\n", keyName));
        return;
    }
    kv_registry::Store store = kv_registry::readFile(path);
    const uint8_t* bytes = (const uint8_t*)pData;
    store[keyName] = kv_registry::Value(bytes, bytes + szData);
    if(!kv_registry::writeFile(path, store))
        SPEW(("REG", "gos_SaveDataToRegistry: failed to write %s\n", path.c_str()));
}

void __stdcall gos_SaveStringToRegistry( const char* keyName,  char* pData,  DWORD szData )
{
    gos_SaveDataToRegistry(keyName, pData, szData);
}

// Saved games and settings: SDL_GetPrefPath, or $MC2_USER_DIR if set (tests
// use it to start from an empty directory). See utils/user_dir.h.
bool __stdcall gos_GetUserDataDirectory(char* user_dir, const int len)
{
    static std::mutex m;
    static std::string cached;
    std::lock_guard<std::mutex> lock(m);
    if (cached.empty()) {
        std::string dir;
        std::vector<std::string> legacy;
        if (const char* env = SDL_getenv("MC2_USER_DIR")) {
            dir = env;
        } else {
            char* pref = SDL_GetPrefPath("", "MechCommander2");
            if (!pref) {
                SPEW(("gos_GetUserDataDirectory", "SDL_GetPrefPath failed: %s\n", SDL_GetError()));
                return false;
            }
            dir = pref;
            SDL_free(pref);
            legacy = user_dir::legacy_dirs_from_env();
        }
        while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\'))
            dir.pop_back();

        std::string report;
        if (!user_dir::prepare(dir, legacy, &report)) {
            SPEW(("gos_GetUserDataDirectory", "cannot create %s\n", dir.c_str()));
            return false;
        }
        if (!report.empty())
            fprintf(stderr, "USERDIR: %s", report.c_str());
        cached = dir;
    }
    if (len <= 0 || cached.size() >= (size_t)len)
        return false;
    memcpy(user_dir, cached.c_str(), cached.size() + 1);
    return true;
}


////////////////////////////////////////////////////////////////////////////////

bool volatile mc2IsInDisplayBackBuffer = false;
bool volatile mc2IsInMouseTimer = false;

////////////////////////////////////////////////////////////////////////////////

HGOSHEAP ParentClientHeap = NULL;
float ProcessorSpeed = 10595.42f; // sebi: put something cool
float OneOverProcessorSpeed = 1.0f / ProcessorSpeed; // sebi: put something cool
