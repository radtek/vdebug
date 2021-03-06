#include <Windows.h>
#include <Psapi.h>
#include <Winternl.h>
#include <shobjidl.h>
#include <AccCtrl.h>
#include <AclAPI.h>
#include <WtsApi32.h>
#include <UserEnv.h>
#include <string>
#include <ComLib/ComLib.h>
#include <ComLib/ComLib.h>
#include "ComUtil.h"
#include "StrSafe.h"

#pragma comment(lib, "Psapi.lib")
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "WtsApi32.lib")

using namespace std;
using namespace Json;

PFILE_MAPPING_STRUCT __stdcall MappingFileA(LPCSTR fileName, BOOL bWrite, DWORD maxViewSize)
{
    PFILE_MAPPING_STRUCT pfms = NULL;

    do
    {
        if (!fileName)
        {
            break;
        }

        pfms = (PFILE_MAPPING_STRUCT)malloc(sizeof(FILE_MAPPING_STRUCT));
        if (!pfms)
        {
            break;
        }
        RtlZeroMemory(pfms, sizeof(FILE_MAPPING_STRUCT));

        pfms->hFile = CreateFileA(
            fileName,
            GENERIC_READ | (bWrite ? GENERIC_WRITE : 0),
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
            );
        if (INVALID_HANDLE_VALUE == pfms->hFile)
        {
            break;
        }

        if (!GetFileSizeEx(pfms->hFile, &(pfms->fileSize)) || !pfms->fileSize.QuadPart)
        {
            break;
        }

        if (!maxViewSize)
        {
            pfms->mappedSize = pfms->fileSize.QuadPart > 0xffffffff ? 0xffffffff : pfms->fileSize.LowPart;
        }
        else
        {
            pfms->mappedSize = pfms->fileSize.QuadPart > maxViewSize ? maxViewSize : pfms->fileSize.LowPart;
        }

        pfms->hMap = CreateFileMapping(pfms->hFile, NULL, bWrite ? PAGE_READWRITE : PAGE_READONLY, 0, 0, NULL);
        if (!pfms->hMap)
        {
            break;
        }

        pfms->lpView = MapViewOfFile(pfms->hMap, FILE_MAP_READ | (bWrite ? FILE_MAP_WRITE : 0), 0, 0, pfms->mappedSize);
        if (!pfms->lpView)
        {
            break;
        }
    } while (FALSE);

    return pfms;
}

void __stdcall CloseFileMapping(PFILE_MAPPING_STRUCT pfms)
{
    if (pfms)
    {
        if (pfms->lpView)
        {
            UnmapViewOfFile(pfms->lpView);
        }

        if (pfms->hMap)
        {
            CloseHandle(pfms->hMap);
        }

        if (INVALID_HANDLE_VALUE != pfms->hFile)
        {
            CloseHandle(pfms->hFile);
        }

        free((void*)pfms);
    }
}

VOID __stdcall CentreWindow(HWND hSrcWnd, HWND hDstWnd)
{
    if (!hDstWnd)
    {
        hDstWnd = GetDesktopWindow();
    }

    RECT rt = {0};
    GetWindowRect(hDstWnd, &rt);
    RECT crt = {0};
    GetWindowRect(hSrcWnd, &crt);
    int iX = 0;
    int iY = 0;
    int icW = crt.right - crt.left;
    int iW = rt.right - rt.left;
    int icH = crt.bottom - crt.top;
    int iH = rt.bottom - rt.top;
    iX = rt.left + (iW - icW) / 2;
    iY = rt.top + (iH - icH) / 2;
    MoveWindow(hSrcWnd, iX, iY, icW, icH, TRUE);
}

DWORD __stdcall GetColourFromStr(LPCSTR szColour)
{
    if (!szColour || !szColour[0])
    {
        return 0;
    }

    mstring str(szColour);
    if (str == "null")
    {
        return 0xffffffff;
    }
    str.delchar(' ');
    str += ",";
    int vCol[3] = {0};
    int iIdex = 0;
    size_t iLast = 0;
    size_t iCur = 0;
    while (mstring::npos != (iCur = str.find(",", iLast)))
    {
        mstring strSub = str.substr(iLast, iCur - iLast);
        vCol[iIdex++] = atoi(strSub.c_str());
        iLast = (iCur + 1);

        if (3 == iIdex)
        {
            break;
        }
    }

    return RGB(vCol[0], vCol[1], vCol[2]);
}

VOID __stdcall PrintDbgInternal(LPCWSTR wszTarget, LPCSTR szFile, DWORD dwLine, LPCWSTR wszFormat, ...)
{
    WCHAR wszFormat1[1024] = {0};
    WCHAR wszFormat2[1024] = {0};
    lstrcpyW(wszFormat1, L"[%ls][%hs.%d]%ls");
    StrCatW(wszFormat1, L"\n");
    wnsprintfW(wszFormat2, RTL_NUMBER_OF(wszFormat2), wszFormat1, wszTarget, szFile, dwLine, wszFormat);

    WCHAR wszLogInfo[1024];
    va_list vList;
    va_start(vList, wszFormat);
    wvnsprintfW(wszLogInfo, sizeof(wszLogInfo), wszFormat2, vList);
    va_end(vList);
    OutputDebugStringW(wszLogInfo);
}

void __stdcall IterateProcW(pfnProcHandlerW handler, void* lpParam)
{
    do
    {
        if (!handler)
        {
            break;
        }

        PROCESSENTRY32W pe32 = {0};
        pe32.dwSize = sizeof(pe32);

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (INVALID_HANDLE_VALUE == hSnap)
        {
            break;
        }

        BOOL bMore = Process32FirstW(hSnap, &pe32);
        while (bMore)
        {
            if (!handler(&pe32, lpParam))
            {
                // 回调方要求退出
                break;
            }

            bMore = Process32NextW(hSnap, &pe32);
        }

        CloseHandle(hSnap);
    } while (FALSE);
}

void ErrMessage(const wchar_t *format, ...)
{
    wchar_t szText[2048] = {0};
    va_list val;

    va_start(val, format);
    wvnsprintfW(szText, RTL_NUMBER_OF(szText), format, val);
    va_end(val);
    MessageBoxW(0, szText, L"Error", MB_TOPMOST);
}

static BOOL WINAPI _PeIsPeFile(LPVOID buffer, BOOL* b64)
{
    BOOL bRet = FALSE;

    do
    {
        if (!buffer)
        {
            break;
        }

        IMAGE_DOS_HEADER* pDosHdr = (IMAGE_DOS_HEADER*)buffer;
        if (IMAGE_DOS_SIGNATURE != pDosHdr->e_magic)
        {
            break;
        }
        if (pDosHdr->e_lfanew > 1024)
        {
            break;
        }

        IMAGE_NT_HEADERS32* pNtHdr = (IMAGE_NT_HEADERS32*)((byte*)pDosHdr + pDosHdr->e_lfanew);
        if (IsBadReadPtr(pNtHdr, sizeof(void*)))
        {
            break;
        }

        if (IMAGE_NT_SIGNATURE != pNtHdr->Signature)
        {
            break;
        }

        if (b64)
        {
            *b64 = (pNtHdr->FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER64));
        }

        bRet = TRUE;
    } while (FALSE);

    return bRet;
}

#define _PE_MAX_MAP_SIZE    (1024 * 1024 * 4)

static BOOL WINAPI _PeCheckPeMapping(PFILE_MAPPING_STRUCT pfms, BOOL* b64)
{
    if (!pfms || !pfms->lpView || !pfms->fileSize.QuadPart)
    {
        return FALSE;
    }

    if (pfms->fileSize.QuadPart < (sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS32)))
    {
        return FALSE;
    }

    if (!_PeIsPeFile(pfms->lpView, b64))
    {
        return FALSE;
    }

    return TRUE;
}

static PFILE_MAPPING_STRUCT WINAPI _PeMappingFile(LPCSTR fileName, BOOL bWrite, BOOL* b64)
{
    PFILE_MAPPING_STRUCT pfms = MappingFileA(fileName, bWrite, _PE_MAX_MAP_SIZE);
    if (!_PeCheckPeMapping(pfms, b64))
    {
        CloseFileMapping(pfms);
        return NULL;
    }

    return pfms;
}

BOOL __stdcall IsPeFileA(LPCSTR fileName, BOOL* b64)
{
    PFILE_MAPPING_STRUCT pfms = NULL;
    if (!fileName)
    {
        return FALSE;
    }

    BOOL bRet = FALSE;

    do
    {
        pfms = _PeMappingFile(fileName, FALSE, b64);
        if (!pfms)
        {
            break;
        }

        bRet = TRUE;
    } while (FALSE);

    CloseFileMapping(pfms);

    return bRet;
}

