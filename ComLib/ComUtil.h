#ifndef COMUTIL_COMLIB_H_H_
#define COMUTIL_COMLIB_H_H_
#include <Windows.h>
#include <Shlwapi.h>
#include <stdlib.h>
#include <TlHelp32.h>
#include "json/json.h"
#include "mstring.h"
#include "StrUtil.h"

#pragma comment(lib, "shlwapi.lib")

#define EXPAND_ARG(arg)                 (arg), RTL_NUMBER_OF((arg))

typedef struct _FILE_MAPPING_STRUCT
{
    HANDLE hFile;
    HANDLE hMap;
    LPVOID lpView;
    LARGE_INTEGER fileSize;
    DWORD mappedSize;
} FILE_MAPPING_STRUCT, *PFILE_MAPPING_STRUCT;

PFILE_MAPPING_STRUCT __stdcall MappingFileA(LPCSTR fileName, BOOL bWrite = FALSE, DWORD maxViewSize = 1024 * 1024 * 64);
void __stdcall CloseFileMapping(PFILE_MAPPING_STRUCT pfms);

VOID __stdcall CentreWindow(HWND hSrcWnd, HWND hDstWnd = NULL);
DWORD __stdcall GetColourFromStr(LPCSTR szColour);

VOID __stdcall PrintDbgInternal(LPCWSTR wszTarget, LPCSTR wszFile, DWORD dwLine, LPCWSTR wszFormat, ...);
#define dp(f, ...) PrintDbgInternal(L"vdebug", __FILE__, __LINE__, f, ##__VA_ARGS__)

typedef BOOL (__stdcall* pfnProcHandlerW)(PPROCESSENTRY32W, void*);
void __stdcall IterateProcW(pfnProcHandlerW handler, void* lpParam);

BOOL  __stdcall IsPeFileA(LPCSTR fileName, BOOL* b64);

BOOL __stdcall IsSameFileW(LPCWSTR file1, LPCWSTR file2);
BOOL __stdcall IsSameFileA(LPCSTR file1, LPCSTR file2);

BOOL __stdcall GetPeVersionW(LPCWSTR lpszFileName, LPWSTR outBuf, UINT size);
BOOL __stdcall GetPeVersionA(LPCSTR lpszFileName, LPSTR outBuf, UINT size);

PVOID __stdcall DisableWow64Red();

BOOL __stdcall RevertWow64Red(PVOID oldValue);

typedef BOOL (__stdcall* pfnModuleHandlerW)(PMODULEENTRY32W, void*);
void __stdcall IterateModulesW(DWORD procId, pfnModuleHandlerW handler, void* lpParam);

void __stdcall ErrMessage(const wchar_t *format, ...);
std::ustring __stdcall GetProcessCommandLineW(_In_ DWORD dwPid, BOOL bx64);
std::mstring __stdcall GetProcessCommandLineA(_In_ DWORD dwPid, BOOL bx64);

//获取pe文件属性
//Comments InternalName ProductName 
//CompanyName LegalCopyright ProductVersion 
//FileDescription LegalTrademarks PrivateBuild 
//FileVersion OriginalFilename SpecialBuild 
std::mstring __stdcall GetPeDescStrA(const std::mstring &path, const std::mstring &attr);

typedef struct _GDS_LINKINFO
{
    WCHAR wszPath[MAX_PATH];
    WCHAR wszArgs[MAX_PATH * 2];
    WCHAR wszIcon[MAX_PATH];
    int nIconIdx;
    WCHAR wszWorkDir[MAX_PATH];
    WCHAR wszDesc[MAX_PATH];
} GDS_LINKINFO, *PGDS_LINKINFO;

BOOL __stdcall ShlParseShortcutsW(LPCWSTR wszLnkFile, PGDS_LINKINFO info);

