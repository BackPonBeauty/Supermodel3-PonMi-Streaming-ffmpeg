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

    // ローカルポートにbind（HELLOホールパンチを受信するため）
    sockaddr_in localAddr = {};
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons((u_short)destPort);
    localAddr.sin_addr.s_addr = INADDR_ANY;
    bind(m_socket, (sockaddr *)&localAddr, sizeof(localAddr));

    int timeout = 500;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

    srand((unsigned)time(nullptr));
    m_ssrc = rand();
    m_seqNum = (uint16_t)rand();
    m_timestamp = rand();

    // HELLOパケット受信スレッド起動（クライアントの一時ポートを動的学習）
    m_helloRunning.store(true);
    m_helloThread = CreateThread(nullptr, 0, HelloRecvThreadProc, this, 0, nullptr);

    printf("[RTP] Ready (listening on port %d, no dest until HELLO)\n", destPort);
    return true;
}

unsigned long __stdcall RtpSender::HelloRecvThreadProc(void *param)
{
    static_cast<RtpSender *>(param)->HelloRecvLoop();
    return 0;
}

void RtpSender::HelloRecvLoop()
{
    char buf[256];
    sockaddr_in from = {};
    int fromLen = sizeof(from);

    while (m_helloRunning.load())
    {
        int received = recvfrom(m_socket, buf, sizeof(buf) - 1, 0,
                                (sockaddr *)&from, &fromLen);
        if (received <= 0)
            continue;

        buf[received] = '\0';

        if (strncmp(buf, "HELLO", 5) == 0)
        {
            std::string fromIP = inet_ntoa(from.sin_addr);
            int fromPort = ntohs(from.sin_port);
            printf("[RTP] HELLO from %s:%d -> learning dest\n", fromIP.c_str(), fromPort);

            std::lock_guard<std::mutex> lock(m_destsMutex);
            // 同じIPのエントリがあればポートを更新、なければ追加
            bool found = false;
            for (auto &dest : m_dests)
            {
                if (dest.sin_addr.s_addr == from.sin_addr.s_addr)
                {
                    dest.sin_port = from.sin_port;
                    found = true;
                    break;
                }
            }
            if (found) continue;
            sockaddr_in newDest = {};
            newDest.sin_family = AF_INET;
            newDest.sin_addr = from.sin_addr;
            newDest.sin_port = from.sin_port;
            m_dests.push_back(newDest);
        }
    }
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

    int sent = size + 12;
    std::lock_guard<std::mutex> lock(m_destsMutex);
    for (const auto &dest : m_dests)
    {
        sendto(m_socket, (char *)buf, sent, 0,
               (sockaddr *)&dest, sizeof(dest));
    }
    if (!m_dests.empty())
        m_bytesSentAcc.fetch_add((uint64_t)sent, std::memory_order_relaxed);
    m_seqNum++;
}

float RtpSender::GetBitrateBps()
{
    uint32_t now     = GetTickCount();
    uint32_t last    = m_lastBitrateTime.load(std::memory_order_relaxed);
    uint32_t elapsed = now - last;
    if (elapsed >= 500 && last != 0)
    {
        uint64_t bytes = m_bytesSentAcc.exchange(0, std::memory_order_relaxed);
        float bps = (float)bytes * 8.0f * 1000.0f / (float)elapsed;
        m_bitrateBps.store(bps, std::memory_order_relaxed);
        m_lastBitrateTime.store(now, std::memory_order_relaxed);
    }
    else if (last == 0)
    {
        m_lastBitrateTime.store(now, std::memory_order_relaxed);
    }
    return m_bitrateBps.load(std::memory_order_relaxed);
}

void RtpSender::Shutdown()
{
    m_helloRunning.store(false);
    if (m_socket != INVALID_SOCKET)
    {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        WSACleanup();
    }
    if (m_helloThread)
    {
        WaitForSingleObject(m_helloThread, 2000);
        CloseHandle(m_helloThread);
        m_helloThread = nullptr;
    }
    std::lock_guard<std::mutex> lock(m_destsMutex);
    m_dests.clear();
    printf("[RTP] Shutdown\n");
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