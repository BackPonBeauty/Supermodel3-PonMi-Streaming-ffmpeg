#pragma once

#ifdef CreateSemaphore
#undef CreateSemaphore
#endif
#ifdef CreateMutex
#undef CreateMutex
#endif

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <cuda.h>
#include <cuda_gl_interop.h>
#include <utility>
#include "nvEncodeAPI.h"
#include "Network/RtpSender.h" // Added
#include "IEncoder.h"

#ifdef CreateSemaphore
#undef CreateSemaphore
#endif
#ifdef CreateMutex
#undef CreateMutex
#endif

class NvencEncoder : public IEncoder
{
public:
    NvencEncoder() = default;
    virtual ~NvencEncoder() { Shutdown(); }

    
    virtual bool Init(int width, int height, int fps, int port, const std::string &codec, OnEncodedCallback cb) override;
    virtual bool ReconfigureBitrate(int avgBitrate, int maxBitrate) override;
    virtual void EncodeFrame(unsigned int glTextureID) override;
    virtual void Shutdown() override;
    virtual void SetDestIP(const std::string &ip) override;
    virtual void SetDestIPs(const std::vector<std::string> &ips) override;
    virtual void SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints) override;
    void SetDestPort(int port) { m_rtpSender.SetDestPort(port); }
    virtual int GetWidth() const override { return m_width; }
    virtual int GetHeight() const override { return m_height; }
   

private:
    bool LoadNvencDll();
    bool InitCuda();
    bool CreateEncoder();
    bool AllocateBuffers();

    void ProcessOutput(int idx);

    NV_ENCODE_API_FUNCTION_LIST m_nvenc = {};
    void *m_encoder = nullptr;
    CUcontext m_cuContext = nullptr;

    static constexpr int BUFFER_COUNT = 3;

    cudaGraphicsResource_t m_cuResource = nullptr;
    unsigned int m_glTexID = 0;

    NV_ENC_INPUT_PTR m_inputBuffers[BUFFER_COUNT] = {};
    NV_ENC_OUTPUT_PTR m_outputBuffers[BUFFER_COUNT] = {};
    int m_bufferIndex = 0;

    int m_width = 0;
    int m_height = 0;
    int m_fps = 60;

    HMODULE m_nvencDll = nullptr;
    OnEncodedCallback m_callback;

    // RTP transmission
    RtpSender m_rtpSender;
    bool m_rtpEnabled = false;
    std::string m_codec;
};