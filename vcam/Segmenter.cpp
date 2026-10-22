#include "Segmenter.h"
#include <windows.h>
#include <algorithm>
#include <vector>
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

// onnxruntime.dll is loaded from our own folder by full path and resolved with GetProcAddress:
// Windows 11 ships an older onnxruntime.dll in System32 that a by-name load could pick up instead.
struct Segmenter::Impl
{
    HMODULE dll = nullptr;
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
    std::string inName, outName;
    std::unique_ptr<Ort::MemoryInfo> mem; // Ort objects may only exist after Ort::InitApi
    std::vector<float> in, out;
};

Segmenter::Segmenter() : m(std::make_unique<Impl>()) {}

Segmenter::~Segmenter()
{
    m->mem.reset();
    m->session.reset();
    m->env.reset();
    // dll intentionally stays loaded: ORT keeps thread-local state that outlives the session.
}

bool Segmenter::Init(const std::wstring& dir)
{
    try
    {
        m->dll = LoadLibraryExW((dir + L"\\onnxruntime.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!m->dll) return false;
        using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)();
        auto getApiBase = (GetApiBaseFn)GetProcAddress(m->dll, "OrtGetApiBase");
        if (!getApiBase) return false;
        const OrtApi* api = getApiBase()->GetApi(ORT_API_VERSION);
        if (!api) return false;
        Ort::InitApi(api);

        m->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "camfx");
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetInterOpNumThreads(1);
        opts.SetExecutionMode(ORT_SEQUENTIAL);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m->session = std::make_unique<Ort::Session>(*m->env, (dir + L"\\person_segmenter.onnx").c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        m->inName = m->session->GetInputNameAllocated(0, alloc).get();
        m->outName = m->session->GetOutputNameAllocated(0, alloc).get();
        m->mem = std::make_unique<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
        return true;
    }
    catch (const Ort::Exception& e)
    {
        OutputDebugStringA("CamFX: segmenter init failed: ");
        OutputDebugStringA(e.what());
        m->session.reset();
        return false;
    }
}

bool Segmenter::Run(const float* rgb, float* mask)
{
    if (!m->session || !m->mem) return false;
    try
    {
        const int plane = kW * kH;
        m->in.resize(3 * plane);
        m->out.resize(2 * plane);
        for (int i = 0; i < 3 * plane; i++) m->in[i] = rgb[i] * 2.f - 1.f; // model expects (x - 0.5) / 0.5
        const int64_t inShape[] = { 1, 3, kH, kW }, outShape[] = { 1, 2, kH, kW };
        Ort::Value in = Ort::Value::CreateTensor<float>(*m->mem, m->in.data(), m->in.size(), inShape, 4);
        Ort::Value out = Ort::Value::CreateTensor<float>(*m->mem, m->out.data(), m->out.size(), outShape, 4);
        const char* inNames[] = { m->inName.c_str() };
        const char* outNames[] = { m->outName.c_str() };
        m->session->Run(Ort::RunOptions{ nullptr }, inNames, &in, 1, outNames, &out, 1);
        std::copy(m->out.begin() + plane, m->out.end(), mask); // softmax planes: background, person
        return true;
    }
    catch (const Ort::Exception& e)
    {
        OutputDebugStringA("CamFX: segmenter run failed: ");
        OutputDebugStringA(e.what());
        return false;
    }
}