//获取pe文件属性
mstring __stdcall GetPeDescStrA(const mstring &path, const mstring &attr)
{
    struct LanguagePage
    {
        WORD m_language;
        WORD m_page;
    };

    if (path.empty() || attr.empty())
    {
        return "";
    }

    char buffer[2096];
    buffer[0] = 0;
    char *ptr = buffer;
    DWORD size = 0;
    DWORD ret = 0;
    //获取版本信息大小
    size = GetFileVersionInfoSizeA(path.c_str(), NULL);
    if (size == 0) 
    { 
        return "";
    }

    if (size > sizeof(buffer))
    {
        ptr = new char[size + 1];
    }
    ptr[0] = 0;

    //获取版本信息
    ret = GetFileVersionInfoA(path.c_str(), NULL, size, ptr);

    LPVOID pInfo = NULL;
    BOOL res = FALSE;
    mstring result;
    UINT len = 0;
    do 
    {
        if(ret == 0) 
        { 
            break;
        }

        len = 0;
        if (0 == VerQueryValueA(ptr, "\\VarFileInfo\\Translation", &pInfo, &len))
        {
            break;
        }

        UINT it = 0;
        for (it = 0 ; it < (len / sizeof(LanguagePage)) ; it++)
        {
            LanguagePage *uu = ((LanguagePage *)(pInfo) + it);
            const char *data = NULL;
            UINT count = 0;
            mstring vv;
            vv.format("\\StringFileInfo\\%04x%04x\\%hs", uu->m_language, uu->m_page, attr.c_str());
            if (0 != VerQueryValueA(ptr, vv.c_str(), (LPVOID *)(&data), &count))
            {
                if (count > 0)
                {
                    result = data;
                    res = TRUE;
                    break;
                }
            } else {
                int err = GetLastError();
                int ee = 123;
            }
        }
    } while (FALSE);

    if (ptr && ptr != buffer)
    {
        delete []ptr;
    }
    return result;
}

BOOL __stdcall GetPeVersionW(LPCWSTR lpszFileName, LPWSTR outBuf, UINT size)
{
    WCHAR* szVersionBuffer = NULL;
    BOOL bRet = FALSE;

    do
    {
        if (!lpszFileName || !outBuf)
        {
            break;
        }

        DWORD dwVerSize;
        DWORD dwHandle;

        dwVerSize = GetFileVersionInfoSizeW(lpszFileName, &dwHandle);
        if (!dwVerSize)
        {
            break;
        }

        szVersionBuffer = (WCHAR*)malloc(dwVerSize * sizeof(WCHAR));
        if (!szVersionBuffer)
        {
            break;
        }
        RtlZeroMemory(szVersionBuffer, dwVerSize);

        if (!GetFileVersionInfoW(lpszFileName, 0, dwVerSize, szVersionBuffer))
        {
            break;
        }

        VS_FIXEDFILEINFO* pInfo;
        unsigned int nInfoLen;
        if (!VerQueryValueW(szVersionBuffer, L"\\", (void**)&pInfo, &nInfoLen))
        {
            break;
        }

        wnsprintfW(
            outBuf,
            size,
            L"%d.%d.%d.%d",
            HIWORD(pInfo->dwFileVersionMS),
            LOWORD(pInfo->dwFileVersionMS),
            HIWORD(pInfo->dwFileVersionLS),
            LOWORD(pInfo->dwFileVersionLS)
            );

        bRet = TRUE;
    } while (FALSE);

    if (szVersionBuffer)
    {
        free((void*)szVersionBuffer);
    }

    return bRet;
}

BOOL __stdcall GetPeVersionA(LPCSTR lpszFileName, LPSTR outBuf, UINT size) {
    MemoryAlloc<WCHAR> allocer;
    WCHAR *buff = allocer.GetMemory(size);
    buff[0] = 0;

    BOOL stat = GetPeVersionW(AtoW(lpszFileName).c_str(), buff, size);
    if (buff)
    {
        lstrcpyA(outBuf, WtoA(buff).c_str());
    }
    return stat;
}

BOOL __stdcall IsSystem64()
{
#if WIN64 || _WIN64
    return TRUE;
#else
    typedef void (WINAPI* pfnGetNativeSystemInfo)(LPSYSTEM_INFO);
    pfnGetNativeSystemInfo pGetNativeSystemInfo;
    SYSTEM_INFO SysInfo;

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (!hKernel32)
    {
        return FALSE;
    }

    pGetNativeSystemInfo = (pfnGetNativeSystemInfo)GetProcAddress(hKernel32, "GetNativeSystemInfo");
    if (pGetNativeSystemInfo == NULL)
    {
        return FALSE;
    }

    pGetNativeSystemInfo(&SysInfo);

    if (SysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
    {
        return FALSE;
    }

    return TRUE;
#endif
}

PVOID __stdcall DisableWow64Red()
{
#if _WIN64 || WIN64
    return (PVOID)0xffffffff;
#else
    if (!IsSystem64())
    {
        return (PVOID)0xffffffff;
    }

    typedef BOOL (WINAPI* pfnDisable)(PVOID*);
    pfnDisable disable = (pfnDisable)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Wow64DisableWow64FsRedirection");
    if (disable)
    {
        PVOID oldValue = NULL;
        if (disable(&oldValue))
        {
            return oldValue;
        }
    }

    return (PVOID)0xffffffff;
#endif
}

BOOL __stdcall RevertWow64Red(PVOID oldValue)
{
#if _WIN64 || WIN64
    return FALSE;
#else
    if ((PVOID)-1 == oldValue)
    {
        return FALSE;
    }

    typedef BOOL (WINAPI* pfnRevert)(PVOID);
    pfnRevert revert = (pfnRevert)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Wow64RevertWow64FsRedirection");
    if (revert)
    {
        return revert(oldValue); 
    }

    return FALSE;
#endif
}

void __stdcall IterateModulesW(DWORD procId, pfnModuleHandlerW handler, void* lpParam)
{
    do
    {
        if (!handler)
        {
            break;
        }

        MODULEENTRY32W me32 = {0};
        me32.dwSize = sizeof(me32);

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, procId);
        if (INVALID_HANDLE_VALUE == hSnap)
        {
            int e = GetLastError();
            break;
        }

        BOOL bMore = Module32FirstW(hSnap, &me32);
        while (bMore)
        {
            if (!handler(&me32, lpParam))
            {
                // 回调方要求退出
                break;
            }

            bMore = Module32NextW(hSnap, &me32);
        }

        CloseHandle(hSnap);
    } while (FALSE);
}

static BOOL _GetNtVersionNumbers(DWORD &dwMajorVer, DWORD &dwMinorVer, DWORD &dwBuildNumber)
{
    static BOOL s_bInit = FALSE;
    static DWORD s_dwMajorVer;
    static DWORD s_dwMinorVer;
    static DWORD s_dwBuildNumber;

    if (s_bInit)
    {
        dwMajorVer = s_dwMajorVer;
        dwMinorVer = s_dwMinorVer;
        dwBuildNumber = s_dwBuildNumber;
        return TRUE;
    }

    HMODULE hModNtdll = GetModuleHandleA("ntdll.dll");
    if (hModNtdll)
    {
        typedef void (WINAPI* RtlGetNtVersionNumbers)(DWORD*, DWORD*, DWORD*);
        RtlGetNtVersionNumbers pRtlGetNtVersionNumbers = (RtlGetNtVersionNumbers)GetProcAddress(hModNtdll, "RtlGetNtVersionNumbers");
        if (pRtlGetNtVersionNumbers)
        {
            pRtlGetNtVersionNumbers(&dwMajorVer, &dwMinorVer, &dwBuildNumber);
            dwBuildNumber &= 0x0ffff;
            s_dwMajorVer = dwMajorVer;
            s_dwMinorVer = dwMinorVer;
            s_dwBuildNumber = dwBuildNumber;
            s_bInit = TRUE;
        }
    }
    return s_bInit;
}

static BOOL _IsWin81Later()
{
    DWORD dwMajorVer = 0;
    DWORD dwMinorVer = 0;
    DWORD dwBuild = 0;

    _GetNtVersionNumbers(dwMajorVer, dwMinorVer, dwBuild);
    if ((dwMajorVer == 6 && dwMinorVer >= 3) || dwMajorVer > 6)
    {
        return TRUE;
    }
    return FALSE;
}

