// Diagnostic: open a camera like a call app does, report fps, save the last frame as BMP.
// usage: camfx_grab [out.bmp] [frames] [camera name, default "CamFX Camera"]
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wil/com.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mf")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* out = argc > 1 ? argv[1] : L"grab.bmp";
    int frames = argc > 2 ? _wtoi(argv[2]) : 90;
    std::wstring want = argc > 3 ? argv[3] : L"CamFX Camera";

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    wil::com_ptr_nothrow<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 1);
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attrs.get(), &devs, &count))) return 2;
    wil::com_ptr_nothrow<IMFActivate> chosen;
    for (UINT32 i = 0; i < count; i++)
    {
        wil::unique_cotaskmem_string name;
        UINT32 cch = 0;
        devs[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &cch);
        wprintf(L"camera: %s\n", name.get());
        if (std::wstring(name.get()).rfind(want, 0) == 0) chosen = devs[i]; // prefix: Windows appends a suffix
        devs[i]->Release();
    }
    CoTaskMemFree(devs);
    if (!chosen) { wprintf(L"'%s' not found\n", want.c_str()); return 3; }

    wil::com_ptr_nothrow<IMFMediaSource> source;
    HRESULT hr = chosen->ActivateObject(IID_PPV_ARGS(&source));
    if (FAILED(hr)) { wprintf(L"activate failed 0x%08X\n", hr); return 4; }

    wil::com_ptr_nothrow<IMFSourceReader> reader;
    wil::com_ptr_nothrow<IMFMediaType> type;
    attrs.reset();
    MFCreateAttributes(&attrs, 1);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    hr = MFCreateSourceReaderFromMediaSource(source.get(), attrs.get(), &reader);
    if (FAILED(hr)) { wprintf(L"reader failed 0x%08X\n", hr); return 5; }
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.get());
    if (FAILED(hr)) { wprintf(L"set type failed 0x%08X\n", hr); return 6; }
    type.reset();
    reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
    UINT32 w = 0, h = 0;
    MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &w, &h);
    wprintf(L"format %ux%u\n", w, h);

    std::vector<uint8_t> px((size_t)w * h * 4);
    LONGLONG first = 0, last = 0;
    int got = 0;
    while (got < frames)
    {
        DWORD stream, flags;
        LONGLONG ts;
        wil::com_ptr_nothrow<IMFSample> sample;
        hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) { wprintf(L"read failed 0x%08X flags 0x%X\n", hr, flags); return 7; }
        if (!sample) continue;
        if (got == 0) first = ts;
        last = ts;
        got++;
        wil::com_ptr_nothrow<IMFMediaBuffer> buf;
        wil::com_ptr_nothrow<IMF2DBuffer> b2;
        sample->GetBufferByIndex(0, &buf);
        BYTE* scan0;
        LONG pitch;
        if (SUCCEEDED(buf.query_to(&b2)) && SUCCEEDED(b2->Lock2D(&scan0, &pitch)))
        {
            for (UINT32 r = 0; r < h; r++) memcpy(&px[(size_t)r * w * 4], scan0 + (ptrdiff_t)r * pitch, (size_t)w * 4);
            b2->Unlock2D();
        }
        else
        {
            DWORD len = 0;
            if (SUCCEEDED(buf->Lock(&scan0, nullptr, &len)))
            {
                memcpy(px.data(), scan0, std::min<size_t>(len, px.size()));
                buf->Unlock();
            }
        }
    }
    double secs = (last - first) / 1e7;
    double luma = 0;
    for (size_t i = 0; i < px.size(); i += 4) luma += 0.114 * px[i] + 0.587 * px[i + 1] + 0.299 * px[i + 2];
    wprintf(L"%d frames, %.1f fps, mean luma %.1f\n", got, secs > 0 ? (got - 1) / secs : 0.0, luma / (w * h));
    reader.reset();
    source->Shutdown();

    BITMAPFILEHEADER fh = { 0x4D42 };
    BITMAPINFOHEADER ih = { sizeof(ih), (LONG)w, -(LONG)h, 1, 32 };
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + (DWORD)px.size();
    FILE* f = _wfopen(out, L"wb");
    if (!f) return 8;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    wprintf(L"saved %s\n", out);
    return 0;
}
