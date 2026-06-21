#include "NvencEncoder.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

typedef NVENCSTATUS(NVENCAPI *PFN_NvEncodeAPICreateInstance)(NV_ENCODE_API_FUNCTION_LIST *);

// ============================================================
bool NvencEncoder::LoadNvencDll()
{
    m_nvencDll = LoadLibraryA("nvEncodeAPI64.dll");
    if (!m_nvencDll)
    {
        printf("[NVENC] Failed to load nvEncodeAPI64.dll\n");
        return false;
    }

    auto createInstance = (PFN_NvEncodeAPICreateInstance)
        GetProcAddress(m_nvencDll, "NvEncodeAPICreateInstance");
    if (!createInstance)
    {
        printf("[NVENC] NvEncodeAPICreateInstance not found\n");
        return false;
    }

    m_nvenc.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    NVENCSTATUS st = createInstance(&m_nvenc);
    if (st != NV_ENC_SUCCESS)
    {
        printf("[NVENC] NvEncodeAPICreateInstance failed: %d\n", st);
        return false;
    }

    printf("[NVENC] nvEncodeAPI64.dll loaded OK\n");
    return true;
}

// ============================================================
bool NvencEncoder::InitCuda()
{
    CUresult res = cuInit(0);
    if (res != CUDA_SUCCESS)
    {
        printf("[NVENC] cuInit failed: %d\n", res);
        return false;
    }

    CUdevice device;
    res = cuDeviceGet(&device, 0);
    if (res != CUDA_SUCCESS)
    {
        printf("[NVENC] cuDeviceGet failed: %d\n", res);
        return false;
    }

    // CUDA 13.1 is now cuCtxCreate_v4, so explicitly use the older API
    CUctxCreateParams params = {};
    params.execAffinityParams = nullptr;
    res = cuCtxCreate(&m_cuContext, &params, 0, device);
    if (res != CUDA_SUCCESS)
    {
        printf("[NVENC] cuCtxCreate failed: %d\n", res);
        return false;
    }

    printf("[NVENC] CUDA context created\n");
    return true;
}

// ============================================================
bool NvencEncoder::CreateEncoder()
{
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sessionParams = {};
    sessionParams.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    sessionParams.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    sessionParams.device = m_cuContext;
    sessionParams.apiVersion = NVENCAPI_VERSION;

    NVENCSTATUS st = m_nvenc.nvEncOpenEncodeSessionEx(&sessionParams, &m_encoder);
    if (st != NV_ENC_SUCCESS)
    {
        printf("[NVENC] nvEncOpenEncodeSessionEx failed: %d\n", st);
        return false;
    }

    bool useH265 = (m_codec != "H264");
    GUID codecGuid = useH265 ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;

    // Retrieve preset configurations
    NV_ENC_PRESET_CONFIG presetConfig = {};
    presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
    m_nvenc.nvEncGetEncodePresetConfigEx(
        m_encoder,
        codecGuid,
        NV_ENC_PRESET_P1_GUID,
        NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
        &presetConfig);

    NV_ENC_CONFIG encConfig = presetConfig.presetCfg;

    // Low-latency VBR configuration
/*
    encConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
    encConfig.rcParams.averageBitRate = 1500000; // 1.5Mbps
    encConfig.rcParams.maxBitRate = 3000000;
    encConfig.rcParams.vbvBufferSize = 0;
    encConfig.rcParams.vbvInitialDelay = 0;
    encConfig.frameIntervalP = 1; // No B-frames
    encConfig.gopLength = m_fps;
*/
    // Low-latency VBR configuration
    encConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
    encConfig.rcParams.averageBitRate = 3000000; // 3Mbps (実測430Kbpsの余裕あり)
    encConfig.rcParams.maxBitRate = 4000000;     // 3Mbps (動きの激しい場面用)
    encConfig.rcParams.vbvBufferSize = 3000000;  // averageと同値推奨
    encConfig.rcParams.vbvInitialDelay = 1500000;
    encConfig.frameIntervalP = 1;    // No B-frames (低遅延維持)
    encConfig.gopLength = m_fps;

    // 追加推奨
    encConfig.rcParams.enableAQ = 1;         // Adaptive Quantization ON (静止部分を綺麗に)
    encConfig.rcParams.aqStrength = 8;       // 1-15、強めが画質良い
    encConfig.rcParams.enableTemporalAQ = 1; // 時間方向AQも有効
    if (useH265)
    {
        encConfig.encodeCodecConfig.hevcConfig.idrPeriod = m_fps;
        encConfig.encodeCodecConfig.hevcConfig.sliceMode = 0;
        encConfig.encodeCodecConfig.hevcConfig.sliceModeData = 0;
    }
    else
    {
        encConfig.encodeCodecConfig.h264Config.idrPeriod = m_fps;
        encConfig.encodeCodecConfig.h264Config.sliceMode = 0;
        encConfig.encodeCodecConfig.h264Config.sliceModeData = 0;
    }

    NV_ENC_INITIALIZE_PARAMS initParams = {};
    initParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    initParams.encodeGUID = codecGuid;
    initParams.presetGUID = NV_ENC_PRESET_P1_GUID;
    initParams.encodeWidth = m_width;
    initParams.encodeHeight = m_height;
    initParams.darWidth = m_width;
    initParams.darHeight = m_height;
    initParams.frameRateNum = m_fps;
    initParams.frameRateDen = 1;
    initParams.enablePTD = 1;
    initParams.encodeConfig = &encConfig;
    initParams.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;

    st = m_nvenc.nvEncInitializeEncoder(m_encoder, &initParams);
    if (st != NV_ENC_SUCCESS)
    {
        printf("[NVENC] nvEncInitializeEncoder failed: %d\n", st);
        return false;
    }

    printf("[NVENC] Encoder initialized (%s) %dx%d @%dfps\n", useH265 ? "H265" : "H264", m_width, m_height, m_fps);
    return true;
}