typedef enum VDPROCESSINFOCLASS
{
    VDProcessBasicInformation,      // 0, q: PROCESS_BASIC_INFORMATION, PROCESS_EXTENDED_BASIC_INFORMATION
    VDProcessQuotaLimits,           // qs: QUOTA_LIMITS, QUOTA_LIMITS_EX
    VDProcessIoCounters,            // q: IO_COUNTERS
    VDProcessVmCounters,            // q: VM_COUNTERS, VM_COUNTERS_EX, VM_COUNTERS_EX2
    VDProcessTimes,                 // q: KERNEL_USER_TIMES
    VDProcessBasePriority,          // s: KPRIORITY
    VDProcessRaisePriority,         // s: ULONG
    VDProcessDebugPort,             // q: HANDLE
    VDProcessExceptionPort,         // s: HANDLE
    VDProcessAccessToken,           // s: PROCESS_ACCESS_TOKEN
    VDProcessLdtInformation,        // 10, qs: PROCESS_LDT_INFORMATION
    VDProcessLdtSize,               // s: PROCESS_LDT_SIZE
    VDProcessDefaultHardErrorMode,  // qs: ULONG
    VDProcessIoPortHandlers,        // (kernel-mode only)
    VDProcessPooledUsageAndLimits,  // q: POOLED_USAGE_AND_LIMITS
    VDProcessWorkingSetWatch,       // q: PROCESS_WS_WATCH_INFORMATION[]; s: void
    VDProcessUserModeIOPL,
    VDProcessEnableAlignmentFaultFixup, // s: BOOLEAN
    VDProcessPriorityClass,         // qs: PROCESS_PRIORITY_CLASS
    VDProcessWx86Information,
    VDProcessHandleCount,           // 20, q: ULONG, PROCESS_HANDLE_INFORMATION
    VDProcessAffinityMask,          // s: KAFFINITY
    VDProcessPriorityBoost,         // qs: ULONG
    VDProcessDeviceMap,             // qs: PROCESS_DEVICEMAP_INFORMATION, PROCESS_DEVICEMAP_INFORMATION_EX
    VDProcessSessionInformation,    // q: PROCESS_SESSION_INFORMATION
    VDProcessForegroundInformation, // s: PROCESS_FOREGROUND_BACKGROUND
    VDProcessWow64Information,      // q: ULONG_PTR
    VDProcessImageFileName,         // q: UNICODE_STRING
    VDProcessLUIDDeviceMapsEnabled, // q: ULONG
    VDProcessBreakOnTermination,    // qs: ULONG
    VDProcessDebugObjectHandle,     // 30, q: HANDLE
    VDProcessDebugFlags,            // qs: ULONG
    VDProcessHandleTracing,         // q: PROCESS_HANDLE_TRACING_QUERY; s: size 0 disables, otherwise enables
    VDProcessIoPriority,            // qs: ULONG
    VDProcessExecuteFlags,          // qs: ULONG
    VDProcessResourceManagement,
    VDProcessCookie,                // q: ULONG
    VDProcessImageInformation,      // q: SECTION_IMAGE_INFORMATION
    VDProcessCycleTime,             // q: PROCESS_CYCLE_TIME_INFORMATION // since VISTA
    VDProcessPagePriority,          // q: ULONG
    VDProcessInstrumentationCallback, // 40
    VDProcessThreadStackAllocation, // s: PROCESS_STACK_ALLOCATION_INFORMATION, PROCESS_STACK_ALLOCATION_INFORMATION_EX
    VDProcessWorkingSetWatchEx,     // q: PROCESS_WS_WATCH_INFORMATION_EX[]
    VDProcessImageFileNameWin32,    // q: UNICODE_STRING
    VDProcessImageFileMapping,      // q: HANDLE (input)
    VDProcessAffinityUpdateMode,    // qs: PROCESS_AFFINITY_UPDATE_MODE
    VDProcessMemoryAllocationMode,  // qs: PROCESS_MEMORY_ALLOCATION_MODE
    VDProcessGroupInformation,      // q: USHORT[]
    VDProcessTokenVirtualizationEnabled, // s: ULONG
    VDProcessConsoleHostProcess,    // q: ULONG_PTR
    VDProcessWindowInformation,     // 50, q: PROCESS_WINDOW_INFORMATION
    VDProcessHandleInformation,     // q: PROCESS_HANDLE_SNAPSHOT_INFORMATION // since WIN8
    VDProcessMitigationPolicy,      // s: PROCESS_MITIGATION_POLICY_INFORMATION
    VDProcessDynamicFunctionTableInformation,
    VDProcessHandleCheckingMode,
    VDProcessKeepAliveCount,        // q: PROCESS_KEEPALIVE_COUNT_INFORMATION
    VDProcessRevokeFileHandles,     // s: PROCESS_REVOKE_FILE_HANDLES_INFORMATION
    VDProcessWorkingSetControl,     // s: PROCESS_WORKING_SET_CONTROL
    VDProcessHandleTable,           // since WINBLUE
    VDProcessCheckStackExtentsMode,
    VDProcessCommandLineInformation,    // 60, q: UNICODE_STRING
    VDProcessProtectionInformation,     // q: PS_PROTECTION
    VDProcessMemoryExhaustion,          // PROCESS_MEMORY_EXHAUSTION_INFO // since THRESHOLD
    VDProcessFaultInformation,          // PROCESS_FAULT_INFORMATION
    VDProcessTelemetryIdInformation,    // PROCESS_TELEMETRY_ID_INFORMATION
    VDProcessCommitReleaseInformation,  // PROCESS_COMMIT_RELEASE_INFORMATION
    VDProcessDefaultCpuSetsInformation,
    VDProcessAllowedCpuSetsInformation,
    VDProcessReserved1Information,
    VDProcessReserved2Information,
    VDProcessSubsystemProcess,          // 70
    VDProcessJobMemoryInformation,      // PROCESS_JOB_MEMORY_INFO
    VDMaxProcessInfoClass
} VDPROCESSINFOCLASS;

#define RTL_MAX_DRIVE_LETTERS 32

typedef NTSTATUS (WINAPI *pfnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    ULONG ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
    );

typedef struct _RTL_DRIVE_LETTER_CURDIR
{
    USHORT Flags;
    USHORT Length;
    ULONG TimeStamp;
    STRING DosPath;
} RTL_DRIVE_LETTER_CURDIR, *PRTL_DRIVE_LETTER_CURDIR;

typedef struct _CURDIR
{
    UNICODE_STRING DosPath;
    HANDLE Handle;
} CURDIR, *PCURDIR;

typedef struct _RTL_USER_PROCESS_PARAMETERS
{
    ULONG MaximumLength;
    ULONG Length;

    ULONG Flags;
    ULONG DebugFlags;

    HANDLE ConsoleHandle;
    ULONG ConsoleFlags;
    HANDLE StandardInput;
    HANDLE StandardOutput;
    HANDLE StandardError;

    CURDIR CurrentDirectory;
    UNICODE_STRING DllPath;
    UNICODE_STRING ImagePathName;
    UNICODE_STRING CommandLine;
    PVOID Environment;

    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG FillAttribute;

    ULONG WindowFlags;
    ULONG ShowWindowFlags;
    UNICODE_STRING WindowTitle;
    UNICODE_STRING DesktopInfo;
    UNICODE_STRING ShellInfo;
    UNICODE_STRING RuntimeData;
    RTL_DRIVE_LETTER_CURDIR CurrentDirectories[32];

    ULONG EnvironmentSize;
    ULONG EnvironmentVersion;
    PVOID PackageDependencyData;
    ULONG ProcessGroupId;
    ULONG LoaderThreads;
} RTL_USER_PROCESS_PARAMETERS, *PRTL_USER_PROCESS_PARAMETERS;

#define WOW64_POINTER(Type) ULONG

typedef struct _STRING32
{
    USHORT Length;
    USHORT MaximumLength;
    ULONG Buffer;
} STRING32, *PSTRING32;

typedef struct _RTL_DRIVE_LETTER_CURDIR32
{
    USHORT Flags;
    USHORT Length;
    ULONG TimeStamp;
    STRING32 DosPath;
} RTL_DRIVE_LETTER_CURDIR32, *PRTL_DRIVE_LETTER_CURDIR32;

typedef STRING32 UNICODE_STRING32, *PUNICODE_STRING32;

typedef struct _CURDIR32
{
    UNICODE_STRING32 DosPath;
    WOW64_POINTER(HANDLE) Handle;
} CURDIR32, *PCURDIR32;

