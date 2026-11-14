#include "Platform/Win32/Utility/Win32Compat.h"

#if defined(__clang__)
#pragma clang diagnostic ignored "-Winconsistent-dllimport"
#endif

#ifndef COPY_FILE_OPEN_AND_COPY_REPARSE_POINTS
#define COPY_FILE_OPEN_AND_COPY_REPARSE_POINTS 0x00200000
#endif
#ifndef COPY_FILE_NO_BUFFERING
#define COPY_FILE_NO_BUFFERING 0x00001000
#endif

#ifndef COPYFILE2_COPY_FLAG_FAIL_IF_EXISTS
#define COPYFILE2_COPY_FLAG_FAIL_IF_EXISTS 0x00000001
#define COPYFILE2_COPY_FLAG_RESTARTABLE 0x00000002
#define COPYFILE2_COPY_FLAG_OPEN_AND_COPY_REPARSE_POINTS 0x00000004
#define COPYFILE2_COPY_FLAG_NO_BUFFERING 0x00000008
#endif

#if (_WIN32_WINNT < 0x0602)
typedef struct _CREATEFILE2_EXTENDED_PARAMETERS {
    DWORD dwSize;
    DWORD dwFileAttributes;
    DWORD dwFileFlags;
    DWORD dwSecurityQosFlags;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes;
    HANDLE hTemplateFile;
} CREATEFILE2_EXTENDED_PARAMETERS, *PCREATEFILE2_EXTENDED_PARAMETERS, *LPCREATEFILE2_EXTENDED_PARAMETERS;
#endif

extern "C" {

// 1. CopyFile2 (Windows 8+)
HRESULT WINAPI CopyFile2(
    PCWSTR pwszExistingFileName,
    PCWSTR pwszNewFileName,
    COPYFILE2_EXTENDED_PARAMETERS *pExtendedParameters
) {
    if (!pwszExistingFileName || !pwszNewFileName) {
        return E_INVALIDARG;
    }

    typedef HRESULT (WINAPI *CopyFile2_Fn)(PCWSTR, PCWSTR, COPYFILE2_EXTENDED_PARAMETERS*);
    static CopyFile2_Fn s_real_copyfile2 = []() -> CopyFile2_Fn {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        if (hKernel) {
            return reinterpret_cast<CopyFile2_Fn>(GetProcAddress(hKernel, "CopyFile2"));
        }
        return nullptr;
    }();

    if (s_real_copyfile2) {
        return s_real_copyfile2(pwszExistingFileName, pwszNewFileName, pExtendedParameters);
    }

    // Windows 7 Fallback: emulate with CopyFileExW
    DWORD copyFlags = 0;
    BOOL *cancelPtr = nullptr;
    if (pExtendedParameters) {
        if (pExtendedParameters->dwCopyFlags & COPYFILE2_COPY_FLAG_FAIL_IF_EXISTS) {
            copyFlags |= COPY_FILE_FAIL_IF_EXISTS;
        }
        if (pExtendedParameters->dwCopyFlags & COPYFILE2_COPY_FLAG_RESTARTABLE) {
            copyFlags |= COPY_FILE_RESTARTABLE;
        }
        if (pExtendedParameters->dwCopyFlags & COPYFILE2_COPY_FLAG_OPEN_AND_COPY_REPARSE_POINTS) {
            copyFlags |= COPY_FILE_OPEN_AND_COPY_REPARSE_POINTS;
        }
        if (pExtendedParameters->dwCopyFlags & COPYFILE2_COPY_FLAG_NO_BUFFERING) {
            copyFlags |= COPY_FILE_NO_BUFFERING;
        }
        cancelPtr = pExtendedParameters->pfCancel;
    }

    BOOL ok = CopyFileExW(pwszExistingFileName, pwszNewFileName, nullptr, nullptr, cancelPtr, copyFlags);
    if (ok) {
        return S_OK;
    }
    return HRESULT_FROM_WIN32(GetLastError());
}

typedef HRESULT (WINAPI *PFN_CopyFile2)(PCWSTR, PCWSTR, COPYFILE2_EXTENDED_PARAMETERS*);
extern PFN_CopyFile2 const __imp_CopyFile2 = CopyFile2;

// 2. CreateFile2 (Windows 8+)
HANDLE WINAPI CreateFile2(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    DWORD dwCreationDisposition,
    LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams
) {
    typedef HANDLE (WINAPI *CreateFile2_Fn)(LPCWSTR, DWORD, DWORD, DWORD, LPCREATEFILE2_EXTENDED_PARAMETERS);
    static CreateFile2_Fn s_real_createfile2 = []() -> CreateFile2_Fn {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        if (hKernel) {
            return reinterpret_cast<CreateFile2_Fn>(GetProcAddress(hKernel, "CreateFile2"));
        }
        return nullptr;
    }();

    if (s_real_createfile2) {
        return s_real_createfile2(lpFileName, dwDesiredAccess, dwShareMode, dwCreationDisposition, pCreateExParams);
    }

    DWORD dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
    LPSECURITY_ATTRIBUTES lpSecurityAttributes = nullptr;
    HANDLE hTemplateFile = nullptr;

    if (pCreateExParams) {
        dwFlagsAndAttributes = pCreateExParams->dwFileAttributes | pCreateExParams->dwFileFlags | pCreateExParams->dwSecurityQosFlags;
        lpSecurityAttributes = pCreateExParams->lpSecurityAttributes;
        hTemplateFile = pCreateExParams->hTemplateFile;
    }

    return CreateFileW(
        lpFileName,
        dwDesiredAccess,
        dwShareMode,
        lpSecurityAttributes,
        dwCreationDisposition,
        dwFlagsAndAttributes,
        hTemplateFile
    );
}

typedef HANDLE (WINAPI *PFN_CreateFile2)(LPCWSTR, DWORD, DWORD, DWORD, LPCREATEFILE2_EXTENDED_PARAMETERS);
extern PFN_CreateFile2 const __imp_CreateFile2 = CreateFile2;

// 3. GetSystemTimePreciseAsFileTime (Windows 8+)
VOID WINAPI GetSystemTimePreciseAsFileTime(LPFILETIME lpSystemTimeAsFileTime) {
    typedef VOID (WINAPI *GetSystemTimePreciseAsFileTime_Fn)(LPFILETIME);
    static GetSystemTimePreciseAsFileTime_Fn s_real_fn = []() -> GetSystemTimePreciseAsFileTime_Fn {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        if (hKernel) {
            return reinterpret_cast<GetSystemTimePreciseAsFileTime_Fn>(GetProcAddress(hKernel, "GetSystemTimePreciseAsFileTime"));
        }
        return nullptr;
    }();

    if (s_real_fn) {
        s_real_fn(lpSystemTimeAsFileTime);
    } else if (lpSystemTimeAsFileTime) {
        GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
    }
}

typedef VOID (WINAPI *PFN_GetSystemTimePreciseAsFileTime)(LPFILETIME);
extern PFN_GetSystemTimePreciseAsFileTime const __imp_GetSystemTimePreciseAsFileTime = GetSystemTimePreciseAsFileTime;

// 4. SetThreadDescription (Windows 10+)
HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription) {
    typedef HRESULT (WINAPI *SetThreadDescription_Fn)(HANDLE, PCWSTR);
    static SetThreadDescription_Fn s_real_fn = []() -> SetThreadDescription_Fn {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        if (hKernel) {
            return reinterpret_cast<SetThreadDescription_Fn>(GetProcAddress(hKernel, "SetThreadDescription"));
        }
        return nullptr;
    }();

    if (s_real_fn) {
        return s_real_fn(hThread, lpThreadDescription);
    }
    return S_OK;
}

