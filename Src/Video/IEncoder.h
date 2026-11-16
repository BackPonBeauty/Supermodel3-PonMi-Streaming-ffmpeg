#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <utility>

using OnEncodedCallback = std::function<void(const uint8_t* data, size_t size, bool isKeyframe)>;

class IEncoder
{
public:
    virtual ~IEncoder() = default;
    virtual bool Init(int width, int height, int fps, int port, const std::string &codec, OnEncodedCallback cb) = 0;
    virtual void EncodeFrame(unsigned int glTextureID) = 0;
    virtual bool ReconfigureBitrate(int avgBitrate, int maxBitrate) = 0;
    virtual void Shutdown() = 0;

    // P2 (2026-11-16, design SV-03/D-1): make the next encoded frame a keyframe (IDR + parameter sets).
    // Thread-safe: may be called from a network thread; the encoder thread picks it up on the next frame.
    virtual void RequestKeyframe() = 0;

    virtual void SetDestIP(const std::string &ip) = 0;
    virtual void SetDestIPs(const std::vector<std::string> &ips) = 0;
    virtual void SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints) = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
    virtual float GetBitrateBps() = 0;
};
