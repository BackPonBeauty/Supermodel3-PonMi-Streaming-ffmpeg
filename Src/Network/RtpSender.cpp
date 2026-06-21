#include "RtpSender.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <mswsock.h>

bool RtpSender::Init(const char *destIP, int destPort, bool useH265)
{
    m_useH265 = useH265;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET)
    {
        printf("[RTP] socket() failed: %d\n", WSAGetLastError());
        return false;
    }
    // ICMPエラーを無視（Port unreachable対策）
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

    srand((unsigned)time(nullptr));
    m_ssrc = rand();
    m_seqNum = (uint16_t)rand();
    m_timestamp = rand();

    printf("[RTP] Ready -> %s:%d\n", destIP, destPort);
    return true;
}

void RtpSender::Send(const uint8_t *nalData, int size)
{
    if (m_socket == INVALID_SOCKET || size <= 0)
        return;

    const uint8_t *nal = nalData;
    int nalSize = size;

    // スタートコード除去
    if (nalSize >= 4 && nal[0] == 0 && nal[1] == 0 && nal[2] == 0 && nal[3] == 1)
    {
        nal += 4;
        nalSize -= 4;
    }
    else if (nalSize >= 3 && nal[0] == 0 && nal[1] == 0 && nal[2] == 1)
    {
        nal += 3;
        nalSize -= 3;
    }

    if (nalSize <= 0)
        return;

    if (nalSize <= RTP_MTU)
    {
        SendRtpPacket(nal, nalSize, true);
    }
    else if (m_useH265)
    {
        // HEVC FU fragmentation
        uint8_t type = (nal[0] & 0x7E) >> 1;
        uint8_t payloadHeader0 = (49 << 1) | (nal[0] & 0x01);
        uint8_t payloadHeader1 = nal[1];
        const uint8_t *payload = nal + 2;
        int remaining = nalSize - 2;
        bool first = true;

        while (remaining > 0)
        {
            int fragSize = (remaining > RTP_MTU - 3) ? RTP_MTU - 3 : remaining;
            bool last = (remaining <= RTP_MTU - 3);

            uint8_t fuBuf[RTP_MTU + 3];
            fuBuf[0] = payloadHeader0;
            fuBuf[1] = payloadHeader1;
            fuBuf[2] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | type;
            memcpy(fuBuf + 3, payload, fragSize);

            SendRtpPacket(fuBuf, fragSize + 3, last);

            payload += fragSize;
            remaining -= fragSize;
            first = false;
        }
    }
    else
    {
        // H.264 FU-A fragmentation
        uint8_t type = nal[0] & 0x1F;
        uint8_t nri = nal[0] & 0x60;
        uint8_t fuIndicator = nri | 28; // FU-A type is 28
        const uint8_t *payload = nal + 1;
        int remaining = nalSize - 1;
        bool first = true;

        while (remaining > 0)
        {
            int fragSize = (remaining > RTP_MTU - 2) ? RTP_MTU - 2 : remaining;
            bool last = (remaining <= RTP_MTU - 2);

            uint8_t fuBuf[RTP_MTU + 2];
            fuBuf[0] = fuIndicator;
            fuBuf[1] = (first ? 0x80 : 0) | (last ? 0x40 : 0) | type;
            memcpy(fuBuf + 2, payload, fragSize);

            SendRtpPacket(fuBuf, fragSize + 2, last);

            payload += fragSize;
            remaining -= fragSize;
            first = false;
        }
    }

    m_timestamp += 1500; // 90kHz基準 60fps
}

void RtpSender::SendRtpPacket(const uint8_t *data, int size, bool marker)
{
    uint8_t buf[2048];

    buf[0] = 0x80;
    buf[1] = (marker ? 0x80 : 0) | 96;
    buf[2] = (m_seqNum >> 8) & 0xFF;
    buf[3] = m_seqNum & 0xFF;
    buf[4] = (m_timestamp >> 24) & 0xFF;
    buf[5] = (m_timestamp >> 16) & 0xFF;
    buf[6] = (m_timestamp >> 8) & 0xFF;
    buf[7] = m_timestamp & 0xFF;
    buf[8] = (m_ssrc >> 24) & 0xFF;
    buf[9] = (m_ssrc >> 16) & 0xFF;
    buf[10] = (m_ssrc >> 8) & 0xFF;
    buf[11] = m_ssrc & 0xFF;

    memcpy(buf + 12, data, size);

    std::lock_guard<std::mutex> lock(m_destsMutex);
    for (const auto &dest : m_dests)
    {
        sendto(m_socket, (char *)buf, size + 12, 0,
               (sockaddr *)&dest, sizeof(dest));
    }
    m_seqNum++;
}

void RtpSender::Shutdown()
{
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        WSACleanup();
        printf("[RTP] Shutdown\n");
    }
    std::lock_guard<std::mutex> lock(m_destsMutex);
    m_dests.clear();
}

void RtpSender::SetDestPort(int port)
{
    std::lock_guard<std::mutex> lock(m_destsMutex);
    m_destPort = port;
    for (auto &dest : m_dests)
    {
        dest.sin_port = htons((u_short)port);
    }
    printf("[RTP] Destination Port changed to %d\n", port);
}

void RtpSender::SetDestIP(const std::string &ip)
{
    std::vector<std::string> ips = { ip };
    SetDestIPs(ips);
}

void RtpSender::SetDestIPs(const std::vector<std::string> &ips)
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
    printf("[RTP] Destinations updated (%zu clients)\n", m_dests.size());
}

void RtpSender::SetDestEndpoints(const std::vector<std::pair<std::string, int>> &endpoints)
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
    printf("[RTP] Destinations updated (%zu endpoints)\n", m_dests.size());
}