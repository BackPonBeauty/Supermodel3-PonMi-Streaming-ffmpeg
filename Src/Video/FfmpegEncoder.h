#pragma once
#include "IEncoder.h"
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include "Network/RtpSender.h"

// Forward declare FFmpeg structures to keep header clean
struct AVCodecContext;
struct AVCodec;
struct AVFrame;
struct SwsContext;
struct AVPacket;

class FfmpegEncoder : public IEncoder
{
public:
    FfmpegEncoder();
    virtual ~FfmpegEncoder() { Shutdown(); }

    virtual bool Init(int width, int height, int fps, int port, const std::string &codec, OnEncodedCallback cb) override;
    virtual void EncodeFrame(unsigned int glTextureID) override;
    virtual bool ReconfigureBitrate(int avgBitrate, int maxBitrate) override;
    virtual void Shutdown() override;

    virtual void SetDestIP(const std::string &ip) override;
    virtual void SetDestIPs(const std::vector<std::string> &ips) override;
    virtual void SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints) override;
    void SetDestPort(int port) { m_rtpSender.SetDestPort(port); }
    virtual int GetWidth() const override { return m_width; }
    virtual int GetHeight() const override { return m_height; }

private:
    int m_width = 0;
    int m_height = 0;
    int m_fps = 60;
    std::string m_codec;
    OnEncodedCallback m_callback;

    // FFmpeg state
    AVCodecContext* m_codecContext = nullptr;
    const AVCodec* m_ffmpegCodec = nullptr;
    AVFrame* m_srcFrame = nullptr;
    AVFrame* m_dstFrame = nullptr;
    SwsContext* m_swsContext = nullptr;
    AVPacket* m_packet = nullptr;

    // Buffer for OpenGL frame readback
    std::vector<uint8_t> m_hostBuffer;

    // RTP transmission
    RtpSender m_rtpSender;
    bool m_rtpEnabled = false;
};
