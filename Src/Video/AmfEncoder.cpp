#include "AmfEncoder.h"
#include <GL/glew.h>
#include <cstdio>
#include <cstring>

typedef AMF_RESULT(AMF_CDECL_CALL *AMFInit_Fn)(amf_uint64 version, amf::AMFFactory** ppFactory);

AmfEncoder::AmfEncoder()
{
}

bool AmfEncoder::LoadAmfDll()
{
    m_amfDll = LoadLibraryA("amf-rt-x64.dll");
    if (!m_amfDll)
    {
        printf("[AMF] Failed to load amf-rt-x64.dll\n");
        return false;
    }

    auto pInit = (AMFInit_Fn)GetProcAddress(m_amfDll, AMF_INIT_FUNCTION_NAME);
    if (!pInit)
    {
        printf("[AMF] AMFInit function not found in DLL\n");
        return false;
    }

    AMF_RESULT res = pInit(AMF_FULL_VERSION, &m_pFactory);
    if (res != AMF_OK)
    {
        printf("[AMF] AMFInit failed: %d\n", res);
        return false;
    }

    printf("[AMF] AMF runtime DLL loaded OK\n");
    return true;
}

bool AmfEncoder::Init(int width, int height, int fps, int port, const std::string &codec, OnEncodedCallback cb)
{
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_codec = codec;
    m_callback = cb;

    m_hostBuffer.resize(width * height * 4);

    if (!LoadAmfDll())
        return false;

    AMF_RESULT res = m_pFactory->CreateContext(&m_pContext);
    if (res != AMF_OK)
    {
        printf("[AMF] CreateContext failed: %d\n", res);
        return false;
    }

    bool useH265 = (codec != "H264");
    const wchar_t* encoderId = useH265 ? AMFVideoEncoder_HEVC : AMFVideoEncoderVCE_AVC;

    res = m_pFactory->CreateComponent(m_pContext, encoderId, &m_pEncoder);
    if (res != AMF_OK)
    {
        printf("[AMF] CreateComponent for encoder failed: %d\n", res);
        return false;
    }

    // Configure properties for ultra-low latency
    if (useH265)
    {
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_ULTRA_LOW_LATENCY);
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, amf_int64(3000000));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, amf_int64(4000000));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMESIZE, ::AMFConstructSize(width, height));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(fps, 1));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_GOP_SIZE, amf_int64(fps));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_MAX_NUM_REFRAMES, amf_int64(1)); // No B-frames, single reference frame
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_HEVC_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR);
    }
    else
    {
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY);
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, amf_int64(3000000));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, amf_int64(4000000));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMESIZE, ::AMFConstructSize(width, height));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(fps, 1));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_IDR_PERIOD, amf_int64(fps));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, amf_int64(0)); // No B-frames
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_MAX_NUM_REFRAMES, amf_int64(1));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD, AMF_VIDEO_ENCODER_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR);
    }

    res = m_pEncoder->Init(amf::AMF_SURFACE_RGBA, width, height);
    if (res != AMF_OK)
    {
        printf("[AMF] Encoder Init failed: %d\n", res);
        return false;
    }

    m_rtpEnabled = m_rtpSender.Init("127.0.0.1", port, useH265);

    printf("[AMF] Encoder initialized successfully (%s) %dx%d @%dfps\n", useH265 ? "H265" : "H264", width, height, fps);
    return true;
}