DWORD __stdcall RegGetDWORDFromRegA(HKEY hKey, LPCSTR subKey, LPCSTR value, DWORD defaultVal);
DWORD __stdcall RegGetDWORDFromRegW(HKEY hKey, LPCWSTR subKey, LPCWSTR value, DWORD defaultVal);
BOOL __stdcall RegSetDWORDValueA(HKEY hKey, LPCSTR szSubKey, LPCSTR szValue, DWORD dwData);
BOOL __stdcall RegSetDWORDValueW(HKEY hKey, LPCWSTR wszSubKey, LPCWSTR wszValue, DWORD dwData);
BOOL __stdcall RegSetStrValueW(HKEY hKey, LPCWSTR wszSubKey, LPCWSTR wszValue, LPCWSTR wszData);
BOOL __stdcall RegSetStrValueA(HKEY hKey, LPCSTR szSubKey, LPCSTR szValue, LPCSTR szData);
std::ustring __stdcall RegGetStrValueExW(HKEY hKey, LPCWSTR wszSubKey, LPCWSTR wszValue);
std::mstring __stdcall RegGetStrValueExA(HKEY hKey, LPCSTR szSubKey, LPCSTR szValue);

typedef BOOL (__stdcall* pfnRegValueHandlerA)(LPCSTR, void*);
typedef BOOL (__stdcall* pfnRegValueHandlerW)(LPCWSTR, void*);
BOOL __stdcall RegEnumValuesA(HKEY hKey, LPCSTR szSubKey, pfnRegValueHandlerA handler, void* lpParam);
BOOL __stdcall RegEnumValuesW(HKEY hKey, LPCWSTR wszSubKey, pfnRegValueHandlerW handler, void* lpParam);

class HandleAutoClose {
public:
    inline HandleAutoClose(HANDLE h) {
        mHandle = h;
    }

    inline virtual ~HandleAutoClose() {
        if (mHandle&& INVALID_HANDLE_VALUE != mHandle)
        {
            CloseHandle(mHandle);
        }
    }

private:
    HANDLE mHandle;
};

template <class T>
class MemoryAlloc {
public:
    MemoryAlloc() {
        mBuffer = NULL;
        mSize = 0;
    }

    virtual ~MemoryAlloc() {
        if (mBuffer)
        {
            delete []mBuffer;
        }
    }

    T *GetMemory(int size) {
        if (size < mSize)
        {
            return mBuffer;
        } else {
            if (mBuffer)
            {
                delete []mBuffer;
            }
            mSize = size;
            mBuffer = new T[size];
        }
        return mBuffer;
    }

    T *GetPtr() {
        return mBuffer;
    }

    int GetSize() {
        return mSize;
    }

private:
    T *mBuffer;
    int mSize;
};

std::mstring __stdcall GetStrFormJson(const Json::Value &json, const std::mstring &name);
int __stdcall GetIntFromJson(const Json::Value &json, const std::mstring &name);

std::ustring __stdcall GetWindowStrW(HWND hwnd);
std::mstring __stdcall GetWindowStrA(HWND hwnd);

#define TIME_FORMAT1    ("%04d-%02d-%02d %02d:%02d:%02d %03d")
static std::mstring __stdcall GetCurTimeStr1(const char *fmt) {
    SYSTEMTIME time;
    GetLocalTime(&time);
    return FormatA(
        fmt,
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds
        );
}

std::mstring COMAPI __stdcall DosPathToNtPath(LPCSTR wszSrc);
std::mstring COMAPI __stdcall GetProcPathByPid(IN DWORD dwPid);
std::mstring COMAPI __stdcall GetFilePathFromHandle(HANDLE hFile);

std::mstring COMAPI __stdcall GetStdErrorStr(DWORD dwErr = GetLastError());

struct ThreadInformation
{
    DWORD m_dwThreadId;
    void *m_dwStartAddr;
    DWORD m_dwSwitchCount;
    void *m_dwTebBase;
    FILETIME m_vCreateTime;
    LONG m_Priority; 
    ULONG m_eStat;
    ULONG m_eWaitReason;

    ThreadInformation()
    {
        ZeroMemory(this, sizeof(ThreadInformation));
    }
};

HANDLE COMAPI WINAPI CreateLowsdEvent(BOOL bReset, BOOL bInitStat, LPCSTR szName);

BOOL COMAPI WINAPI RunInSession(LPCSTR szImage, LPCSTR szCmd, DWORD dwSessionId, DWORD dwShell);

HANDLE ExecProcessW(LPCWSTR cmdLine, DWORD* procId, BOOL bShowWindow);
HANDLE ExecProcessA(LPCSTR cmdLine, DWORD* procId, BOOL bShowWindow);

std::mstring GetWindowStr(HWND hwnd);
#endif //COMUTIL_COMLIB_H_H_