typedef struct _RTL_USER_PROCESS_PARAMETERS32
{
    ULONG MaximumLength;
    ULONG Length;

    ULONG Flags;
    ULONG DebugFlags;

    WOW64_POINTER(HANDLE) ConsoleHandle;
    ULONG ConsoleFlags;
    WOW64_POINTER(HANDLE) StandardInput;
    WOW64_POINTER(HANDLE) StandardOutput;
    WOW64_POINTER(HANDLE) StandardError;

    CURDIR32 CurrentDirectory;
    UNICODE_STRING32 DllPath;
    UNICODE_STRING32 ImagePathName;
    UNICODE_STRING32 CommandLine;
    WOW64_POINTER(PVOID) Environment;

    ULONG StartingX;
    ULONG StartingY;
    ULONG CountX;
    ULONG CountY;
    ULONG CountCharsX;
    ULONG CountCharsY;
    ULONG FillAttribute;

    ULONG WindowFlags;
    ULONG ShowWindowFlags;
    UNICODE_STRING32 WindowTitle;
    UNICODE_STRING32 DesktopInfo;
    UNICODE_STRING32 ShellInfo;
    UNICODE_STRING32 RuntimeData;
    RTL_DRIVE_LETTER_CURDIR32 CurrentDirectories[RTL_MAX_DRIVE_LETTERS];

    ULONG EnvironmentSize;
    ULONG EnvironmentVersion;
    WOW64_POINTER(PVOID) PackageDependencyData;
    ULONG ProcessGroupId;
    ULONG LoaderThreads;
} RTL_USER_PROCESS_PARAMETERS32, *PRTL_USER_PROCESS_PARAMETERS32;

typedef enum _PH_PEB_OFFSET
{
    PhpoCurrentDirectory,
    PhpoDllPath,
    PhpoImagePathName,
    PhpoCommandLine,
    PhpoWindowTitle,
    PhpoDesktopInfo,
    PhpoShellInfo,
    PhpoRuntimeData,
    PhpoTypeMask = 0xffff,

    PhpoWow64 = 0x10000
} PH_PEB_OFFSET;

pfnNtQueryInformationProcess GetNtQueryInformationProc()
{
    static pfnNtQueryInformationProcess s_pfn = NULL;
    if (!s_pfn)
    {
        s_pfn = (pfnNtQueryInformationProcess)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    }
    return s_pfn;
}

static BOOL _PhpQueryProcessVariableSize(
    _In_ HANDLE ProcessHandle,
    _In_ VDPROCESSINFOCLASS ProcessInformationClass,
    _Out_ char *pBuffer,
    IN OUT DWORD *pLength
    )
{
    pfnNtQueryInformationProcess pfn = GetNtQueryInformationProc();
    NTSTATUS status;
    ULONG retLength = 0;

    status = pfn(
        ProcessHandle,
        ProcessInformationClass,
        NULL,
        0,
        &retLength
        );

    if (!retLength)
    {
        return FALSE;
    }

    if (pLength[0] < retLength)
    {
        pLength[0] = retLength;
        return FALSE;
    }

    status = pfn(
        ProcessHandle,
        ProcessInformationClass,
        pBuffer,
        pLength[0],
        &retLength
        );
    return (0 == status);
}

#define PTR_ADD_OFFSET(Pointer, Offset) ((PVOID)((ULONG_PTR)(Pointer) + (ULONG_PTR)(Offset)))

#define GDI_HANDLE_BUFFER_SIZE32 34
#define GDI_HANDLE_BUFFER_SIZE64 60

#ifndef WIN64
#define GDI_HANDLE_BUFFER_SIZE GDI_HANDLE_BUFFER_SIZE32
#else
#define GDI_HANDLE_BUFFER_SIZE GDI_HANDLE_BUFFER_SIZE64
#endif

typedef ULONG GDI_HANDLE_BUFFER[GDI_HANDLE_BUFFER_SIZE];

typedef struct _PEB_LDR_DATA
{
    ULONG Length;
    BOOLEAN Initialized;
    HANDLE SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID EntryInProgress;
    BOOLEAN ShutdownInProgress;
    HANDLE ShutdownThreadId;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _VDPEB
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    union
    {
        BOOLEAN BitField;
        struct
        {
            BOOLEAN ImageUsesLargePages : 1;
            BOOLEAN IsProtectedProcess : 1;
            BOOLEAN IsImageDynamicallyRelocated : 1;
            BOOLEAN SkipPatchingUser32Forwarders : 1;
            BOOLEAN IsPackagedProcess : 1;
            BOOLEAN IsAppContainer : 1;
            BOOLEAN IsProtectedProcessLight : 1;
            BOOLEAN SpareBits : 1;
        };
    };
    HANDLE Mutant;

    PVOID ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    PRTL_USER_PROCESS_PARAMETERS ProcessParameters;
    PVOID SubSystemData;
    PVOID ProcessHeap;
    PRTL_CRITICAL_SECTION FastPebLock;
    PVOID AtlThunkSListPtr;
    PVOID IFEOKey;
    union
    {
        ULONG CrossProcessFlags;
        struct
        {
            ULONG ProcessInJob : 1;
            ULONG ProcessInitializing : 1;
            ULONG ProcessUsingVEH : 1;
            ULONG ProcessUsingVCH : 1;
            ULONG ProcessUsingFTH : 1;
            ULONG ReservedBits0 : 27;
        };
        ULONG EnvironmentUpdateCount;
    };
    union
    {
        PVOID KernelCallbackTable;
        PVOID UserSharedInfoPtr;
    };
    ULONG SystemReserved[1];
    ULONG AtlThunkSListPtr32;
    PVOID ApiSetMap;
    ULONG TlsExpansionCounter;
    PVOID TlsBitmap;
    ULONG TlsBitmapBits[2];
    PVOID ReadOnlySharedMemoryBase;
    PVOID HotpatchInformation;
    PVOID *ReadOnlyStaticServerData;
    PVOID AnsiCodePageData;
    PVOID OemCodePageData;
    PVOID UnicodeCaseTableData;

    ULONG NumberOfProcessors;
    ULONG NtGlobalFlag;

    LARGE_INTEGER CriticalSectionTimeout;
    SIZE_T HeapSegmentReserve;
    SIZE_T HeapSegmentCommit;
    SIZE_T HeapDeCommitTotalFreeThreshold;
    SIZE_T HeapDeCommitFreeBlockThreshold;

    ULONG NumberOfHeaps;
    ULONG MaximumNumberOfHeaps;
    PVOID *ProcessHeaps;

    PVOID GdiSharedHandleTable;
    PVOID ProcessStarterHelper;
    ULONG GdiDCAttributeList;

    PRTL_CRITICAL_SECTION LoaderLock;

    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    USHORT OSBuildNumber;
    USHORT OSCSDVersion;
    ULONG OSPlatformId;
    ULONG ImageSubsystem;
    ULONG ImageSubsystemMajorVersion;
    ULONG ImageSubsystemMinorVersion;
    ULONG_PTR ImageProcessAffinityMask;
    GDI_HANDLE_BUFFER GdiHandleBuffer;
    PVOID PostProcessInitRoutine;

    PVOID TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32];

    ULONG SessionId;

    ULARGE_INTEGER AppCompatFlags;
    ULARGE_INTEGER AppCompatFlagsUser;
    PVOID pShimData;
    PVOID AppCompatInfo;

    UNICODE_STRING CSDVersion;

    PVOID ActivationContextData;
    PVOID ProcessAssemblyStorageMap;
    PVOID SystemDefaultActivationContextData;
    PVOID SystemAssemblyStorageMap;

    SIZE_T MinimumStackCommit;

    PVOID *FlsCallback;
    LIST_ENTRY FlsListHead;
    PVOID FlsBitmap;
    ULONG FlsBitmapBits[FLS_MAXIMUM_AVAILABLE / (sizeof(ULONG) * 8)];
    ULONG FlsHighIndex;

    PVOID WerRegistrationData;
    PVOID WerShipAssertPtr;
    PVOID pContextData;
    PVOID pImageHeaderHash;
    union
    {
        ULONG TracingFlags;
        struct
        {
            ULONG HeapTracingEnabled : 1;
            ULONG CritSecTracingEnabled : 1;
            ULONG LibLoaderTracingEnabled : 1;
            ULONG SpareTracingBits : 29;
        };
    };
    ULONGLONG CsrServerReadOnlySharedMemoryBase;
} VDPEB, *PVDPEB;