// ============================================================
bool NvencEncoder::AllocateBuffers()
{
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        // Input buffer (CUDA memory)
        NV_ENC_CREATE_INPUT_BUFFER inputBuf = {};
        inputBuf.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
        inputBuf.width = m_width;
        inputBuf.height = m_height;
        inputBuf.bufferFmt = NV_ENC_BUFFER_FORMAT_ABGR; // RGBA

        NVENCSTATUS st = m_nvenc.nvEncCreateInputBuffer(m_encoder, &inputBuf);
        if (st != NV_ENC_SUCCESS)
        {
            printf("[NVENC] nvEncCreateInputBuffer[%d] failed: %d\n", i, st);
            return false;
        }
        m_inputBuffers[i] = inputBuf.inputBuffer;

        // Output buffer
        NV_ENC_CREATE_BITSTREAM_BUFFER outputBuf = {};
        outputBuf.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

        st = m_nvenc.nvEncCreateBitstreamBuffer(m_encoder, &outputBuf);
        if (st != NV_ENC_SUCCESS)
        {
            printf("[NVENC] nvEncCreateBitstreamBuffer[%d] failed: %d\n", i, st);
            return false;
        }
        m_outputBuffers[i] = outputBuf.bitstreamBuffer;
    }

    printf("[NVENC] Buffers allocated (%d)\n", BUFFER_COUNT);
    return true;
}

// ============================================================
bool NvencEncoder::Init(int width, int height, int fps, int port, const std::string &codec, OnEncodedCallback cb)
{
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_callback = cb;
    m_codec = codec;

    if (!LoadNvencDll())
        return false;
    if (!InitCuda())
        return false;
    if (!CreateEncoder())
        return false;
    if (!AllocateBuffers())
        return false;

    m_rtpEnabled = m_rtpSender.Init("127.0.0.1", port, (codec != "H264"));

    printf("[NVENC] Init complete\n");
    return true;
}

