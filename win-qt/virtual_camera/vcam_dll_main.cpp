/*
 * Mcaster1DSPEncoder — Windows Qt 6 Build
 * virtual_camera/vcam_dll_main.cpp — COM DLL entry points + self-registration
 *
 * This file provides:
 *   DllMain            — Thread init/cleanup
 *   DllGetClassObject  — COM class factory creation
 *   DllCanUnloadNow    — Safe unload check
 *   DllRegisterServer  — Register as DirectShow video input device
 *   DllUnregisterServer — Remove registration
 *
 * Build as a DLL: cl /LD vcam_filter.cpp vcam_dll_main.cpp /link ...
 * Register:   regsvr32 Mcaster1VirtualCam.dll
 * Unregister: regsvr32 /u Mcaster1VirtualCam.dll
 *
 * Copyright (c) 2026 David St. John <davestj@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "vcam_filter.h"
#include "vcam_guids.h"

#include <cstdio>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

static HMODULE g_hModule = nullptr;

/* ═══════════════════════════════════════════════════════════════════════
 * DllMain
 * ═══════════════════════════════════════════════════════════════════════ */

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════════
 * DllGetClassObject — COM asks us for a class factory
 * ═══════════════════════════════════════════════════════════════════════ */

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID *ppv)
{
    if (rclsid != CLSID_Mcaster1VirtualCam)
        return CLASS_E_CLASSNOTAVAILABLE;

    auto *factory = new mc1::VCamClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

/* ═══════════════════════════════════════════════════════════════════════
 * DllCanUnloadNow
 * ═══════════════════════════════════════════════════════════════════════ */

STDAPI DllCanUnloadNow()
{
    return (mc1::g_vcam_server_locks == 0 && mc1::g_vcam_object_count == 0)
           ? S_OK : S_FALSE;
}

/* ═══════════════════════════════════════════════════════════════════════
 * Registry helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static HRESULT SetRegKeyValue(HKEY hRoot, LPCWSTR subkey, LPCWSTR valueName,
                               LPCWSTR value)
{
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(hRoot, subkey, 0, nullptr, 0,
                              KEY_SET_VALUE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);

    rc = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                        reinterpret_cast<const BYTE *>(value),
                        static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(rc);
}

static void DeleteRegTree(HKEY hRoot, LPCWSTR subkey)
{
    SHDeleteKeyW(hRoot, subkey);
}

/* Convert GUID to registry string form {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} */
static void GuidToStr(const GUID &guid, wchar_t *buf, int bufLen)
{
    StringFromGUID2(guid, buf, bufLen);
}

/* ═══════════════════════════════════════════════════════════════════════
 * DllRegisterServer — Register as a DirectShow Video Input Device
 *
 * Creates:
 *   HKCR\CLSID\{our-clsid}\InprocServer32 = <path to DLL>
 *   HKCR\CLSID\{video-input-cat}\Instance\{our-clsid}\CLSID = {our-clsid}
 *   HKCR\CLSID\{video-input-cat}\Instance\{our-clsid}\FriendlyName = "..."
 * ═══════════════════════════════════════════════════════════════════════ */

STDAPI DllRegisterServer()
{
    wchar_t dllPath[MAX_PATH] = {};
    GetModuleFileNameW(g_hModule, dllPath, MAX_PATH);

    wchar_t clsidStr[64] = {};
    GuidToStr(CLSID_Mcaster1VirtualCam, clsidStr, 64);

    wchar_t catStr[64] = {};
    GuidToStr(CLSID_VideoInputDeviceCat, catStr, 64);

    HRESULT hr;

    /* 1. Register our CLSID under HKCR\CLSID\{our-clsid} */
    wchar_t key[256];

    swprintf_s(key, L"CLSID\\%s", clsidStr);
    hr = SetRegKeyValue(HKEY_CLASSES_ROOT, key, nullptr, mc1::kVCamFriendlyName);
    if (FAILED(hr)) return hr;

    swprintf_s(key, L"CLSID\\%s\\InprocServer32", clsidStr);
    hr = SetRegKeyValue(HKEY_CLASSES_ROOT, key, nullptr, dllPath);
    if (FAILED(hr)) return hr;
    hr = SetRegKeyValue(HKEY_CLASSES_ROOT, key, L"ThreadingModel", L"Both");
    if (FAILED(hr)) return hr;

    /* 2. Register in the VideoInputDevice category */
    swprintf_s(key, L"CLSID\\%s\\Instance\\%s", catStr, clsidStr);
    hr = SetRegKeyValue(HKEY_CLASSES_ROOT, key, L"CLSID", clsidStr);
    if (FAILED(hr)) return hr;
    hr = SetRegKeyValue(HKEY_CLASSES_ROOT, key, L"FriendlyName",
                        mc1::kVCamFriendlyName);
    if (FAILED(hr)) return hr;

    return S_OK;
}

/* ═══════════════════════════════════════════════════════════════════════
 * DllUnregisterServer — Remove all registry entries
 * ═══════════════════════════════════════════════════════════════════════ */

STDAPI DllUnregisterServer()
{
    wchar_t clsidStr[64] = {};
    GuidToStr(CLSID_Mcaster1VirtualCam, clsidStr, 64);

    wchar_t catStr[64] = {};
    GuidToStr(CLSID_VideoInputDeviceCat, catStr, 64);

    wchar_t key[256];

    /* Remove from category */
    swprintf_s(key, L"CLSID\\%s\\Instance\\%s", catStr, clsidStr);
    DeleteRegTree(HKEY_CLASSES_ROOT, key);

    /* Remove CLSID */
    swprintf_s(key, L"CLSID\\%s", clsidStr);
    DeleteRegTree(HKEY_CLASSES_ROOT, key);

    return S_OK;
}
