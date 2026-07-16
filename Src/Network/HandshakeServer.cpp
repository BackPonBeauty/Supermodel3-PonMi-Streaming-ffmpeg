#include "HandshakeServer.h"
#include <winsock2.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

#define TO_SOCKET(p) (reinterpret_cast<SOCKET>(p))

bool HandshakeServer::Start(int port, int width, int height, const std::string &codec,
                             OnClientListChangedCallback onListChanged)
{
    m_port = port;
    m_width = width;
    m_height = height;
    m_codec = codec;
    m_onListChanged = onListChanged;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        printf("[Handshake] socket() failed\n");
        return false;
    }

    int timeout = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&timeout, sizeof(timeout));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        printf("[Handshake] bind() failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return false;
    }

    m_socket = reinterpret_cast<void *>(sock);
    m_running.store(true);

    m_listenThread = CreateThread(nullptr, 0, ListenThreadProc, this, 0, nullptr);
    m_heartbeatThread = CreateThread(nullptr, 0, HeartbeatThreadProc, this, 0, nullptr);

    printf("[Handshake] Listening on port %d\n", port);
    return true;
}

unsigned long __stdcall HandshakeServer::ListenThreadProc(void *param)
{
    static_cast<HandshakeServer *>(param)->ListenLoop();
    return 0;
}

unsigned long __stdcall HandshakeServer::HeartbeatThreadProc(void *param)
{
    static_cast<HandshakeServer *>(param)->HeartbeatLoop();
    return 0;
}