// ============================================================
void NvencEncoder::EncodeFrame(unsigned int glTextureID)
{
    if (!m_encoder)
        return;

    int idx = m_bufferIndex % BUFFER_COUNT;

    // Register OpenGL texture to CUDA (first time or on texture change)
    if (m_glTexID != glTextureID)
    {
        if (m_cuResource)
        {
            cudaGraphicsUnregisterResource(m_cuResource);
            m_cuResource = nullptr;
        }
        cudaError_t err = cudaGraphicsGLRegisterImage(
            &m_cuResource,
            glTextureID,
            GL_TEXTURE_2D,
            cudaGraphicsRegisterFlagsReadOnly);
        if (err != cudaSuccess)
        {
            printf("[NVENC] cudaGraphicsGLRegisterImage failed: %d\n", err);
            return;
        }
        m_glTexID = glTextureID;
    }

    // Map OpenGL texture to CUDA
    cudaGraphicsMapResources(1, &m_cuResource, 0);

    cudaArray_t cuArray;
    cudaGraphicsSubResourceGetMappedArray(&cuArray, m_cuResource, 0, 0);

    // Lock NVENC input buffer
    NV_ENC_LOCK_INPUT_BUFFER lockParams = {};
    lockParams.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    lockParams.inputBuffer = m_inputBuffers[idx];

    NVENCSTATUS st = m_nvenc.nvEncLockInputBuffer(m_encoder, &lockParams);
    if (st != NV_ENC_SUCCESS)
    {
        cudaGraphicsUnmapResources(1, &m_cuResource, 0);
        return;
    }

    // Copy from CUDAArray to NVENC input buffer on GPU
    cudaMemcpy2DFromArray(
        lockParams.bufferDataPtr,  // dst
        lockParams.pitch,          // dst pitch
        cuArray,                   // src
        0, 0,                      // src offset
        m_width * 4,               // width bytes (RGBA)
        m_height,                  // height
        cudaMemcpyDeviceToDevice); // GPU -> GPU

    m_nvenc.nvEncUnlockInputBuffer(m_encoder, m_inputBuffers[idx]);
    cudaGraphicsUnmapResources(1, &m_cuResource, 0);

    // Encode
    NV_ENC_PIC_PARAMS picParams = {};
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.inputBuffer = m_inputBuffers[idx];
    picParams.outputBitstream = m_outputBuffers[idx];
    picParams.bufferFmt = NV_ENC_BUFFER_FORMAT_ABGR;
    picParams.inputWidth = m_width;
    picParams.inputHeight = m_height;
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

    // Force IDR frame for the first few frames
    if (m_bufferIndex < 3)
        picParams.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    else
        picParams.encodePicFlags = NV_ENC_PIC_FLAG_OUTPUT_SPSPPS; // Attach SPS/PPS to every frame

    m_nvenc.nvEncEncodePicture(m_encoder, &picParams);

    ProcessOutput(idx);

    m_bufferIndex++;
}

// ============================================================
void NvencEncoder::ProcessOutput(int idx)
{
    NV_ENC_LOCK_BITSTREAM lockBitstream = {};
    lockBitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
    lockBitstream.outputBitstream = m_outputBuffers[idx];
    lockBitstream.doNotWait = 0;

    NVENCSTATUS st = m_nvenc.nvEncLockBitstream(m_encoder, &lockBitstream);
    if (st == NV_ENC_SUCCESS)
    {
        const uint8_t *data = (const uint8_t *)lockBitstream.bitstreamBufferPtr;
        int size = (int)lockBitstream.bitstreamSizeInBytes;

        // Inspect only around the head to find leading NAL types
        int offset = 0;
        while (offset < size - 4)
        {
            if (data[offset] == 0 && data[offset + 1] == 0 &&
                data[offset + 2] == 0 && data[offset + 3] == 1)
            {
                int nalType = data[offset + 4] & 0x1F;
                // printf("[NVENC] NAL type=%d at offset=%d\n", nalType, offset);
                offset += 4;
            }
            else
                offset++;
            if (offset > 100)
                break; // Inspect only near the head
        }
        bool isKeyframe = (lockBitstream.pictureType == NV_ENC_PIC_TYPE_IDR || lockBitstream.pictureType == NV_ENC_PIC_TYPE_I);
        if (m_callback)
            m_callback(
                (const uint8_t *)lockBitstream.bitstreamBufferPtr,
                (size_t)lockBitstream.bitstreamSizeInBytes,
                isKeyframe);

        // RTP transmission
        if (m_rtpEnabled)
            m_rtpSender.Send(
                (const uint8_t *)lockBitstream.bitstreamBufferPtr,
                (int)lockBitstream.bitstreamSizeInBytes);

        m_nvenc.nvEncUnlockBitstream(m_encoder, m_outputBuffers[idx]);
    }
}

// ============================================================
void NvencEncoder::Shutdown()
{
    if (!m_encoder)
        return;

    if (m_cuResource)
    {
        cudaGraphicsUnregisterResource(m_cuResource);
        m_cuResource = nullptr;
    }

    // Flush
    NV_ENC_PIC_PARAMS flushParams = {};
    flushParams.version = NV_ENC_PIC_PARAMS_VER;
    flushParams.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    m_nvenc.nvEncEncodePicture(m_encoder, &flushParams);

    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        if (m_inputBuffers[i])
            m_nvenc.nvEncDestroyInputBuffer(m_encoder, m_inputBuffers[i]);
        if (m_outputBuffers[i])
            m_nvenc.nvEncDestroyBitstreamBuffer(m_encoder, m_outputBuffers[i]);
    }

    m_nvenc.nvEncDestroyEncoder(m_encoder);
    m_encoder = nullptr;

    if (m_nvencDll)
    {
        FreeLibrary(m_nvencDll);
        m_nvencDll = nullptr;
    }

    printf("[NVENC] Shutdown complete\n");
}
void NvencEncoder::SetDestIP(const std::string &ip)
{
    m_rtpSender.SetDestIP(ip);
    printf("[NVENC] IP changed to %s\n", ip.c_str());
}

