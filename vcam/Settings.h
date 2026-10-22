// Settings shared between CamFX.exe and the media source through %ProgramData%\CamFX.
// Frame Server runs the media source as LocalService, which can't read the user's HKCU or profile.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "Effects.h"

#define CAMFX_EVENT_STARTED L"Global\\CamFXStreamStarted"
#define CAMFX_EVENT_STOPPED L"Global\\CamFXStreamStopped"

std::wstring CamFxDataDir();                 // %ProgramData%\CamFX (created if missing)
std::wstring CamFxSettingsPath();            // ...\settings.ini
std::wstring CamFxBackgroundPath();          // ...\background.img (any WIC format)

FxSettings LoadSettings();
void SaveSettings(const FxSettings& s);

// True when the file's last-write time differs from *stamp (and updates it).
bool FileChanged(const std::wstring& path, FILETIME* stamp);

// Decode any WIC-supported image to 8-bit BGRA.
bool LoadImageBgra(const std::wstring& path, std::vector<uint8_t>& bgra, int& w, int& h);

void SignalNamedEvent(const wchar_t* name);