void AmfEncoder::EncodeFrame(unsigned int glTextureID)
{
    if (!m_pEncoder)
        return;

    // 1. Readback OpenGL texture to system memory (RGBA)
    glBindTexture(GL_TEXTURE_2D, glTextureID);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_hostBuffer.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    // 2. Allocate AMF Host surface
    amf::AMFSurfacePtr pSurface;
    AMF_RESULT res = m_pContext->AllocSurface(amf::AMF_MEMORY_HOST, amf::AMF_SURFACE_RGBA, m_width, m_height, &pSurface);
    if (res != AMF_OK)
        return;

    // 3. Copy pixels from host buffer to AMF surface plane
    amf::AMFPlane* pPlane = pSurface->GetPlaneAt(0);
    uint8_t* dst = (uint8_t*)pPlane->GetNative();
    int pitch = pPlane->GetHPitch();
    const uint8_t* src = m_hostBuffer.data();
    for (int y = 0; y < m_height; y++)
    {
        memcpy(dst + y * pitch, src + y * m_width * 4, m_width * 4);
    }

    // 4. Submit input surface to encoder
    res = m_pEncoder->SubmitInput(pSurface);
    if (res != AMF_OK)
    {
        printf("[AMF] SubmitInput failed: %d\n", res);
        return;
    }

    // 5. Query output bitstreams
    ProcessOutput();
}

void AmfEncoder::ProcessOutput()
{
    while (true)
    {
        amf::AMFDataPtr pData;
        AMF_RESULT res = m_pEncoder->QueryOutput(&pData);
        if (res == AMF_REPEAT)
        {
            // Need more inputs or processing is busy, try again later
            break;
        }
        if (res == AMF_EOF || res != AMF_OK)
        {
            break;
        }
        if (pData != nullptr)
        {
            amf::AMFBufferPtr pBuffer(pData);
            const uint8_t* pMem = (const uint8_t*)pBuffer->GetNative();
            size_t size = pBuffer->GetSize();

            // Detect keyframe using NAL header check (identical to NVENC logic)
            bool isKeyframe = false;
            size_t offset = 0;
            while (offset < size - 4)
            {
                if (pMem[offset] == 0 && pMem[offset + 1] == 0 &&
                    pMem[offset + 2] == 0 && pMem[offset + 3] == 1)
                {
                    int nalType = pMem[offset + 4] & 0x1F;
                    if (nalType == 5 || nalType == 7 || nalType == 8) // IDR, SPS, PPS
                    {
                        isKeyframe = true;
                        break;
                    }
                    offset += 4;
                }
                else
                {
                    offset++;
                }
                if (offset > 100)
                    break;
            }

            if (m_callback)
            {
                m_callback(pMem, size, isKeyframe);
            }

            if (m_rtpEnabled)
            {
                m_rtpSender.Send(pMem, (int)size);
            }
        }
    }
}

bool AmfEncoder::ReconfigureBitrate(int avgBitrate, int maxBitrate)
{
    if (!m_pEncoder)
        return false;

    // Update bitrate properties dynamically
    bool useH265 = (m_codec != "H264");
    AMF_RESULT res;
    if (useH265)
    {
        res = m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, amf_int64(avgBitrate));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PEAK_BITRATE, amf_int64(maxBitrate));
    }
    else
    {
        res = m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, amf_int64(avgBitrate));
        m_pEncoder->SetProperty(AMF_VIDEO_ENCODER_PEAK_BITRATE, amf_int64(maxBitrate));
    }

    if (res != AMF_OK)
    {
        printf("[AMF] Failed to reconfigure bitrate dynamically: %d\n", res);
        return false;
    }

    printf("[AMF] Reconfigured bitrate dynamically: avg=%d, max=%d bps\n", avgBitrate, maxBitrate);
    return true;
}

void AmfEncoder::Shutdown()
{
    m_pEncoder = nullptr;
    m_pContext = nullptr;
    m_pFactory = nullptr;

    if (m_amfDll)
    {
        FreeLibrary(m_amfDll);
        m_amfDll = nullptr;
    }

    printf("[AMF] Shutdown complete\n");
}

void AmfEncoder::SetDestIP(const std::string &ip)
{
    m_rtpSender.SetDestIP(ip);
    printf("[AMF] IP changed to %s\n", ip.c_str());
}

void AmfEncoder::SetDestIPs(const std::vector<std::string> &ips)
{
    m_rtpSender.SetDestIPs(ips);
}

void AmfEncoder::SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints)
{
    m_rtpSender.SetDestEndpoints(endpoints);
}
