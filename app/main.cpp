// CamFX.exe: registers the "CamFX Camera" virtual camera, edits effect settings, and lives in the tray.
// The effects themselves run inside the media source DLL, so this app is optional while in a call.
#include <windows.h>
#include <initguid.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <sddl.h>
#include <uxtheme.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfvirtualcamera.h>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "resource.h"
#include "Settings.h"
#include "VirtualCameraMediaSource.h"

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mf")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "mfsensorgroup")
#pragma comment(lib, "comctl32")
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "advapi32")
#pragma comment(lib, "uxtheme")

namespace
{
    const UINT WM_TRAY = WM_APP + 1, WM_CALL = WM_APP + 2, WM_FRAME = WM_APP + 3, WM_STATUS = WM_APP + 4;
    const wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    HINSTANCE g_inst;
    HWND g_dlg;
    HFONT g_sectionFont = nullptr;
    HBRUSH g_windowBrush = nullptr;
    UINT g_taskbarCreated;
    NOTIFYICONDATAW g_nid = {};
    wil::com_ptr_nothrow<IMFVirtualCamera> g_vcam;
    std::vector<std::wstring> g_camLinks;
    bool g_autoShown = false;
    wil::unique_event g_quit;

    std::thread g_preview;
    std::atomic<bool> g_previewStop{ false };
    std::mutex g_frameLock;
    std::vector<uint8_t> g_frame;
    int g_frameW = 0, g_frameH = 0;

    // ---- app-only settings live in the same ini, section [App]
    std::wstring AppGet(const wchar_t* key, const wchar_t* def = L"")
    {
        wchar_t buf[1024] = {};
        GetPrivateProfileStringW(L"App", key, def, buf, ARRAYSIZE(buf), CamFxSettingsPath().c_str());
        return buf;
    }
    void AppSet(const wchar_t* key, const std::wstring& v)
    {
        WritePrivateProfileStringW(L"App", key, v.c_str(), CamFxSettingsPath().c_str());
    }

    void SetStatus(const std::wstring& s) { SetDlgItemTextW(g_dlg, IDC_STATUS, s.c_str()); }

