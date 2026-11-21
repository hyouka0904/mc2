#include "gameos.hpp"
#include "toolos.hpp"
#include "memorymanager.hpp" // gos_Heap
#include <stdio.h>
#include <time.h>
#include <stdlib.h> // rand
#include "utils/timing.h"
#include "utils/kv_registry.h"
#include "platform_clock.h"
#include <string>

#ifndef PLATFORM_WINDOWS
#include <sys/statvfs.h> // statvfs
#else
#include <Shlobj.h> // SHGetFolderPath
#include <Objbase.h> // CoTaskMemFree
#endif


#include <stdlib.h> // getenv

// fstat
#include <sys/types.h>
#include <sys/stat.h>
#ifndef PLATFORM_WINDOWS
#include <unistd.h>
#else
#include<direct.h>
#define S_ISDIR(x) ((_S_IFDIR & (x))!=0)
#endif

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
HGOSHEAP __stdcall gos_CreateMemoryHeap(char const* HeapName, DWORD MaximumSize/* = 0*/, HGOSHEAP parentHeap/* = ParentClientHeap*/)
{
    gos_Heap* pheap = new gos_Heap();
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

    return pheap;
}
void __stdcall gos_DestroyMemoryHeap(HGOSHEAP Heap, bool shouldBeEmpty/* = true*/)
{
    // shouldBeEmpty ?? should Instances be zero or what?
    delete Heap;
    Heap = nullptr;
}
void __stdcall gos_PushCurrentHeap(HGOSHEAP Heap)
{
    gosASSERT(g_heapStackPointer < g_hepsStackSize);
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

void* __cdecl operator new[](size_t size, HGOSHEAP Heap)
{
    return malloc(size);
}

void* __stdcall gos_Malloc(size_t bytes, HGOSHEAP Heap/* = 0*/)
{
    // FIXME: TODO: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // FIXME: TODO: each malloc should record memory allocation for current heap on a stack
    // FIXME: TODO: so that when Heap destroy() is called all memory will be freed

    /*
    gosHeap* heap = NULL;

    if(Heap) {
        heap = (gosHeap*)Heap;
    } else { 
        heap = gos_GetCurrentHeap();
    }

    if(heap)
        heap->BytesAllocated += bytes;

    if(bytes > 1024*1024)
    {
        PAUSE((""));
    }
    */
    return malloc(bytes);
}
void __stdcall gos_Free(void* ptr)
{
    /*
    gosHeap* heap = (gosHeap*)gos_GetCurrentHeap();
    if(Heap) {
        gosHeap* heap = (gosHeap*)Heap;
        heap->BytesAllocated -= // ??? ;
    }
    */
    free(ptr);
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
#ifndef PLATFORM_WINDOWS
    struct statvfs s;
    if(-1 == statvfs(Path, &s)) {
        SPEW(("statvfs: %s\n", strerror(errno)));
        return 0;
    }

    return s.f_bsize * s.f_bfree;
#else
	ULARGE_INTEGER freeBytesAvailable;
	GetDiskFreeSpaceEx(NULL, &freeBytesAvailable, NULL, NULL);
	return freeBytesAvailable.QuadPart;
#endif
}

DWORD __stdcall gos_EnableSetting( gosSetting Setting, DWORD Value )
{
    // stub now implement later
    return 0;
}
////////////////////////////////////////////////////////////////////////////////
// registry read/write
static const char* g_user_reg_file = ".registry";
static const char* g_user_config_dir = ".mechcommander2";

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

bool __stdcall gos_GetUserDataDirectory(char* user_dir, const int len)
{
	const int cfg_dir_size = 1024;
    char cfg_dir[cfg_dir_size] = {0};
    struct stat st;

#ifdef PLATFORM_WINDOWS
	PWSTR homeDir = 0;
	SHGetKnownFolderPath(FOLDERID_Profile, 0, NULL, &homeDir);
	wcstombs(cfg_dir, homeDir, cfg_dir_size-1);
	cfg_dir[cfg_dir_size-1] = '\0';
	CoTaskMemFree(homeDir);
#else
    const char* home_dir = getenv("HOME");
    if(home_dir == NULL)
    {
        SPEW(("gos_GetUserDataDirectory", "home directory is not set\n"));
        return false;
    }

    const char* config_dir = getenv("XDG_CONFIG_HOME");
    if(config_dir != NULL) {
        strncpy(cfg_dir, config_dir, cfg_dir_size-1);
        cfg_dir[cfg_dir_size-1] = '\0';

        if(-1 == stat(cfg_dir, &st)) {
            SPEW(("stat: %s\n", strerror(errno)));
            return false;
        }

        if(!S_ISDIR(st.st_mode)) {
            SPEW(("SAVELOAD", "gos_GetUserDataDirectory: %s is not directory\n", cfg_dir));
            return false;
        }
    } else {
        strncpy(cfg_dir, home_dir, cfg_dir_size-1);
        cfg_dir[cfg_dir_size-1] = '\0';
    }
#endif

    // 1 + 1 =  for \0 and /
    char* mc2_conf_dir = new char[strlen(cfg_dir) + strlen(g_user_config_dir) + 1 + 1];
    sprintf(mc2_conf_dir, "%s/%s", cfg_dir, g_user_config_dir);

    // if directory does not exist create it
#ifndef PLATFORM_WINDOWS
	int permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH | S_IXUSR | S_IXGRP;      
#endif

    if(-1 == stat(mc2_conf_dir, &st)) {
#ifdef PLATFORM_WINDOWS
        if(-1 == _mkdir(mc2_conf_dir)) {
#else
        if(-1 == mkdir(mc2_conf_dir, permissions)) {
#endif
            SPEW(("stat: %s\n", strerror(errno)));
            delete[] mc2_conf_dir;
            return false;
        }
    }
    
    size_t conf_len = strlen(mc2_conf_dir);
    conf_len = len - 1 < conf_len ? len - 1 : conf_len;

    strncpy(user_dir, mc2_conf_dir, conf_len);
    user_dir[conf_len] = '\0';
    delete[] mc2_conf_dir;
    return true;
}


////////////////////////////////////////////////////////////////////////////////

bool volatile mc2IsInDisplayBackBuffer = false;
bool volatile mc2IsInMouseTimer = false;

////////////////////////////////////////////////////////////////////////////////

HGOSHEAP ParentClientHeap = NULL;
float ProcessorSpeed = 10595.42f; // sebi: put something cool
float OneOverProcessorSpeed = 1.0f / ProcessorSpeed; // sebi: put something cool