void HandshakeServer::ListenLoop()
{
    char buf[64];
    sockaddr_in client = {};
    int clientLen = sizeof(client);

    while (m_running.load())
    {
        int received = recvfrom(TO_SOCKET(m_socket), buf, sizeof(buf) - 1, 0,
                                (sockaddr *)&client, &clientLen);
        if (received <= 0)
            continue;

        buf[received] = '\0';
        std::string clientIP = inet_ntoa(client.sin_addr);
        int clientPort = ntohs(client.sin_port);

        if (strncmp(buf, "HELLO", 5) == 0 && (buf[5] == '\0' || buf[5] == ':'))
        {
            std::string discordNick = "guest";
            std::string chosenCodec = m_codec;
            std::string sessionId = "";

            if (buf[5] == ':' && buf[6] != '\0')
            {
                std::string payload = std::string(buf + 6);
                // format: <nick>:<codec1>,<codec2>,...:<sessionId>
                auto colonPos = payload.find(':');
                if (colonPos != std::string::npos)
                {
                    discordNick = payload.substr(0, colonPos);
                    std::string rest = payload.substr(colonPos + 1);

                    // sessionId は末尾コロンの後
                    auto lastColon = rest.rfind(':');
                    std::string codecList;
                    if (lastColon != std::string::npos)
                    {
                        codecList = rest.substr(0, lastColon);
                        sessionId = rest.substr(lastColon + 1);
                    }
                    else
                    {
                        codecList = rest;
                    }

                    if (!codecList.empty())
                    {
                        // prefer h264 if available
                        bool hasH264 = codecList.find("h264") != std::string::npos;
                        bool hasHevc = codecList.find("hevc") != std::string::npos;
                        if (hasH264)
                            chosenCodec = "h264";
                        else if (hasHevc)
                            chosenCodec = "hevc";
                    }
                }
                else
                {
                    discordNick = payload;
                }
            }

            printf("[Handshake] HELLO from %s:%d nick=%s codec=%s sid=%s\n",
                   clientIP.c_str(), clientPort, discordNick.c_str(), chosenCodec.c_str(), sessionId.c_str());

            bool allowed = false;
            bool listChanged = false;
            std::vector<std::string> currentIPs;

            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);

                // 同じsessionIdが既に登録済み → 重複HELLOなのでOKを返すだけ
                auto it = std::find_if(m_clients.begin(), m_clients.end(),
                                       [&sessionId](const ClientInfo &c) { return !c.sessionId.empty() && c.sessionId == sessionId; });
                if (it != m_clients.end())
                {
                    allowed = true;
                }
                else if (m_clients.size() < 2)
                {
                    ClientInfo newClient;
                    newClient.ip = clientIP;
                    newClient.port = clientPort;
                    newClient.lastHeartbeat = GetTickCount();
                    newClient.discordNick = discordNick;
                    newClient.sessionId = sessionId;
                    m_clients.push_back(newClient);
                    allowed = true;
                    listChanged = true;

                    if (m_clients.size() == 1)
                        m_controllerLastInputTime.store(GetTickCount());
                }

                for (const auto &c : m_clients)
                    currentIPs.push_back(c.ip + ":" + c.discordNick);
            }

            if (allowed)
            {
                char ok[64];
                snprintf(ok, sizeof(ok), "OK %d %d %s", m_width, m_height, chosenCodec.c_str());
                sendto(TO_SOCKET(m_socket), ok, (int)strlen(ok), 0,
                       (sockaddr *)&client, clientLen);

                if (listChanged && m_onListChanged)
                {
                    m_onListChanged(currentIPs);
                }
            }
            else
            {
                printf("[Handshake] FULL. Rejecting %s:%d\n", clientIP.c_str(), clientPort);
                const char *fullMsg = "FULL";
                sendto(TO_SOCKET(m_socket), fullMsg, (int)strlen(fullMsg), 0,
                       (sockaddr *)&client, clientLen);
            }
        }
        else if (strncmp(buf, "PING", 4) == 0)
        {
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
            char pong[64];
            snprintf(pong, sizeof(pong), "PONG %lld", now);
            sendto(TO_SOCKET(m_socket), pong, (int)strlen(pong), 0,
                   (sockaddr *)&client, clientLen);
        }
        else if (strcmp(buf, "HB") == 0)
        {
            printf("[Handshake] HB received from %s:%d\n", clientIP.c_str(), clientPort);
            NotifyHeartbeat(clientIP, clientPort);
        }
        else if (strcmp(buf, "KICK") == 0 && (clientIP == "127.0.0.1" || clientIP == "localhost"))
        {
            printf("[Handshake] Local KICK request received. Kicking active controller.\n");
            bool listChanged = false;
            std::vector<std::string> currentIPs;
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                if (!m_clients.empty())
                {
                    sockaddr_in clientAddr = {};
                    clientAddr.sin_family = AF_INET;
                    clientAddr.sin_port = htons(m_clients[0].port);
                    clientAddr.sin_addr.s_addr = inet_addr(m_clients[0].ip.c_str());
                    const char *kickMsg = "KICK";
                    sendto(TO_SOCKET(m_socket), kickMsg, (int)strlen(kickMsg), 0,
                           (sockaddr *)&clientAddr, sizeof(clientAddr));

                    m_clients.erase(m_clients.begin());
                    listChanged = true;

                    if (!m_clients.empty())
                    {
                        m_controllerLastInputTime.store(GetTickCount());
                        printf("[Handshake] Control passed to next client: %s\n", m_clients[0].ip.c_str());
                    }
                }
            }
            if (listChanged && m_onListChanged)
            {
                for (const auto &c : m_clients)
                    currentIPs.push_back(c.ip + ":" + c.discordNick);
                m_onListChanged(currentIPs);
            }
        }
        else if (strncmp(buf, "STAT ", 5) == 0)
        {
            float loss = 0.0f;
            if (sscanf(buf + 5, "%f", &loss) == 1)
            {
                m_latestLossRate.store(loss);
                m_lastStatusTime.store(GetTickCount());
            }
        }
    }
}

void HandshakeServer::HeartbeatLoop()
{
    while (m_running.load())
    {
        Sleep(1000);
        bool listChanged = false;
        std::vector<std::string> currentIPs;

        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            uint32_t now = GetTickCount();

            // 5秒タイムアウト判定
            auto it = m_clients.begin();
            while (it != m_clients.end())
            {
                if (now - it->lastHeartbeat > 5000)
                {
                    printf("[Handshake] Client %s disconnected (timeout)\n", it->ip.c_str());
                    bool wasController = (it == m_clients.begin());
                    it = m_clients.erase(it);
                    listChanged = true;

                    if (wasController && !m_clients.empty())
                    {
                        m_controllerLastInputTime.store(GetTickCount());
                        printf("[Handshake] Control passed to next client: %s\n", m_clients[0].ip.c_str());
                    }
                }
                else
                {
                    ++it;
                }
            }

            // コントローラー（先頭クライアント）の1分無操作判定 (P1スロットのみ対象)
            if (this == &g_handshake && !m_clients.empty())
            {
                if (now - m_controllerLastInputTime.load() > 60000)
                {
                    printf("[Handshake] Controller client %s timed out (1 min inactivity). Kicking.\n", m_clients[0].ip.c_str());

                    // クライアントへKICKパケットを送信
                    sockaddr_in clientAddr = {};
                    clientAddr.sin_family = AF_INET;
                    clientAddr.sin_port = htons(m_clients[0].port);
                    clientAddr.sin_addr.s_addr = inet_addr(m_clients[0].ip.c_str());
                    const char *kickMsg = "KICK";
                    sendto(TO_SOCKET(m_socket), kickMsg, (int)strlen(kickMsg), 0,
                           (sockaddr *)&clientAddr, sizeof(clientAddr));

                    m_clients.erase(m_clients.begin());
                    listChanged = true;

                    if (!m_clients.empty())
                    {
                        m_controllerLastInputTime.store(GetTickCount());
                        printf("[Handshake] Control passed to next client: %s\n", m_clients[0].ip.c_str());
                    }
                }
            }

            if (listChanged)
            {
                for (const auto &c : m_clients)
                    currentIPs.push_back(c.ip + ":" + c.discordNick);
            }
        }

        if (listChanged && m_onListChanged)
        {
            m_onListChanged(currentIPs);
        }
    }
}