    void ApplyModernTheme(HWND hwnd)
    {
        // Native controls keep the panel fast: no compositor, web view, or per-frame UI work.
        g_windowBrush = CreateSolidBrush(RGB(248, 250, 252));
        LOGFONTW lf = {};
        HDC dc = GetDC(hwnd);
        lf.lfHeight = -MulDiv(9, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(hwnd, dc);
        lf.lfWeight = FW_SEMIBOLD;
        wcscpy_s(lf.lfFaceName, L"Segoe UI Variable Text");
        g_sectionFont = CreateFontIndirectW(&lf);
        for (int id : { IDC_CAMERA_SECTION, IDC_FRAME_SECTION, IDC_LOOK_SECTION, IDC_SYSTEM_SECTION })
            SendDlgItemMessageW(hwnd, id, WM_SETFONT, (WPARAM)g_sectionFont, TRUE);
        for (int id : { IDC_CAMERA, IDC_BG, IDC_FILTER, IDC_BGIMAGE, IDC_RESET })
            SetWindowTheme(GetDlgItem(hwnd, id), L"Explorer", nullptr);
    }

    // Windows lists it as "CamFX Camera (Windows Virtual Camera)".
    bool IsCamFx(const std::wstring& name) { return name.rfind(VIRTUALCAMERAMEDIASOURCE_FRIENDLYNAME, 0) == 0; }

    // ---- cameras
    void EnumCameras(std::vector<std::wstring>& names, std::vector<std::wstring>& links)
    {
        wil::com_ptr_nothrow<IMFAttributes> attrs;
        if (FAILED(MFCreateAttributes(&attrs, 1))) return;
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (FAILED(MFEnumDeviceSources(attrs.get(), &devices, &count))) return;
        for (UINT32 i = 0; i < count; i++)
        {
            wil::unique_cotaskmem_string name, link;
            UINT32 cch = 0;
            if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &cch)) &&
                SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &cch)))
            {
                names.push_back(name.get());
                links.push_back(link.get());
            }
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
    }

    HRESULT CreateVirtualCamera(const std::wstring& physicalLink, wil::com_ptr_nothrow<IMFVirtualCamera>& vcam)
    {
        // Same parameters as an existing System-lifetime camera retakes it instead of adding a duplicate.
        RETURN_IF_FAILED(MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
            MFVirtualCameraAccess_CurrentUser, VIRTUALCAMERAMEDIASOURCE_FRIENDLYNAME, VIRTUALCAMERAMEDIASOURCE_CLSID,
            nullptr, 0, &vcam));
        RETURN_IF_FAILED(vcam->AddDeviceSourceInfo(physicalLink.c_str()));
        // Ask Frame Server for its shared source of the physical camera: it also offers NV12 decoded from MJPEG.
        RETURN_IF_FAILED(vcam->SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1));
        RETURN_IF_FAILED(vcam->SetString(VCAM_DEVICE_INFO, physicalLink.c_str()));
        return S_OK;
    }

    HRESULT RegisterVirtualCamera(const std::wstring& physicalLink)
    {
        if (g_vcam)
        {
            g_vcam->Remove();
            g_vcam.reset();
        }
        wil::com_ptr_nothrow<IMFVirtualCamera> vcam;
        RETURN_IF_FAILED(CreateVirtualCamera(physicalLink, vcam));
        RETURN_IF_FAILED(vcam->Start(nullptr));
        g_vcam = vcam;
        return S_OK;
    }

    HRESULT RemoveVirtualCamera()
    {
        std::wstring link = AppGet(L"camera");
        if (link.empty()) return S_FALSE;
        wil::com_ptr_nothrow<IMFVirtualCamera> vcam;
        RETURN_IF_FAILED(CreateVirtualCamera(link, vcam));
        RETURN_IF_FAILED(vcam->Remove());
        return S_OK;
    }

    void SetupCameras()
    {
        std::vector<std::wstring> names, links;
        EnumCameras(names, links);
        HWND combo = GetDlgItem(g_dlg, IDC_CAMERA);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        g_camLinks.clear();
        std::wstring saved = AppGet(L"camera");
        int sel = -1;
        for (size_t i = 0; i < names.size(); i++)
        {
            if (IsCamFx(names[i])) continue;
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)names[i].c_str());
            if (links[i] == saved) sel = (int)g_camLinks.size();
            g_camLinks.push_back(links[i]);
        }
        if (g_camLinks.empty())
        {
            SetStatus(L"No physical camera found");
            return;
        }
        if (sel < 0) sel = 0;
        SendMessageW(combo, CB_SETCURSEL, sel, 0);
        AppSet(L"camera", g_camLinks[sel]);
        HRESULT hr = RegisterVirtualCamera(g_camLinks[sel]);
        wchar_t msg[96];
        swprintf_s(msg, SUCCEEDED(hr) ? L"CamFX Camera ready" : L"Could not register CamFX Camera (0x%08X)", hr);
        SetStatus(msg);
    }

    // ---- controls <-> settings
    void InitTrack(int id, int lo, int hi, int pos)
    {
        HWND h = GetDlgItem(g_dlg, id);
        SendMessageW(h, TBM_SETRANGEMIN, FALSE, lo);
        SendMessageW(h, TBM_SETRANGEMAX, TRUE, hi); // redraw: SETPOS below is a no-op when pos is unchanged
        SendMessageW(h, TBM_SETPOS, TRUE, pos);
    }
    int Track(int id) { return (int)SendDlgItemMessageW(g_dlg, id, TBM_GETPOS, 0, 0); }
    bool Checked(int id) { return IsDlgButtonChecked(g_dlg, id) == BST_CHECKED; }
    int Combo(int id) { return (int)SendDlgItemMessageW(g_dlg, id, CB_GETCURSEL, 0, 0); }

    void UpdateEnables()
    {
        bool manualZoom = !Checked(IDC_AUTOFRAME);
        EnableWindow(GetDlgItem(g_dlg, IDC_ZOOM), manualZoom);
        EnableWindow(GetDlgItem(g_dlg, IDC_PANX), manualZoom && Track(IDC_ZOOM) > 100);
        EnableWindow(GetDlgItem(g_dlg, IDC_PANY), manualZoom && Track(IDC_ZOOM) > 100);
        EnableWindow(GetDlgItem(g_dlg, IDC_BLUR), Combo(IDC_BG) == BgBlur);
    }

    void LoadControls(const FxSettings& s)
    {
        InitTrack(IDC_ZOOM, 100, 400, (int)(s.zoom * 100 + 0.5f));
        InitTrack(IDC_PANX, -100, 100, (int)(s.panX * 100));
        InitTrack(IDC_PANY, -100, 100, (int)(s.panY * 100));
        InitTrack(IDC_BLUR, 1, 5, s.blur);
        InitTrack(IDC_BRIGHTNESS, -100, 100, s.brightness);
        InitTrack(IDC_CONTRAST, -100, 100, s.contrast);
        InitTrack(IDC_SATURATION, -100, 100, s.saturation);
        InitTrack(IDC_WARMTH, -100, 100, s.warmth);
        InitTrack(IDC_EXPOSURE, -100, 100, s.exposure);
        CheckDlgButton(g_dlg, IDC_AUTOFRAME, s.autoFrame);
        CheckDlgButton(g_dlg, IDC_MIRROR, s.mirror);
        CheckDlgButton(g_dlg, IDC_LOWLIGHT, s.lowLight);
        SendDlgItemMessageW(g_dlg, IDC_BG, CB_SETCURSEL, s.background, 0);
        SendDlgItemMessageW(g_dlg, IDC_FILTER, CB_SETCURSEL, s.filter, 0);
        UpdateEnables();
    }

    FxSettings ReadControls()
    {
        FxSettings s;
        s.zoom = Track(IDC_ZOOM) / 100.f;
        s.panX = Track(IDC_PANX) / 100.f;
        s.panY = Track(IDC_PANY) / 100.f;
        s.blur = Track(IDC_BLUR);
        s.brightness = Track(IDC_BRIGHTNESS);
        s.contrast = Track(IDC_CONTRAST);
        s.saturation = Track(IDC_SATURATION);
        s.warmth = Track(IDC_WARMTH);
        s.exposure = Track(IDC_EXPOSURE);
        s.autoFrame = Checked(IDC_AUTOFRAME);
        s.mirror = Checked(IDC_MIRROR);
        s.lowLight = Checked(IDC_LOWLIGHT);
        s.background = Combo(IDC_BG);
        s.filter = Combo(IDC_FILTER);
        return s;
    }

    void OnChanged()
    {
        UpdateEnables();
        SaveSettings(ReadControls());
    }

    // ---- start with Windows
    bool AutoStartEnabled()
    {
        return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"CamFX", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    }
    void SetAutoStart(bool on)
    {
        wil::unique_hkey key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
        if (!on)
        {
            RegDeleteValueW(key.get(), L"CamFX");
            return;
        }
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, ARRAYSIZE(exe));
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --tray";
        RegSetValueExW(key.get(), L"CamFX", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    }

    // ---- background image: copied next to the settings so Frame Server (LocalService) can read it
    void ChooseImage()
    {
        wchar_t file[MAX_PATH] = {};
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = g_dlg;
        ofn.lpstrFilter = L"Images\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.heic;*.webp\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = ARRAYSIZE(file);
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) return;

        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (!LoadImageBgra(file, px, w, h))
        {
            MessageBoxW(g_dlg, L"That image could not be read.", L"CamFX", MB_ICONWARNING);
            return;
        }
        if (!CopyFileW(file, CamFxBackgroundPath().c_str(), FALSE))
        {
            MessageBoxW(g_dlg, L"Could not copy the image to the CamFX data folder.", L"CamFX", MB_ICONWARNING);
            return;
        }
        SendDlgItemMessageW(g_dlg, IDC_BG, CB_SETCURSEL, BgImage, 0);
        OnChanged();
    }

    // ---- preview: reads "CamFX Camera" exactly like a call app would
    void PreviewLoop(HWND hwnd)
    {
        auto status = [&](const wchar_t* s) { PostMessageW(hwnd, WM_STATUS, 0, (LPARAM)_wcsdup(s)); };
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
        auto uninit = wil::scope_exit([] { CoUninitialize(); });

        std::vector<std::wstring> names, links;
        EnumCameras(names, links);
        std::wstring link;
        for (size_t i = 0; i < names.size(); i++)
            if (IsCamFx(names[i])) link = links[i];
        if (link.empty()) { status(L"CamFX Camera not found"); return; }

        wil::com_ptr_nothrow<IMFAttributes> attrs;
        wil::com_ptr_nothrow<IMFMediaSource> source;
        wil::com_ptr_nothrow<IMFSourceReader> reader;
        wil::com_ptr_nothrow<IMFMediaType> type;
        MFCreateAttributes(&attrs, 2);
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link.c_str());
        HRESULT hr = MFCreateDeviceSource(attrs.get(), &source);
        if (FAILED(hr)) { status(L"Could not open CamFX Camera"); return; }
        auto shutdown = wil::scope_exit([&] { reader.reset(); source->Shutdown(); });

        attrs.reset();
        MFCreateAttributes(&attrs, 1);
        attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (FAILED(MFCreateSourceReaderFromMediaSource(source.get(), attrs.get(), &reader)) ||
            FAILED(MFCreateMediaType(&type)) ||
            FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
            FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
            FAILED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.get())))
        {
            status(L"CamFX Camera has no usable format");
            return;
        }
        type.reset();
        UINT32 w = 0, h = 0;
        reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
        MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h);
        status(L"Previewing CamFX Camera");

        while (!g_previewStop)
        {
            DWORD stream = 0, flags = 0;
            LONGLONG ts = 0;
            wil::com_ptr_nothrow<IMFSample> sample;
            if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample)) ||
                (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)))
            {
                status(L"Preview stopped (camera busy or unplugged?)");
                break;
            }
            if (!sample) continue;
            wil::com_ptr_nothrow<IMFMediaBuffer> buf;
            if (FAILED(sample->GetBufferByIndex(0, &buf))) continue;
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            wil::com_ptr_nothrow<IMF2DBuffer> buf2d;
            bool is2d = SUCCEEDED(buf.query_to(&buf2d)) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch));
            DWORD len = 0;
            if (!is2d)
            {
                if (FAILED(buf->Lock(&scan0, nullptr, &len))) continue;
                pitch = (LONG)w * 4;
                if (len < w * h * 4) { buf->Unlock(); continue; }
            }
            {
                std::lock_guard<std::mutex> lock(g_frameLock);
                g_frame.resize((size_t)w * h * 4);
                for (UINT32 r = 0; r < h; r++) memcpy(&g_frame[(size_t)r * w * 4], scan0 + (ptrdiff_t)r * pitch, (size_t)w * 4);
                g_frameW = (int)w; g_frameH = (int)h;
            }
            if (is2d) buf2d->Unlock2D(); else buf->Unlock();
            PostMessageW(hwnd, WM_FRAME, 0, 0);
        }
    }

    void StopPreview()
    {
        g_previewStop = true;
        if (g_preview.joinable()) g_preview.join();
        std::lock_guard<std::mutex> lock(g_frameLock);
        g_frame.clear();
        g_frameW = g_frameH = 0;
    }

    void UpdatePreview()
    {
        bool want = IsWindowVisible(g_dlg) && Checked(IDC_PREVIEW_ON) && !g_autoShown;
        if (want == g_preview.joinable()) return;
        StopPreview();
        if (want)
        {
            g_previewStop = false;
            g_preview = std::thread(PreviewLoop, g_dlg);
        }
        InvalidateRect(GetDlgItem(g_dlg, IDC_PREVIEW), nullptr, FALSE);
    }

    void DrawPreview(const DRAWITEMSTRUCT* di)
    {
        RECT rc = di->rcItem;
        HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
        std::lock_guard<std::mutex> lock(g_frameLock);
        if (g_frame.empty()) { FillRect(di->hDC, &rc, black); return; }
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        int dw = cw, dh = cw * g_frameH / g_frameW;
        if (dh > ch) { dh = ch; dw = ch * g_frameW / g_frameH; }
        // Paint only the letterbox bars: filling under the frame first flashes black on every frame.
        int dx = rc.left + (cw - dw) / 2, dy = rc.top + (ch - dh) / 2;
        int saved = SaveDC(di->hDC);
        ExcludeClipRect(di->hDC, dx, dy, dx + dw, dy + dh);
        FillRect(di->hDC, &rc, black);
        RestoreDC(di->hDC, saved);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = g_frameW;
        bi.bmiHeader.biHeight = -g_frameH; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        SetStretchBltMode(di->hDC, HALFTONE);
        StretchDIBits(di->hDC, dx, dy, dw, dh, 0, 0, g_frameW, g_frameH,
            g_frame.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    }

    // ---- call start/stop events from the media source (running as LocalService in Frame Server)
    void WatchCalls(HWND hwnd)
    {
        // Owner and SYSTEM get full access, LocalService may only signal.
        wil::unique_hlocal_security_descriptor sd;
        SECURITY_ATTRIBUTES sa = { sizeof(sa) };
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;OW)(A;;GA;;;SY)(A;;0x00100002;;;LS)",
            SDDL_REVISION_1, &sd, nullptr))
        {
            sa.lpSecurityDescriptor = sd.get();
        }
        wil::unique_event started(CreateEventW(&sa, FALSE, FALSE, CAMFX_EVENT_STARTED));
        wil::unique_event stopped(CreateEventW(&sa, FALSE, FALSE, CAMFX_EVENT_STOPPED));
        if (!started || !stopped) return;
        HANDLE handles[] = { started.get(), stopped.get(), g_quit.get() };
        for (;;)
        {
            DWORD r = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
            if (r == WAIT_OBJECT_0) PostMessageW(hwnd, WM_CALL, 1, 0);
            else if (r == WAIT_OBJECT_0 + 1) PostMessageW(hwnd, WM_CALL, 0, 0);
            else break;
        }
    }

    // ---- window / tray
    void ShowMain(bool autoShown)
    {
        g_autoShown = autoShown;
        ShowWindow(g_dlg, SW_SHOW);
        SetForegroundWindow(g_dlg);
        UpdatePreview();
    }

    void HideMain()
    {
        ShowWindow(g_dlg, SW_HIDE);
        g_autoShown = false;
        UpdatePreview();
    }

    void AddTrayIcon()
    {
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = g_dlg;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAY;
        g_nid.hIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_CAMFX), IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        wcscpy_s(g_nid.szTip, L"CamFX");
        Shell_NotifyIconW(NIM_ADD, &g_nid);
    }

    void TrayMenu()
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_OPEN, L"Open CamFX");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g_dlg);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_dlg, nullptr);
        DestroyMenu(menu);
    }

    void Exit()
    {
        StopPreview();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        DestroyWindow(g_dlg);
        PostQuitMessage(0);
    }

    INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == g_taskbarCreated && g_taskbarCreated)
        {
            Shell_NotifyIconW(NIM_ADD, &g_nid);
            return TRUE;
        }
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            g_dlg = hwnd;
            ApplyModernTheme(hwnd);
            HICON big = LoadIconW(g_inst, MAKEINTRESOURCEW(IDI_CAMFX));
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)big);
            for (auto s : { L"Off", L"Blur", L"Image" }) SendDlgItemMessageW(hwnd, IDC_BG, CB_ADDSTRING, 0, (LPARAM)s);
            for (auto s : { L"None", L"Mono", L"Warm", L"Cool", L"Vivid", L"Sepia", L"Fade" })
                SendDlgItemMessageW(hwnd, IDC_FILTER, CB_ADDSTRING, 0, (LPARAM)s);
            LoadControls(LoadSettings());
            CheckDlgButton(hwnd, IDC_PREVIEW_ON, AppGet(L"preview", L"1") == L"1");
            CheckDlgButton(hwnd, IDC_POPUP, AppGet(L"popup", L"1") == L"1");
            CheckDlgButton(hwnd, IDC_AUTOSTART, AutoStartEnabled());
            SetupCameras();
            return TRUE;
        }
        case WM_CTLCOLORDLG:
            return (INT_PTR)g_windowBrush;
        case WM_CTLCOLORSTATIC:
        {
            HDC dc = (HDC)wp;
            int id = GetDlgCtrlID((HWND)lp);
            SetBkMode(dc, TRANSPARENT);
            if (id == IDC_CAMERA_SECTION || id == IDC_FRAME_SECTION || id == IDC_LOOK_SECTION || id == IDC_SYSTEM_SECTION)
                SetTextColor(dc, RGB(37, 99, 235));
            else if (id == IDC_STATUS || id == IDC_CALL_HELP)
                SetTextColor(dc, RGB(71, 85, 105));
            else
                SetTextColor(dc, RGB(15, 23, 42));
            return (INT_PTR)g_windowBrush;
        }
        case WM_HSCROLL:
            OnChanged();
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case IDC_CAMERA:
                if (HIWORD(wp) == CBN_SELCHANGE)
                {
                    int sel = Combo(IDC_CAMERA);
                    if (sel >= 0 && sel < (int)g_camLinks.size())
                    {
                        StopPreview();
                        AppSet(L"camera", g_camLinks[sel]);
                        HRESULT hr = RegisterVirtualCamera(g_camLinks[sel]);
                        SetStatus(SUCCEEDED(hr) ? L"CamFX Camera ready" : L"Could not register CamFX Camera");
                        UpdatePreview();
                    }
                }
                return TRUE;
            case IDC_BG:
            case IDC_FILTER:
                if (HIWORD(wp) == CBN_SELCHANGE) OnChanged();
                return TRUE;
            case IDC_AUTOFRAME:
            case IDC_MIRROR:
            case IDC_LOWLIGHT:
                OnChanged();
                return TRUE;
            case IDC_BGIMAGE:
                ChooseImage();
                return TRUE;
            case IDC_RESET:
                LoadControls(FxSettings());
                OnChanged();
                return TRUE;
            case IDC_PREVIEW_ON:
                AppSet(L"preview", Checked(IDC_PREVIEW_ON) ? L"1" : L"0");
                if (Checked(IDC_PREVIEW_ON)) g_autoShown = false; // user wants to see it: no auto-hide
                UpdatePreview();
                return TRUE;
            case IDC_POPUP:
                AppSet(L"popup", Checked(IDC_POPUP) ? L"1" : L"0");
                return TRUE;
            case IDC_AUTOSTART:
                SetAutoStart(Checked(IDC_AUTOSTART));
                return TRUE;
            case IDM_OPEN:
                ShowMain(false);
                return TRUE;
            case IDM_EXIT:
                Exit();
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (wp == IDC_PREVIEW) { DrawPreview((const DRAWITEMSTRUCT*)lp); return TRUE; }
            break;
        case WM_FRAME:
            InvalidateRect(GetDlgItem(hwnd, IDC_PREVIEW), nullptr, FALSE);
            return TRUE;
        case WM_STATUS:
            SetStatus((const wchar_t*)lp);
            free((void*)lp);
            return TRUE;
        case WM_CALL:
            if (wp && Checked(IDC_POPUP) && !IsWindowVisible(hwnd)) ShowMain(true);
            else if (!wp && g_autoShown) HideMain();
            return TRUE;
        case WM_TRAY:
            if (LOWORD(lp) == WM_LBUTTONUP) IsWindowVisible(hwnd) ? HideMain() : ShowMain(false);
            else if (LOWORD(lp) == WM_RBUTTONUP) TrayMenu();
            return TRUE;
        case WM_CLOSE:
            HideMain();
            return TRUE;
        case WM_NCDESTROY:
            if (g_sectionFont) { DeleteObject(g_sectionFont); g_sectionFont = nullptr; }
            if (g_windowBrush) { DeleteObject(g_windowBrush); g_windowBrush = nullptr; }
            g_dlg = nullptr;
            break;
        }
        return FALSE;
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int)
{
    g_inst = inst;
    bool startInTray = wcsstr(cmdLine, L"--tray") != nullptr;
    bool remove = wcsstr(cmdLine, L"--remove") != nullptr;

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) || FAILED(MFStartup(MF_VERSION))) return 1;

    if (remove)
    {
        HRESULT hr = RemoveVirtualCamera();
        MFShutdown();
        return FAILED(hr) ? 1 : 0;
    }

    wil::unique_mutex single(CreateMutexW(nullptr, TRUE, L"Local\\CamFXSingleInstance"));
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND other = FindWindowW(L"#32770", L"CamFX")) PostMessageW(other, WM_COMMAND, IDM_OPEN, 0);
        return 0;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g_quit.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));

    if (!CreateDialogParamW(inst, MAKEINTRESOURCEW(IDD_MAIN), nullptr, DlgProc, 0)) return 1;
    AddTrayIcon();
    std::thread watcher(WatchCalls, g_dlg);
    if (!startInTray) ShowMain(false);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (!IsDialogMessageW(g_dlg, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    SetEvent(g_quit.get());
    watcher.join();
    g_vcam.reset();
    MFShutdown();
    return 0;
}