typedef ULONG GDI_HANDLE_BUFFER32[GDI_HANDLE_BUFFER_SIZE32];
typedef ULONG GDI_HANDLE_BUFFER64[GDI_HANDLE_BUFFER_SIZE64];

typedef struct _PEB32
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    union
    {
        BOOLEAN BitField;
        struct
        {
            BOOLEAN ImageUsesLargePages : 1;
            BOOLEAN IsProtectedProcess : 1;
            BOOLEAN IsLegacyProcess : 1;
            BOOLEAN IsImageDynamicallyRelocated : 1;
            BOOLEAN SkipPatchingUser32Forwarders : 1;
            BOOLEAN IsPackagedProcess : 1;
            BOOLEAN IsAppContainer : 1;
            BOOLEAN SpareBits : 1;
        };
    };
    WOW64_POINTER(HANDLE) Mutant;

    WOW64_POINTER(PVOID) ImageBaseAddress;
    WOW64_POINTER(PPEB_LDR_DATA) Ldr;
    WOW64_POINTER(PRTL_USER_PROCESS_PARAMETERS) ProcessParameters;
    WOW64_POINTER(PVOID) SubSystemData;
    WOW64_POINTER(PVOID) ProcessHeap;
    WOW64_POINTER(PRTL_CRITICAL_SECTION) FastPebLock;
    WOW64_POINTER(PVOID) AtlThunkSListPtr;
    WOW64_POINTER(PVOID) IFEOKey;
    union
    {
        ULONG CrossProcessFlags;
        struct
        {
            ULONG ProcessInJob : 1;
            ULONG ProcessInitializing : 1;
            ULONG ProcessUsingVEH : 1;
            ULONG ProcessUsingVCH : 1;
            ULONG ProcessUsingFTH : 1;
            ULONG ReservedBits0 : 27;
        };
        ULONG EnvironmentUpdateCount;
    };
    union
    {
        WOW64_POINTER(PVOID) KernelCallbackTable;
        WOW64_POINTER(PVOID) UserSharedInfoPtr;
    };
    ULONG SystemReserved[1];
    ULONG AtlThunkSListPtr32;
    WOW64_POINTER(PVOID) ApiSetMap;
    ULONG TlsExpansionCounter;
    WOW64_POINTER(PVOID) TlsBitmap;
    ULONG TlsBitmapBits[2];
    WOW64_POINTER(PVOID) ReadOnlySharedMemoryBase;
    WOW64_POINTER(PVOID) HotpatchInformation;
    WOW64_POINTER(PVOID *) ReadOnlyStaticServerData;
    WOW64_POINTER(PVOID) AnsiCodePageData;
    WOW64_POINTER(PVOID) OemCodePageData;
    WOW64_POINTER(PVOID) UnicodeCaseTableData;

    ULONG NumberOfProcessors;
    ULONG NtGlobalFlag;

    LARGE_INTEGER CriticalSectionTimeout;
    WOW64_POINTER(SIZE_T) HeapSegmentReserve;
    WOW64_POINTER(SIZE_T) HeapSegmentCommit;
    WOW64_POINTER(SIZE_T) HeapDeCommitTotalFreeThreshold;
    WOW64_POINTER(SIZE_T) HeapDeCommitFreeBlockThreshold;

    ULONG NumberOfHeaps;
    ULONG MaximumNumberOfHeaps;
    WOW64_POINTER(PVOID *) ProcessHeaps;

    WOW64_POINTER(PVOID) GdiSharedHandleTable;
    WOW64_POINTER(PVOID) ProcessStarterHelper;
    ULONG GdiDCAttributeList;

    WOW64_POINTER(PRTL_CRITICAL_SECTION) LoaderLock;

    ULONG OSMajorVersion;
    ULONG OSMinorVersion;
    USHORT OSBuildNumber;
    USHORT OSCSDVersion;
    ULONG OSPlatformId;
    ULONG ImageSubsystem;
    ULONG ImageSubsystemMajorVersion;
    ULONG ImageSubsystemMinorVersion;
    WOW64_POINTER(ULONG_PTR) ImageProcessAffinityMask;
    GDI_HANDLE_BUFFER32 GdiHandleBuffer;
    WOW64_POINTER(PVOID) PostProcessInitRoutine;

    WOW64_POINTER(PVOID) TlsExpansionBitmap;
    ULONG TlsExpansionBitmapBits[32];

    ULONG SessionId;

    ULARGE_INTEGER AppCompatFlags;
    ULARGE_INTEGER AppCompatFlagsUser;
    WOW64_POINTER(PVOID) pShimData;
    WOW64_POINTER(PVOID) AppCompatInfo;

    UNICODE_STRING32 CSDVersion;

    WOW64_POINTER(PVOID) ActivationContextData;
    WOW64_POINTER(PVOID) ProcessAssemblyStorageMap;
    WOW64_POINTER(PVOID) SystemDefaultActivationContextData;
    WOW64_POINTER(PVOID) SystemAssemblyStorageMap;

    WOW64_POINTER(SIZE_T) MinimumStackCommit;

    WOW64_POINTER(PVOID *) FlsCallback;
    LIST_ENTRY32 FlsListHead;
    WOW64_POINTER(PVOID) FlsBitmap;
    ULONG FlsBitmapBits[FLS_MAXIMUM_AVAILABLE / (sizeof(ULONG) * 8)];
    ULONG FlsHighIndex;

    WOW64_POINTER(PVOID) WerRegistrationData;
    WOW64_POINTER(PVOID) WerShipAssertPtr;
    WOW64_POINTER(PVOID) pContextData;
    WOW64_POINTER(PVOID) pImageHeaderHash;
    union
    {
        ULONG TracingFlags;
        struct
        {
            ULONG HeapTracingEnabled : 1;
            ULONG CritSecTracingEnabled : 1;
            ULONG LibLoaderTracingEnabled : 1;
            ULONG SpareTracingBits : 29;
        };
    };
    ULONGLONG CsrServerReadOnlySharedMemoryBase;
} PEB32, *PPEB32;

static BOOL _GetProcressPebString(HANDLE hProcress, ULONG eOffsetType, char *buffer, DWORD *pLength)
{
    ULONG uOffset = 0;
    switch (eOffsetType)
    {
    case PhpoCurrentDirectory:
        break;
    case (PhpoCurrentDirectory | PhpoWow64):
        break;
    case  PhpoCommandLine:
        uOffset = FIELD_OFFSET(RTL_USER_PROCESS_PARAMETERS, CommandLine);
        break;
    case (PhpoCommandLine | PhpoWow64):
        uOffset = FIELD_OFFSET(RTL_USER_PROCESS_PARAMETERS32, CommandLine);
        break;
    default:
        return FALSE;
    }

    pfnNtQueryInformationProcess pfn = GetNtQueryInformationProc();
    BOOL stat = TRUE;
    do 
    {
        //x86
        if (eOffsetType & PhpoWow64)
        {
            PVOID peb32;
            ULONG processParameters32;
            UNICODE_STRING unicodeString32;

            stat &= (0 == pfn(hProcress, ProcessWow64Information, &peb32, sizeof(PVOID), NULL));
            stat &= ReadProcessMemory(
                hProcress,
                PTR_ADD_OFFSET(peb32, FIELD_OFFSET(PEB32, ProcessParameters)),
                &processParameters32,
                sizeof(ULONG),
                NULL
                );

            stat &= ReadProcessMemory(
                hProcress,
                PTR_ADD_OFFSET(processParameters32, uOffset),
                &unicodeString32,
                sizeof(UNICODE_STRING32),
                NULL
                );

            if (unicodeString32.Length + 1 >= (int)pLength[0])
            {
                pLength[0] = unicodeString32.Length;
                break;
            }

            UNICODE_STRING32 *pStr = (UNICODE_STRING32 *)buffer;
            stat &= ReadProcessMemory(
                hProcress,
                (LPCVOID)unicodeString32.Buffer,
                pStr,
                unicodeString32.Length,
                NULL
                );
        }
        //x64
        else
        {
            PROCESS_BASIC_INFORMATION basicInfo;
            PVOID processParameters;
            UNICODE_STRING unicodeString;

            // Get the PEB address
            stat &= (0 == pfn(hProcress, ProcessBasicInformation, &basicInfo, sizeof(basicInfo), NULL));
            stat &= ReadProcessMemory(
                hProcress,
                PTR_ADD_OFFSET(basicInfo.PebBaseAddress, FIELD_OFFSET(VDPEB, ProcessParameters)),
                &processParameters,
                sizeof(PVOID),
                NULL
                );

            stat &= ReadProcessMemory(
                hProcress,
                PTR_ADD_OFFSET(processParameters, uOffset),
                &unicodeString,
                sizeof(UNICODE_STRING),
                NULL
                );

            if (unicodeString.Length + 1 >= (int)pLength[0])
            {
                pLength[0] = unicodeString.Length + 512;
                break;
            }

            UNICODE_STRING *pStr = (UNICODE_STRING *)buffer;
            stat &= ReadProcessMemory(hProcress,
                unicodeString.Buffer,
                pStr,
                unicodeString.Length,
                NULL
                );
        }
    } while (FALSE);
    

    return stat;
}

