//
// Copyright (C) Microsoft Corporation. All rights reserved.
//
// 
// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "VirtualCameraMediaSourceActivate.h"

HINSTANCE   g_hInst;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        g_hInst = hModule;
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

bool __stdcall winrt_can_unload_now() noexcept
{
    if (winrt::get_module_lock())
    {
        return false;
    }

    winrt::clear_factory_cache();
    return true;
}

int32_t __stdcall WINRT_CanUnloadNow() noexcept
{
#ifdef _WRL_MODULE_H_
    if (!::Microsoft::WRL::Module<::Microsoft::WRL::InProc>::GetModule().Terminate())
    {
        return 1;
    }
#endif

    return winrt_can_unload_now() ? 0 : 1;
}


HRESULT __stdcall DllGetClassObject(GUID const& clsid, GUID const& iid, void** result)
{
    try
    {
        *result = nullptr;

        if (clsid == __uuidof(winrt::WindowsSample::implementation::VirtualCameraMediaSourceActivate))
        {
            return winrt::make_self<VirtualCameraMediaSourceActivateFactory>()->QueryInterface(iid, result);
        }

#ifdef _WRL_MODULE_H_
        return ::Microsoft::WRL::Module<::Microsoft::WRL::InProc>::GetModule().GetClassObject(clsid, iid, result);
#else
        return winrt::hresult_class_not_available().to_abi();
#endif
    }
    catch (...)
    {
        return winrt::to_hresult();
    }
}



// regsvr32 support. Frame Server only looks in HKLM, so this needs an elevated prompt.
STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH] = {};
    RETURN_LAST_ERROR_IF(GetModuleFileNameW(g_hInst, path, ARRAYSIZE(path)) == 0);

    std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + VIRTUALCAMERAMEDIASOURCE_CLSID;
    wil::unique_hkey hkey;
    RETURN_IF_WIN32_ERROR(RegCreateKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hkey, nullptr));
    RETURN_IF_WIN32_ERROR(RegSetValueExW(hkey.get(), nullptr, 0, REG_SZ, (const BYTE*)VIRTUALCAMERAMEDIASOURCE_FRIENDLYNAME,
        (DWORD)((wcslen(VIRTUALCAMERAMEDIASOURCE_FRIENDLYNAME) + 1) * sizeof(wchar_t))));

    wil::unique_hkey hinproc;
    RETURN_IF_WIN32_ERROR(RegCreateKeyExW(hkey.get(), L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &hinproc, nullptr));
    RETURN_IF_WIN32_ERROR(RegSetValueExW(hinproc.get(), nullptr, 0, REG_SZ, (const BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t))));
    static const wchar_t kBoth[] = L"Both";
    RETURN_IF_WIN32_ERROR(RegSetValueExW(hinproc.get(), L"ThreadingModel", 0, REG_SZ, (const BYTE*)kBoth, sizeof(kBoth)));
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    std::wstring key = std::wstring(L"Software\\Classes\\CLSID\\") + VIRTUALCAMERAMEDIASOURCE_CLSID;
    LSTATUS st = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key.c_str());
    return (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND) ? S_OK : HRESULT_FROM_WIN32(st);
}
