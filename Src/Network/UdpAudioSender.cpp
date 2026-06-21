#include "UdpAudioSender.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <mswsock.h>

bool UdpAudioSender::Init(const char *destIP, int destPort)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET)
    {
        printf("[AudioUDP] socket() failed: %d\n", WSAGetLastError());
        return false;
    }
    DWORD dwBytesReturned = 0;
    BOOL bNewBehavior = FALSE;
    WSAIoctl(m_socket, SIO_UDP_CONNRESET,
             &bNewBehavior, sizeof(bNewBehavior),
             NULL, 0, &dwBytesReturned, NULL, NULL);

    m_destPort = destPort;
    if (destIP && strlen(destIP) > 0)
    {
        SetDestIP(destIP);
    }

    // Opusエンコーダー初期化
    int err;
    m_encoder = opus_encoder_create(OPUS_SAMPLE_RATE, OPUS_CHANNELS,
                                    OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || !m_encoder)
    {
        printf("[AudioUDP] opus_encoder_create failed: %d\n", err);
        return false;
    }

    // 低遅延設定
    opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(128000)); // 128kbps
    opus_encoder_ctl(m_encoder, OPUS_SET_COMPLEXITY(5));   // 中程度
    opus_encoder_ctl(m_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    m_opusBuf.resize(OPUS_MAX_PACKET);
    m_resampleBuf.resize(OPUS_FRAME_SIZE * OPUS_CHANNELS);
    m_floatBuf.resize(OPUS_FRAME_SIZE * OPUS_CHANNELS);
    m_inputAccum.clear();

    printf("[AudioUDP] Ready -> %s:%d (Opus 128kbps)\n", destIP, destPort);
    return true;
}

void UdpAudioSender::SendWithTimestamp(const int16_t *pcm, int samples, int ch)
{
    if (m_socket == INVALID_SOCKET || !m_encoder)
        return;

    for (int i = 0; i < samples * ch; i++)
        m_inputAccum.push_back(pcm[i]);

    const int srcFrameSize = 882;
    const int frameBytes = srcFrameSize * OPUS_CHANNELS;

    // 次フレームの先頭1サンプルも見えるように +1 余裕を持って判定
    while ((int)m_inputAccum.size() >= frameBytes + OPUS_CHANNELS)
    {
        for (int i = 0; i < OPUS_FRAME_SIZE; i++)
        {
            float srcPos = (float)i * srcFrameSize / OPUS_FRAME_SIZE;
            int s0 = (int)srcPos;
            int s1 = s0 + 1;  // 次フレーム先頭を参照できるので clamp 不要
            float f = srcPos - s0;

            m_resampleBuf[i * 2 + 0] = (int16_t)(m_inputAccum[s0 * 2 + 0] * (1.0f - f) +
                                                  m_inputAccum[s1 * 2 + 0] * f);
            m_resampleBuf[i * 2 + 1] = (int16_t)(m_inputAccum[s0 * 2 + 1] * (1.0f - f) +
                                                  m_inputAccum[s1 * 2 + 1] * f);
        }

        // 消費するのは882サンプル分のみ（+1は次フレームに残す）
        m_inputAccum.erase(m_inputAccum.begin(),
                           m_inputAccum.begin() + frameBytes);

        // 以降はエンコード・送信（変更なし）
        int encoded = opus_encode(m_encoder,
                                  m_resampleBuf.data(),
                                  OPUS_FRAME_SIZE,
                                  m_opusBuf.data(),
                                  OPUS_MAX_PACKET);
        if (encoded < 0)
            continue;

        std::vector<uint8_t> pkt(4 + encoded);
        pkt[0] = (m_timestamp >> 24) & 0xFF;
        pkt[1] = (m_timestamp >> 16) & 0xFF;
        pkt[2] = (m_timestamp >> 8)  & 0xFF;
        pkt[3] =  m_timestamp        & 0xFF;
        memcpy(pkt.data() + 4, m_opusBuf.data(), encoded);

        std::lock_guard<std::mutex> lock(m_destsMutex);
        for (const auto &dest : m_dests)
        {
            sendto(m_socket, (const char *)pkt.data(), (int)pkt.size(), 0,
                   (sockaddr *)&dest, sizeof(dest));
        }

        m_timestamp += OPUS_FRAME_SIZE;
    }
}

void UdpAudioSender::SetDestIP(const std::string &ip)
{
    std::vector<std::string> ips = { ip };
    SetDestIPs(ips);
}

void UdpAudioSender::SetDestIPs(const std::vector<std::string> &ips)
{
    std::lock_guard<std::mutex> lock(m_destsMutex);
    m_dests.clear();
    for (const auto &ip : ips)
    {
        bool exists = false;
        for (const auto &dest : m_dests)
        {
            if (inet_ntoa(dest.sin_addr) == ip) {
                exists = true;
                break;
            }
        }
        if (exists) continue;

        sockaddr_in dest = {};
        dest.sin_family = AF_INET;
        dest.sin_port = htons((u_short)m_destPort);
        dest.sin_addr.s_addr = inet_addr(ip.c_str());
        m_dests.push_back(dest);
    }
    printf("[AudioUDP] Destinations updated (%zu clients)\n", m_dests.size());
}

void UdpAudioSender::SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints)
{
    std::lock_guard<std::mutex> lock(m_destsMutex);
    m_dests.clear();
    for (const auto &ep : endpoints)
    {
        sockaddr_in dest = {};
        dest.sin_family = AF_INET;
        dest.sin_port = htons((u_short)ep.second);
        dest.sin_addr.s_addr = inet_addr(ep.first.c_str());
        m_dests.push_back(dest);
    }
    printf("[AudioUDP] Destinations updated (%zu endpoints)\n", m_dests.size());
}

void UdpAudioSender::SetDestPort(int port)
{
    std::lock_guard<std::mutex> lock(m_destsMutex);
    m_destPort = port;
    for (auto &dest : m_dests)
    {
        dest.sin_port = htons((u_short)port);
    }
    printf("[AudioUDP] Port changed to %d\n", port);
}

void UdpAudioSender::Shutdown()
{
    if (m_encoder)
    {
        opus_encoder_destroy(m_encoder);
        m_encoder = nullptr;
    }
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        printf("[AudioUDP] Shutdown\n");
    }
    std::lock_guard<std::mutex> lock(m_destsMutex);
    m_dests.clear();
}