void NvencEncoder::SetDestIPs(const std::vector<std::string> &ips)
{
    m_rtpSender.SetDestIPs(ips);
}

void NvencEncoder::SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints)
{
    m_rtpSender.SetDestEndpoints(endpoints);
}

bool NvencEncoder::ReconfigureBitrate(int avgBitrate, int maxBitrate)
{
    if (!m_encoder)
        return false;

    NV_ENC_RECONFIGURE_PARAMS reconfigParams = {};
    reconfigParams.version = NV_ENC_RECONFIGURE_PARAMS_VER;
    reconfigParams.reInitEncodeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;

    bool useH265 = (m_codec != "H264");
    GUID codecGuid = useH265 ? NV_ENC_CODEC_HEVC_GUID : NV_ENC_CODEC_H264_GUID;

    NV_ENC_PRESET_CONFIG presetConfig = {};
    presetConfig.version = NV_ENC_PRESET_CONFIG_VER;
    presetConfig.presetCfg.version = NV_ENC_CONFIG_VER;
    NVENCSTATUS st = m_nvenc.nvEncGetEncodePresetConfigEx(
        m_encoder,
        codecGuid,
        NV_ENC_PRESET_P1_GUID,
        NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
        &presetConfig);
    if (st != NV_ENC_SUCCESS)
    {
        printf("[NVENC] nvEncGetEncodePresetConfigEx failed in reconfigure: %d\n", st);
        return false;
    }

    NV_ENC_CONFIG encConfig = presetConfig.presetCfg;
    encConfig.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
    encConfig.rcParams.averageBitRate = avgBitrate;
    encConfig.rcParams.maxBitRate = maxBitrate;
    encConfig.rcParams.vbvBufferSize = avgBitrate;
    encConfig.rcParams.vbvInitialDelay = avgBitrate / 2;
    encConfig.frameIntervalP = 1;
    encConfig.gopLength = m_fps;

    encConfig.rcParams.enableAQ = 1;
    encConfig.rcParams.aqStrength = 8;
    encConfig.rcParams.enableTemporalAQ = 1;
    if (useH265)
    {
        encConfig.encodeCodecConfig.hevcConfig.idrPeriod = m_fps;
        encConfig.encodeCodecConfig.hevcConfig.sliceMode = 0;
        encConfig.encodeCodecConfig.hevcConfig.sliceModeData = 0;
    }
    else
    {
        encConfig.encodeCodecConfig.h264Config.idrPeriod = m_fps;
        encConfig.encodeCodecConfig.h264Config.sliceMode = 0;
        encConfig.encodeCodecConfig.h264Config.sliceModeData = 0;
    }

    reconfigParams.reInitEncodeParams.encodeGUID = codecGuid;
    reconfigParams.reInitEncodeParams.presetGUID = NV_ENC_PRESET_P1_GUID;
    reconfigParams.reInitEncodeParams.encodeWidth = m_width;
    reconfigParams.reInitEncodeParams.encodeHeight = m_height;
    reconfigParams.reInitEncodeParams.darWidth = m_width;
    reconfigParams.reInitEncodeParams.darHeight = m_height;
    reconfigParams.reInitEncodeParams.frameRateNum = m_fps;
    reconfigParams.reInitEncodeParams.frameRateDen = 1;
    reconfigParams.reInitEncodeParams.enablePTD = 1;
    reconfigParams.reInitEncodeParams.encodeConfig = &encConfig;
    reconfigParams.reInitEncodeParams.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;

    st = m_nvenc.nvEncReconfigureEncoder(m_encoder, &reconfigParams);
    if (st != NV_ENC_SUCCESS)
    {
        printf("[NVENC] nvEncReconfigureEncoder failed: %d\n", st);
        return false;
    }

    printf("[NVENC] Reconfigured bitrate dynamically: avg=%d, max=%d bps\n", avgBitrate, maxBitrate);
    return true;
}
