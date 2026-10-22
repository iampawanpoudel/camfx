#include "Settings.h"
#include <shlobj.h>
#include <wincodec.h>
#include <wil/com.h>
#include <cwchar>

#pragma comment(lib, "windowscodecs")
#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")

namespace
{
    const wchar_t* kSection = L"CamFX";

    int GetInt(const std::wstring& path, const wchar_t* key, int def)
    {
        return (int)GetPrivateProfileIntW(kSection, key, def, path.c_str());
    }

    float GetFloat(const std::wstring& path, const wchar_t* key, float def)
    {
        wchar_t buf[32] = {};
        GetPrivateProfileStringW(kSection, key, L"", buf, ARRAYSIZE(buf), path.c_str());
        return buf[0] ? (float)wcstod(buf, nullptr) : def;
    }

    void Put(const std::wstring& path, const wchar_t* key, double v)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"%g", v);
        WritePrivateProfileStringW(kSection, key, buf, path.c_str());
    }
}

std::wstring CamFxDataDir()
{
    wil::unique_cotaskmem_string pd;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &pd))) return L"C:\\ProgramData\\CamFX";
    std::wstring dir = std::wstring(pd.get()) + L"\\CamFX";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring CamFxSettingsPath() { return CamFxDataDir() + L"\\settings.ini"; }
std::wstring CamFxBackgroundPath() { return CamFxDataDir() + L"\\background.img"; }

FxSettings LoadSettings()
{
    const std::wstring p = CamFxSettingsPath();
    FxSettings s;
    s.zoom = GetFloat(p, L"zoom", s.zoom);
    s.panX = GetFloat(p, L"panX", s.panX);
    s.panY = GetFloat(p, L"panY", s.panY);
    s.autoFrame = GetInt(p, L"autoFrame", s.autoFrame) != 0;
    s.mirror = GetInt(p, L"mirror", s.mirror) != 0;
    s.background = GetInt(p, L"background", s.background);
    s.blur = GetInt(p, L"blur", s.blur);
    s.brightness = GetInt(p, L"brightness", s.brightness);
    s.contrast = GetInt(p, L"contrast", s.contrast);
    s.saturation = GetInt(p, L"saturation", s.saturation);
    s.warmth = GetInt(p, L"warmth", s.warmth);
    s.exposure = GetInt(p, L"exposure", s.exposure);
    s.filter = GetInt(p, L"filter", s.filter);
    s.lowLight = GetInt(p, L"lowLight", s.lowLight) != 0;
    return s;
}

void SaveSettings(const FxSettings& s)
{
    const std::wstring p = CamFxSettingsPath();
    Put(p, L"zoom", s.zoom);
    Put(p, L"panX", s.panX);
    Put(p, L"panY", s.panY);
    Put(p, L"autoFrame", s.autoFrame);
    Put(p, L"mirror", s.mirror);
    Put(p, L"background", s.background);
    Put(p, L"blur", s.blur);
    Put(p, L"brightness", s.brightness);
    Put(p, L"contrast", s.contrast);
    Put(p, L"saturation", s.saturation);
    Put(p, L"warmth", s.warmth);
    Put(p, L"exposure", s.exposure);
    Put(p, L"filter", s.filter);
    Put(p, L"lowLight", s.lowLight);
}

bool FileChanged(const std::wstring& path, FILETIME* stamp)
{
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    FILETIME t = {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) t = fad.ftLastWriteTime;
    if (CompareFileTime(&t, stamp) == 0) return false;
    *stamp = t;
    return true;
}

bool LoadImageBgra(const std::wstring& path, std::vector<uint8_t>& bgra, int& w, int& h)
{
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    wil::com_ptr_nothrow<IWICBitmapFrameDecode> frame;
    wil::com_ptr_nothrow<IWICFormatConverter> conv;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return false;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder))) return false;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;
    if (FAILED(factory->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    UINT uw = 0, uh = 0;
    if (FAILED(conv->GetSize(&uw, &uh)) || uw == 0 || uh == 0 || uw > 8192 || uh > 8192) return false;
    bgra.resize((size_t)uw * uh * 4);
    if (FAILED(conv->CopyPixels(nullptr, uw * 4, (UINT)bgra.size(), bgra.data()))) return false;
    w = (int)uw; h = (int)uh;
    return true;
}

void SignalNamedEvent(const wchar_t* name)
{
    if (HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, name))
    {
        SetEvent(h);
        CloseHandle(h);
    }
}
