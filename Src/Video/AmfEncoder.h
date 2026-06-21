#pragma once
#include "IEncoder.h"
#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>
#include <utility>

// AMF SDK Forward declarations and includes
#include "AMF/core/Context.h"
#include "AMF/core/Factory.h"
#include "AMF/components/VideoEncoderHEVC.h"
#include "AMF/components/VideoEncoderVCE.h"
#include "Network/RtpSender.h"

class AmfEncoder : public IEncoder
{
public:
    AmfEncoder();
    virtual ~AmfEncoder() { Shutdown(); }

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
    bool LoadAmfDll();
    void ProcessOutput();

    HMODULE m_amfDll = nullptr;
    amf::AMFFactory* m_pFactory = nullptr;
    amf::AMFContextPtr m_pContext;
    amf::AMFComponentPtr m_pEncoder;

    int m_width = 0;
    int m_height = 0;
    int m_fps = 60;
    std::string m_codec;
    OnEncodedCallback m_callback;

    // Buffer for OpenGL frame readback
    std::vector<uint8_t> m_hostBuffer;

    // RTP transmission
    RtpSender m_rtpSender;
    bool m_rtpEnabled = false;
};