ustring __stdcall GetProcessCommandLineW(_In_ DWORD dwPid, BOOL bx64)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwPid);
    HandleAutoClose abc(hProcess);
    if (!hProcess)
    {
        return L"";
    }

    char buffer[1024];
    DWORD size = 1024;
    char *pBufHeader = buffer;
    BOOL bStat = false;
    DWORD tmp = size;
    MemoryAlloc<char> allocer;

    do
    {
        if (_IsWin81Later())
        {
            bStat = _PhpQueryProcessVariableSize(hProcess, VDProcessCommandLineInformation, pBufHeader, &tmp);
            if (!bStat && tmp > 1024 * 1024 * 16)
            {
                break;
            }

            if (!bStat && tmp > size)
            {
                size += (tmp + 4);
                pBufHeader = allocer.GetMemory(size);

                tmp = size;
                bStat = _PhpQueryProcessVariableSize(hProcess, VDProcessCommandLineInformation, pBufHeader, &tmp);
            }
        }
        else
        {
            ULONG uFlag = (PhpoCommandLine | PhpoWow64);
            if (bx64)
            {
                uFlag = PhpoCommandLine;
            }

            tmp = size;
            bStat = _GetProcressPebString(hProcess, uFlag, pBufHeader, &tmp);

            if (!bStat && tmp > 1024 * 1024 * 16)
            {
                break;
            }

            if (!bStat && tmp > size)
            {
                size += (tmp + 4);
                pBufHeader = allocer.GetMemory(size);

                tmp = size;
                bStat = _GetProcressPebString(hProcess, uFlag, pBufHeader, &tmp);
            }
        }
    } while (false);

    ustring result;
    if (bStat)
    {
        UNICODE_STRING *ptr = (UNICODE_STRING *)(pBufHeader);
        result = (LPCWSTR)ptr->Buffer;
    }
    return result;
}

mstring __stdcall GetProcessCommandLineA(_In_ DWORD dwPid, BOOL bx64) {
    return WtoA(GetProcessCommandLineW(dwPid, bx64));
}

BOOL __stdcall ShlParseShortcutsW(LPCWSTR wszLnkFile, PGDS_LINKINFO info)
{
    BOOL bRet = FALSE;
    IShellLinkW* pLink = NULL;
    IPersistFile* ppf = NULL;

    do
    {
        if (!wszLnkFile || !info)
        {
            break;
        }

        WIN32_FIND_DATAW wfd = {0};
        if ((S_OK != CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&pLink)) ||
            (S_OK != (pLink->QueryInterface(IID_IPersistFile, (void**)&ppf))) ||
            (S_OK != ppf->Load(wszLnkFile, STGM_READ)) ||
            (S_OK != pLink->Resolve(NULL, SLR_ANY_MATCH | SLR_NO_UI)) ||
            (S_OK != pLink->GetPath(EXPAND_ARG(info->wszPath), &wfd, SLGP_RAWPATH))
            )
        {
            break;
        }

        pLink->GetArguments(EXPAND_ARG(info->wszArgs));
        pLink->GetIconLocation(EXPAND_ARG(info->wszIcon), &(info->nIconIdx));
        pLink->GetWorkingDirectory(EXPAND_ARG(info->wszWorkDir));
        pLink->GetDescription(EXPAND_ARG(info->wszDesc));

        bRet = TRUE;
    } while (FALSE);

    if (pLink)
    {
        pLink->Release();
    }

    if (ppf)
    {
        ppf->Release();
    }

    return bRet;
}

DWORD WINAPI RegGetDWORDFromRegA(HKEY hKey, LPCSTR subKey, LPCSTR value, DWORD defaultVal)
{
    DWORD ret = defaultVal;
    DWORD type = REG_DWORD;
    DWORD len = sizeof(DWORD);
    SHGetValueA(hKey, subKey, value, &type, &ret, &len);

    return ret;
}

DWORD WINAPI RegGetDWORDFromRegW(HKEY hKey, LPCWSTR subKey, LPCWSTR value, DWORD defaultVal)
{
    DWORD ret = defaultVal;
    DWORD type = REG_DWORD;
    DWORD len = sizeof(DWORD);
    SHGetValueW(hKey, subKey, value, &type, &ret, &len);

    return ret;
}

BOOL WINAPI RegSetDWORDValueA(HKEY hKey, LPCSTR szSubKey, LPCSTR szValue, DWORD dwData)
{
    if (!szSubKey || !szValue)
    {
        return FALSE;
    }

    return (ERROR_SUCCESS == SHSetValueA(hKey, szSubKey, szValue, REG_DWORD, (LPVOID)&dwData, sizeof(DWORD)));
}

BOOL WINAPI RegSetDWORDValueW(HKEY hKey, LPCWSTR wszSubKey, LPCWSTR wszValue, DWORD dwData)
{
    if (!wszSubKey || !wszValue)
    {
        return FALSE;
    }

    return (ERROR_SUCCESS == SHSetValueW(hKey, wszSubKey, wszValue, REG_DWORD, (LPVOID)&dwData, sizeof(DWORD)));
}

BOOL __stdcall RegSetStrValueW(HKEY hKey, LPCWSTR wszSubKey, LPCWSTR wszValue, LPCWSTR wszData)
{
    return (ERROR_SUCCESS == SHSetValueW(
        hKey,
        wszSubKey,
        wszValue,
        REG_SZ,
        wszData,
        (lstrlenW(wszData) + 1) * sizeof(WCHAR)
        ));
}

BOOL __stdcall RegSetStrValueA(HKEY hKey, LPCSTR szSubKey, LPCSTR szValue, LPCSTR szData) {
    return (ERROR_SUCCESS == SHSetValueA(
        hKey,
        szSubKey,
        szValue,
        REG_SZ,
        szData,
        (lstrlenA(szData) + 1)
        ));
}

std::ustring __stdcall RegGetStrValueExW(HKEY hKey, LPCWSTR wszSubKey, LPCWSTR wszValue)
{
    if (!wszSubKey || !wszValue)
    {
        return L"";
    }
    DWORD dwLength = 0;
    LPVOID pBuf = NULL;
    DWORD dwType = 0;
    SHGetValueW(
        hKey,
        wszSubKey,
        wszValue,
        &dwType,
        (LPVOID)pBuf,
        &dwLength
        );
    if (REG_SZ != dwType || !dwLength)
    {
        return L"";
    }
    dwLength += 2;
    pBuf = new BYTE[dwLength];
    memset(pBuf, 0x00, dwLength);
    SHGetValueW(
        hKey,
        wszSubKey,
        wszValue,
        NULL,
        pBuf,
        &dwLength
        );
    std::ustring wstrRes = (LPCWSTR)pBuf;
    delete []pBuf;
    return wstrRes;
}

std::mstring __stdcall RegGetStrValueExA(HKEY hKey, LPCSTR szSubKey, LPCSTR szValue) {
    ustring wstr = RegGetStrValueExW(hKey, AtoW(szSubKey).c_str(), AtoW(szValue).c_str());

    return WtoA(wstr);
}

void test()
{
    int offset = (PhpoCurrentDirectory | PhpoWow64);
    switch (offset)
    {
    case  PhpoCurrentDirectory:
        {
            int d = 1;
        }
        break;
    case  PhpoCurrentDirectory | PhpoWow64:
        {
            int d = 2;
        }
        break;
    }
}

