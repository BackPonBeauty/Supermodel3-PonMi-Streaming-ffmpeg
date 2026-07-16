#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <winsock2.h>
#include <utility>

class RtpSender
{
public:
    RtpSender() = default;
    ~RtpSender() { Shutdown(); }

    bool Init(const char *destIP, int destPort, bool useH265 = true);
    void Send(const uint8_t *nalData, int size);
    void Shutdown();
    void SetDestPort(int port);
    void SetDestIP(const std::string &ip);
    void SetDestIPs(const std::vector<std::string> &ips);
    void SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints);
    float GetBitrateBps();

private:
    void SendRtpPacket(const uint8_t *data, int size, bool marker);
    void HelloRecvLoop();
    static unsigned long __stdcall HelloRecvThreadProc(void *param);

    SOCKET m_socket = INVALID_SOCKET;
    std::vector<sockaddr_in> m_dests;
    std::mutex m_destsMutex;
    int m_destPort = 0;
    HANDLE m_helloThread = nullptr;
    std::atomic<bool> m_helloRunning{false};

    uint16_t m_seqNum = 0;
    uint32_t m_timestamp = 0;
    uint32_t m_ssrc = 0;

    static constexpr int RTP_MTU = 1400;
    bool m_useH265 = true;

    std::atomic<uint64_t> m_bytesSentAcc{0};
    std::atomic<float>    m_bitrateBps{0.0f};
    std::atomic<uint32_t> m_lastBitrateTime{0};
};