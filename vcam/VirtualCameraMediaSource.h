//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once
#ifndef VIRTUALCAMERA_MEDIASOURCE_H
#define VIRTUALCAMERA_MEDIASOURCE_H

#include <initguid.h>

// {EE458BEA-0AE8-4A8B-8FBA-2AE2C4C1DBF2}
DEFINE_GUID(CLSID_VirtualCameraMediaSource ,
    0xee458bea, 0x0ae8, 0x4a8b, 0x8f, 0xba, 0x2a, 0xe2, 0xc4, 0xc1, 0xdb, 0xf2);

static LPCWSTR VIRTUALCAMERAMEDIASOURCE_CLSID = L"{EE458BEA-0AE8-4A8B-8FBA-2AE2C4C1DBF2}";
static LPCWSTR VIRTUALCAMERAMEDIASOURCE_FRIENDLYNAME = L"CamFX Camera";

// --> VirtualCameraMediaSource activation attributes:

// {3C31A5F8-2795-4FB9-A0A1-C733A65C0CE8}
// The value of this attribute is the physical camera symboliclink name that the VirtualCameraMediaSource
// will be using.
DEFINE_GUID(VCAM_DEVICE_INFO,
    0x3c31a5f8, 0x2795, 0x4fb9, 0xa0, 0xa1, 0xc7, 0x33, 0xa6, 0x5c, 0xc, 0xe8);

// <-- VirtualCameraMediaSource activation attributes

// Example custom control implemented by the AugmentedMediaSource
// 
// {0C4384E1-B457-43A7-B776-6D031DD88B12}
static const GUID PROPSETID_AUGMENTEDMEDIASOURCE_CUSTOMCONTROL =
{ 0xc4384e1, 0xb457, 0x43a7, { 0xb7, 0x76, 0x6d, 0x3, 0x1d, 0xd8, 0x8b, 0x12 } };

enum
{
    KSPROPERTY_AUGMENTEDMEDIASOURCE_CUSTOMCONTROL_CUSTOMFX = 0,
    KSPROPERTY_AUGMENTEDMEDIASOURCE_CUSTOMCONTROL_END  // all ids must be defined before this.
};

enum
{
    KSPROPERTY_AUGMENTEDMEDIASOURCE_CUSTOMCONTROL_CUSTOMFX_OFF = 0,
    KSPROPERTY_AUGMENTEDMEDIASOURCE_CUSTOMCONTROL_CUSTOMFX_AUTO = 1
};

typedef struct
{
    uint64_t Capability;
    uint64_t Flags;
    uint32_t Size;
} KSPROPERTY_AUGMENTEDMEDIASOURCE_CUSTOMCONTROL_FX, * PKSPROPERTY_AUGMENTEDMEDIASOURCE_CUSTOMCONTROL_FX;

#endif