BOOL __stdcall IsSameFileW(LPCWSTR file1, LPCWSTR file2)
{
    BOOL bRet = FALSE;
    HANDLE hFile1 = INVALID_HANDLE_VALUE;
    HANDLE hFile2 = INVALID_HANDLE_VALUE;

    do
    {
        if (!file1 || !file2)
        {
            break;
        }

        // 同一个文件名则视为相同
        if (0 == StrCmpIW(file1, file2))
        {
            bRet = TRUE;
            break;
        }

        hFile1 = CreateFileW(
            file1,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
            );
        if (INVALID_HANDLE_VALUE == hFile1)
        {
            break;
        }

        hFile2 = CreateFileW(
            file2,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
            );
        if (INVALID_HANDLE_VALUE == hFile2)
        {
            break;
        }

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile1, &fileSize))
        {
            break;
        }

        LARGE_INTEGER fileSize2;
        if (!GetFileSizeEx(hFile2, &fileSize2))
        {
            break;
        }

        // 文件大小不同则自然不会相同
        if (fileSize2.QuadPart != fileSize.QuadPart)
        {
            break;
        }

        LONGLONG curReaded = 0;
        char byte1[4096];
        char byte2[4096];
        DWORD readed1;
        DWORD readed2;
        BOOL bSame = TRUE;

        while (curReaded < fileSize.QuadPart)
        {
            if (!ReadFile(hFile1, byte1, 4096, &readed1, NULL))
            {
                bSame = FALSE;
                break;
            }

            if (!ReadFile(hFile2, byte2, 4096, &readed2, NULL))
            {
                bSame = FALSE;
                break;
            }

            if (readed1 != readed2)
            {
                bSame = FALSE;
                break;
            }

            if (memcmp(byte1, byte2, readed1))
            {
                bSame = FALSE;
                break;
            }

            curReaded += readed1;
        }

        bRet = bSame;
    } while (FALSE);

    if (INVALID_HANDLE_VALUE != hFile2)
    {
        CloseHandle(hFile2);
    }

    if (INVALID_HANDLE_VALUE != hFile1)
    {
        CloseHandle(hFile1);
    }

    return bRet;
}

BOOL __stdcall IsSameFileA(LPCSTR file1, LPCSTR file2) {
    return IsSameFileW(AtoW(file1).c_str(), AtoW(file2).c_str());
}

std::mstring __stdcall GetStrFormJson(const Value &json, const std::mstring &name) {
    Value node = json[name];
    if (node.type() != nullValue)
    {
        if (node.type() == stringValue)
        {
            return node.asString();
        } else {
            return FastWriter().write(node);
        }
    }
    return "";
}

int __stdcall GetIntFromJson(const Value &json, const std::mstring &name) {
    Value node = json[name];
    if (node.type() == intValue)
    {
        return node.asInt();
    }
    return 0;
}

std::ustring __stdcall GetWindowStrW(HWND hwnd) {
    if (!IsWindow(hwnd))
    {
        return L"";
    }

    WCHAR buffer[256];
    buffer[0] = 0;
    int size = GetWindowTextLength(hwnd);
    if (size < 256)
    {
        GetWindowTextW(hwnd, buffer, 256);
        return buffer;
    } else {
        MemoryAlloc<WCHAR> alloc;
        WCHAR *ptr = alloc.GetMemory(size + 4);
        GetWindowTextW(hwnd, ptr, size + 4);
        return ptr;
    }
}

std::mstring __stdcall GetWindowStrA(HWND hwnd) {
    return WtoA(GetWindowStrW(hwnd));
}

typedef struct _REG_VALUE_HANDLER_DECORATOR
{
    pfnRegValueHandlerA handler;
    void* lpParam;
} REG_VALUE_HANDLER_DECORATOR, *PREG_VALUE_HANDLER_DECORATOR;

static BOOL WINAPI _GdRegValueEnumTransfer(LPCWSTR wszValue, void* lpParam)
{
    PREG_VALUE_HANDLER_DECORATOR pRVHD = (PREG_VALUE_HANDLER_DECORATOR)lpParam;

    return pRVHD->handler(WtoA(wszValue).c_str(), pRVHD->lpParam);
}

BOOL WINAPI RegEnumValuesA(HKEY hKey, LPCSTR szSubKey, pfnRegValueHandlerA handler, void* lpParam)
{
    if (!szSubKey || !handler)
    {
        return FALSE;
    }

    REG_VALUE_HANDLER_DECORATOR rvhd = {handler, lpParam};

    return RegEnumValuesW(hKey, AtoW(szSubKey).c_str(), _GdRegValueEnumTransfer, (void*)&rvhd);
}

BOOL WINAPI RegEnumValuesW(HKEY hKey, LPCWSTR wszSubKey, pfnRegValueHandlerW handler, void* lpParam)
{
    HKEY hTarKey = 0;
    BOOL bRet = FALSE;

    do
    {
        if (ERROR_SUCCESS != RegOpenKeyExW(hKey, wszSubKey, 0, KEY_READ | KEY_QUERY_VALUE, &hTarKey))
        {
            break;
        }

        WCHAR wszValueName[MAX_PATH] = {0};
        DWORD dwValueNameSize = RTL_NUMBER_OF(wszValueName);
        DWORD dwIndex = 0;

        while (ERROR_SUCCESS == RegEnumValueW(
            hTarKey,
            dwIndex,
            wszValueName,
            &dwValueNameSize,
            NULL,
            NULL,
            NULL,
            NULL
            ))
        {
            dwIndex++;
            bRet = TRUE;
            if (!handler(wszValueName, lpParam))
            {
                break;
            }
            RtlZeroMemory(wszValueName, sizeof(WCHAR) * RTL_NUMBER_OF(wszValueName));
            dwValueNameSize = RTL_NUMBER_OF(wszValueName);
        }
    } while (FALSE);

    if (hTarKey)
    {
        RegCloseKey(hTarKey);
    }

    return bRet;
}


//Dos文件路径转为Nt路径
mstring __stdcall DosPathToNtPath(LPCSTR szSrc)
{
    DWORD dwDrivers = GetLogicalDrives();
    int iIdex = 0;
    char szNT[] = "X:";
    char szDos[MAX_PATH] = {0x00};
    mstring strHeader;
    mstring strNtPath;
    mstring strDos(szSrc);
    for (iIdex = 0 ; iIdex < 26 ; iIdex++)
    {
        if ((1 << iIdex) & dwDrivers)
        {
            szNT[0] = 'A' + iIdex;
            if (QueryDosDeviceA(szNT, szDos, MAX_PATH))
            {
                if (0 == strDos.comparei(szDos))
                {
                    strNtPath += szNT;
                    strNtPath += (strDos.c_str() + lstrlenA(szDos));
                    return strNtPath;
                }
            }
        }
    }
    return "";
}

mstring __stdcall GetProcPathByPid(IN DWORD dwPid)
{
    if (4 == dwPid || 0 == dwPid)
    {
        return "";
    }

    mstring strPath;
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPid);

    char image[MAX_PATH] = {0x00};
    if (process)
    {
        GetProcessImageFileNameA(process, image, MAX_PATH);
        if (image[4] != 0x00)
        {
            strPath = DosPathToNtPath(image);
        }
    }
    else
    {
        return "";
    }

    if (process && INVALID_HANDLE_VALUE != process)
    {
        CloseHandle(process);
    }
    return strPath;
}

mstring __stdcall GetFilePathFromHandle(HANDLE hFile)
{
    DWORD dwFileSizeLow = GetFileSize(hFile, NULL); 
    HANDLE hFileMap = NULL;
    void* pMem = NULL;
    mstring strPath;

    do 
    {
        if (!dwFileSizeLow)
        {
            break;
        }

        hFileMap = CreateFileMapping(hFile, 
            NULL, 
            PAGE_READONLY,
            0, 
            1,
            NULL
            );
        if (!hFileMap)
        {
            break;
        }

        pMem = MapViewOfFile(hFileMap, FILE_MAP_READ, 0, 0, 1);
        if (!pMem)
        {
            break;
        }

        CHAR szFileName[MAX_PATH] = {0};
        GetMappedFileNameA(
            GetCurrentProcess(), 
            pMem, 
            szFileName,
            MAX_PATH
            );
        if (!szFileName[0])
        {
            break;
        }

        strPath = DosPathToNtPath(szFileName);
    } while (FALSE);

    if (hFileMap)
    {
        CloseHandle(hFileMap);
    }

    if (pMem)
    {
        UnmapViewOfFile(pMem);
    }
    return strPath;
}

mstring __stdcall GetStdErrorStr
(DWORD dwErr)
{
    LPVOID lpMsgBuf = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |  
        FORMAT_MESSAGE_FROM_SYSTEM |  
        FORMAT_MESSAGE_IGNORE_INSERTS, 
        NULL,
        dwErr,
        MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT), //Default language  
        (LPSTR)&lpMsgBuf,  
        0,  
        NULL  
        ); 
    mstring strMsg((LPCSTR)lpMsgBuf);
    if (lpMsgBuf)
    {
        LocalFlags(lpMsgBuf);
    }
    return strMsg;
}

