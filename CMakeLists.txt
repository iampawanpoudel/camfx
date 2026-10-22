cmake_minimum_required(VERSION 3.21)
project(CamFX LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the media source loads inside Frame Server, so it must not depend on a VC++ redist being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE)
add_compile_options(/EHsc /W3 /bigobj /utf-8)

set(TP ${CMAKE_SOURCE_DIR}/third_party)
if(NOT EXISTS ${TP}/onnxruntime OR NOT EXISTS ${TP}/wil)
  message(FATAL_ERROR "Run scripts/fetch-deps.ps1 first")
endif()

# Virtual camera media source (loaded by Windows Frame Server)
add_library(CamFXCamera SHARED
  vcam/AugmentedMediaSource.cpp
  vcam/AugmentedMediaStream.cpp
  vcam/dllmain.cpp
  vcam/pch.cpp
  vcam/VirtualCameraMediaSourceActivate.cpp
  vcam/winrtCommon.cpp
  vcam/Effects.cpp
  vcam/Segmenter.cpp
  vcam/Settings.cpp
  vcam/VirtualCameraMediaSource.def)
target_include_directories(CamFXCamera PRIVATE vcam ${TP}/wil/include ${TP}/onnxruntime/include)
target_link_libraries(CamFXCamera PRIVATE mfplat mf mfuuid mfsensorgroup windowsapp ole32 shlwapi)
add_custom_command(TARGET CamFXCamera POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TP}/onnxruntime/lib/onnxruntime.dll $<TARGET_FILE_DIR:CamFXCamera>
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/models/person_segmenter.onnx $<TARGET_FILE_DIR:CamFXCamera>)

# Tray / settings app
add_executable(CamFX WIN32 app/main.cpp app/app.rc vcam/Settings.cpp)
target_include_directories(CamFX PRIVATE vcam ${TP}/wil/include)
target_link_options(CamFX PRIVATE /MANIFEST:NO) # app.rc embeds app.manifest

# Effects unit tests (no camera needed)
enable_testing()
add_executable(camfx_tests tests/effects_test.cpp vcam/Effects.cpp vcam/Segmenter.cpp)
target_include_directories(camfx_tests PRIVATE vcam ${TP}/onnxruntime/include)
add_dependencies(camfx_tests CamFXCamera) # needs onnxruntime.dll + model copied next to it
add_test(NAME effects COMMAND camfx_tests $<TARGET_FILE_DIR:CamFXCamera>)

# Diagnostic: grab frames from "CamFX Camera" like a call app does
add_executable(camfx_grab tests/grab.cpp)
target_include_directories(camfx_grab PRIVATE ${TP}/wil/include)