typedef HRESULT (WINAPI *PFN_SetThreadDescription)(HANDLE, PCWSTR);
extern PFN_SetThreadDescription const __imp_SetThreadDescription = SetThreadDescription;

// 5. GetThreadDescription (Windows 10+)
HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR *ppszThreadDescription) {
    typedef HRESULT (WINAPI *GetThreadDescription_Fn)(HANDLE, PWSTR*);
    static GetThreadDescription_Fn s_real_fn = []() -> GetThreadDescription_Fn {
        HMODULE hKernel = GetModuleHandleW(L"kernel32.dll");
        if (hKernel) {
            return reinterpret_cast<GetThreadDescription_Fn>(GetProcAddress(hKernel, "GetThreadDescription"));
        }
        return nullptr;
    }();

    if (s_real_fn) {
        return s_real_fn(hThread, ppszThreadDescription);
    }
    if (ppszThreadDescription) {
        *ppszThreadDescription = nullptr;
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

typedef HRESULT (WINAPI *PFN_GetThreadDescription)(HANDLE, PWSTR*);
extern PFN_GetThreadDescription const __imp_GetThreadDescription = GetThreadDescription;

// 6. Virtual Memory hints (Windows 8.1+)
DWORD WINAPI DiscardVirtualMemory(PVOID VirtualAddress, SIZE_T Size) {
    (void)VirtualAddress;
    (void)Size;
    return ERROR_SUCCESS;
}
typedef DWORD (WINAPI *PFN_DiscardVirtualMemory)(PVOID, SIZE_T);
extern PFN_DiscardVirtualMemory const __imp_DiscardVirtualMemory = DiscardVirtualMemory;

DWORD WINAPI OfferVirtualMemory(PVOID VirtualAddress, SIZE_T Size, DWORD Priority) {
    (void)VirtualAddress;
    (void)Size;
    (void)Priority;
    return ERROR_SUCCESS;
}
typedef DWORD (WINAPI *PFN_OfferVirtualMemory)(PVOID, SIZE_T, DWORD);
extern PFN_OfferVirtualMemory const __imp_OfferVirtualMemory = OfferVirtualMemory;

DWORD WINAPI ReclaimVirtualMemory(PVOID VirtualAddress, SIZE_T Size) {
    (void)VirtualAddress;
    (void)Size;
    return ERROR_SUCCESS;
}
typedef DWORD (WINAPI *PFN_ReclaimVirtualMemory)(PVOID, SIZE_T);
extern PFN_ReclaimVirtualMemory const __imp_ReclaimVirtualMemory = ReclaimVirtualMemory;

} // extern "C"