// 注意，此函数的第二个参数很畸形，如果在本函数内声明 PACL* 并且释放的话，则安全描述符失效
// 为了应对此问题，需要调用方提供 PACL* 并在调用完此函数后自行释放内存
static BOOL WINAPI _SecGenerateLowSD(SECURITY_DESCRIPTOR* pSecDesc, PACL* pDacl)
{
    PSID pSidWorld = NULL;
    EXPLICIT_ACCESS ea;
    SID_IDENTIFIER_AUTHORITY sia = SECURITY_WORLD_SID_AUTHORITY;
    BOOL bRet = FALSE;

    do
    {
        if (!pSecDesc || !pDacl)
        {
            break;
        }

        if (AllocateAndInitializeSid(&sia, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &pSidWorld) == 0)
        {
            break;
        }

        ea.grfAccessMode = GRANT_ACCESS;
        ea.grfAccessPermissions = FILE_ALL_ACCESS ;
        ea.grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
        ea.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
        ea.Trustee.pMultipleTrustee = NULL;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = (LPTSTR)pSidWorld;

        if (SetEntriesInAcl(1, &ea, NULL, pDacl) != ERROR_SUCCESS)
        {
            break;
        }

        if (InitializeSecurityDescriptor(pSecDesc, SECURITY_DESCRIPTOR_REVISION) == 0)
        {
            break;
        }

        if (SetSecurityDescriptorDacl(pSecDesc, TRUE, *pDacl, FALSE) == 0)
        {
            break;
        }

        bRet = TRUE;
    } while (FALSE);

    if (NULL != pSidWorld)
    {
        FreeSid(pSidWorld);
        pSidWorld = NULL;
    }
    return bRet;
}

HANDLE WINAPI CreateLowsdEvent(BOOL bReset, BOOL bInitStat, LPCSTR szName)
{
    SECURITY_DESCRIPTOR secDesc;
    PACL pDacl = NULL;
    SECURITY_ATTRIBUTES secAttr;
    secAttr.nLength = sizeof(secAttr);
    secAttr.lpSecurityDescriptor = &secDesc;
    secAttr.bInheritHandle = FALSE;
    if (!_SecGenerateLowSD(&secDesc, &pDacl))
    {
        if (pDacl)
        {
            LocalFree(pDacl);
        }
        return FALSE;
    }

    HANDLE hEvent = CreateEventA(&secAttr, bReset, bInitStat, szName);
    if (pDacl)
    {
        LocalFree(pDacl);
    }
    return hEvent;
}

static DWORD _GetCurSessionId()
{
    WTS_SESSION_INFOW *pSessions = NULL;
    DWORD dwSessionCount = 0;
    DWORD dwActiveSession = -1;
    WTSEnumerateSessionsW(
        WTS_CURRENT_SERVER_HANDLE,
        0,
        1,
        &pSessions,
        &dwSessionCount
        );
    DWORD dwIdex = 0;
    for (dwIdex = 0 ; dwIdex < dwSessionCount ; dwIdex++)
    {
        if (WTSActive == pSessions[dwIdex].State)
        {
            dwActiveSession = pSessions[dwIdex].SessionId;
            break;
        }
    }
    if (pSessions)
    {
        WTSFreeMemory(pSessions);
    }

    return dwActiveSession;
}

static HANDLE _GetProcessToken(DWORD dwPid)
{
    BOOL bRet = FALSE;
    HANDLE hToken = NULL;
    HANDLE hDup = NULL;
    HANDLE hProcess = NULL;
    do
    {
        if (!(hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwPid)))
        {
            break;
        }

        if (!OpenProcessToken(hProcess, TOKEN_ALL_ACCESS, &hToken))
        {
            break;
        }

        if (!DuplicateTokenEx(
            hToken,
            MAXIMUM_ALLOWED,
            NULL,
            SecurityIdentification,
            TokenPrimary,
            &hDup
            ))
        {
            break;
        }
    } while(FALSE);

    if (hToken)
    {
        CloseHandle(hToken);
    }

    if (hProcess)
    {
        CloseHandle(hProcess);
    }
    return hDup;
}

BOOL WINAPI RunInSession(LPCSTR szImage, LPCSTR szCmd, DWORD dwSessionId, DWORD dwShell)
{
    if (!szImage || !*szImage)
    {
        return FALSE;
    }

    if (INVALID_FILE_ATTRIBUTES == GetFileAttributesA(szImage))
    {
        return FALSE;
    }

    BOOL bStat = FALSE;
    HANDLE hThis = NULL;
    HANDLE hDup = NULL;
    LPVOID pEnv = NULL;
    HANDLE hShellToken = NULL;
    DWORD  dwFlag = NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;
    do
    {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hThis))
        {
            break;
        }

        if (!DuplicateTokenEx(hThis, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &hDup))
        {
            break;
        }

        if (!dwSessionId)
        {
            dwSessionId = _GetCurSessionId();
        }

        if (-1 == dwSessionId)
        {
            break;
        }
        if (!SetTokenInformation(hDup, TokenSessionId, &dwSessionId, sizeof(DWORD)))
        {
            break;
        }

        if (dwShell)
        {
            hShellToken = _GetProcessToken(dwShell);
            CreateEnvironmentBlock(&pEnv, hShellToken, FALSE);
        }
        else
        {
            CreateEnvironmentBlock(&pEnv, hDup, FALSE);
        }

        CHAR szParam[1024] = {0};
        if (szCmd && szCmd[0])
        {
            wnsprintfA(szParam, 1024, "\"%hs\" \"%hs\"", szImage, szCmd);
        }
        else
        {
            wnsprintfA(szParam, 1024, "\"%hs\"", szImage);
        }
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi = {0};
        si.cb = sizeof(STARTUPINFO);
        si.lpDesktop = "WinSta0\\Default";
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = TRUE;
        bStat = CreateProcessAsUserA(
            hDup,
            NULL,
            szParam,
            NULL,
            NULL,
            FALSE,
            dwFlag,
            pEnv,
            NULL,
            &si,
            &pi
            );
        if (pi.hProcess)
        {
            CloseHandle(pi.hProcess);
        }

        if (pi.hThread)
        {
            CloseHandle(pi.hThread);
        }

        if (!bStat)
        {
        }
    } while (FALSE);

    if (pEnv)
    {
        DestroyEnvironmentBlock(pEnv);
        pEnv = NULL;
    }

    if (hThis && INVALID_HANDLE_VALUE != hThis)
    {
        CloseHandle(hThis);
        hThis = NULL;
    }

    if (hShellToken && INVALID_HANDLE_VALUE != hShellToken)
    {
        CloseHandle(hShellToken);
        hShellToken = NULL;
    }

    if (hDup && INVALID_HANDLE_VALUE != hDup)
    {
        CloseHandle(hDup);
        hDup = NULL;
    }
    return TRUE;
}

HANDLE ExecProcessW(LPCWSTR cmdLine, DWORD* procId, BOOL bShowWindow)
{
    if (!cmdLine)
    {
        return NULL;
    }

    STARTUPINFOW si = {sizeof(si)};
    if (!bShowWindow)
    {
        si.wShowWindow = SW_HIDE;
        si.dwFlags = STARTF_USESHOWWINDOW;
    }

    PROCESS_INFORMATION pi;

    LPWSTR wszCmdLine = (LPWSTR)malloc((MAX_PATH + lstrlenW(cmdLine)) * sizeof(WCHAR));
    lstrcpyW(wszCmdLine, cmdLine);
    if (CreateProcessW(NULL, wszCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);

        if (procId)
        {
            *procId = pi.dwProcessId;
        }

        free((void*)wszCmdLine);
        return pi.hProcess;
    }

    free((void*)wszCmdLine);
    return NULL;
}

HANDLE ExecProcessA(LPCSTR cmdLine, DWORD* procId, BOOL bShowWindow) {
    return ExecProcessW(AtoW(cmdLine).c_str(), procId, bShowWindow);
}

mstring GetWindowStr(HWND hwnd) {
    if (!IsWindow(hwnd))
    {
        return "";
    }

    char buffer[256];
    buffer[0] = 0;
    int size = GetWindowTextLength(hwnd);
    if (size < 256)
    {
        GetWindowTextA(hwnd, buffer, 256);
        return buffer;
    } else {
        MemoryAlloc<char> alloc;
        char *ptr = alloc.GetMemory(size + 4);
        GetWindowTextA(hwnd, ptr, size + 4);
        return ptr;
    }
}