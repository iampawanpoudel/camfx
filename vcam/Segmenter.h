// Person segmentation with ONNX Runtime (CPU, 1 thread) and the PP-HumanSegV2-Lite portrait model.
#pragma once
#include <memory>
#include <string>

class Segmenter
{
public:
    static constexpr int kW = 256, kH = 144; // model input and mask size (16:9, like webcams)

    Segmenter();
    ~Segmenter();
    // Loads <dir>\onnxruntime.dll by full path and <dir>\person_segmenter.onnx.
    bool Init(const std::wstring& dir);
    // rgb: planar 3 x kH x kW floats 0..1. mask: kH x kW person alpha 0..1.
    bool Run(const float* rgb, float* mask);

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