void HandshakeServer::NotifyHeartbeat(const std::string &clientIP, int port)
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    
    // 1. 完全一致 (IP & Port) を探す
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                           [&clientIP, port](const ClientInfo &c) { return c.ip == clientIP && c.port == port; });
    if (it != m_clients.end())
    {
        it->lastHeartbeat = GetTickCount();
        return;
    }

    // 2. フォールバック: 操作パケットやHBの送信元ポートが一時的にずれている場合、
    // 同じIPアドレスを持つクライアントの中から、最もポート番号の差が小さいものを検索して更新する。
    auto matchIt = m_clients.end();
    int minDiff = 999999;
    for (auto i = m_clients.begin(); i != m_clients.end(); ++i)
    {
        if (i->ip == clientIP)
        {
            int diff = std::abs(i->port - port);
            if (diff < minDiff)
            {
                minDiff = diff;
                matchIt = i;
            }
        }
    }

    if (matchIt != m_clients.end())
    {
        if (matchIt->port != port)
        {
            printf("[Handshake] HB Port changed from %d to %d for %s (updating)\n", matchIt->port, port, clientIP.c_str());
            matchIt->port = port;
        }
        matchIt->lastHeartbeat = GetTickCount();
    }
}

void HandshakeServer::NotifyControllerInput(const std::string &clientIP, int port)
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    if (m_clients.empty()) return;

    // 1. コントローラーの IP と Port が完全一致
    if (m_clients[0].ip == clientIP && m_clients[0].port == port)
    {
        m_controllerLastInputTime.store(GetTickCount());
        return;
    }

    // 2. フォールバック: 操作パケットの送信元ポートが一時的にずれている場合でもIPが同じなら許容
    if (m_clients[0].ip == clientIP)
    {
        m_controllerLastInputTime.store(GetTickCount());
    }
}

bool HandshakeServer::IsConnected() const
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    return !m_clients.empty();
}

std::string HandshakeServer::GetControllerIP()
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    return m_clients.empty() ? "" : m_clients[0].ip;
}

int HandshakeServer::GetControllerPort()
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    return m_clients.empty() ? 0 : m_clients[0].port;
}

std::vector<std::string> HandshakeServer::GetClientIPs()
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    std::vector<std::string> ips;
    for (const auto &c : m_clients)
        ips.push_back(c.ip + ":" + c.discordNick);
    return ips;
}

std::string HandshakeServer::GetDiscordNick(const std::string &ip)
{
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    if (ip.empty())
        return m_clients.empty() ? "" : m_clients[0].discordNick;
    std::string ipOnly = ip.substr(0, ip.find(':'));
    for (const auto &c : m_clients)
        if (c.ip == ipOnly)
            return c.discordNick;
    return "";
}

void HandshakeServer::Stop()
{
    m_running.store(false);
    if (m_socket)
    {
        closesocket(TO_SOCKET(m_socket));
        m_socket = nullptr;
    }
    if (m_listenThread)
    {
        WaitForSingleObject(m_listenThread, 3000);
        CloseHandle(m_listenThread);
        m_listenThread = nullptr;
    }
    if (m_heartbeatThread)
    {
        WaitForSingleObject(m_heartbeatThread, 3000);
        CloseHandle(m_heartbeatThread);
        m_heartbeatThread = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        m_clients.clear();
    }

    printf("[Handshake] Stopped\n");
}