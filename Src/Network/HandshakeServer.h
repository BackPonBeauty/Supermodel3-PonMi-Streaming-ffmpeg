#pragma once
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>

class HandshakeServer
{
public:
    struct ClientInfo {
        std::string ip;
        int port;
        uint32_t lastHeartbeat;
    };

    // クライアントリストが変更された時のコールバック
    using OnClientListChangedCallback = std::function<void(const std::vector<std::string> &clientIPs)>;

    HandshakeServer() = default;
    ~HandshakeServer() { Stop(); }

    bool Start(int port, int width, int height, const std::string &codec,
               OnClientListChangedCallback onListChanged);

    void Stop();
    bool IsConnected() const;
    void NotifyHeartbeat(const std::string &clientIP, int port);
    void NotifyControllerInput(const std::string &clientIP, int port);

    std::string GetControllerIP();
    int GetControllerPort();
    std::vector<std::string> GetClientIPs();

    float GetLatestLossRate() const { return m_latestLossRate.load(); }
    uint32_t GetLastStatusTime() const { return m_lastStatusTime.load(); }

private:
    int m_width = 960;
    int m_height = 540;

    static unsigned long __stdcall ListenThreadProc(void *param);
    static unsigned long __stdcall HeartbeatThreadProc(void *param);
    void ListenLoop();
    void HeartbeatLoop();

    void *m_socket = nullptr;
    void *m_listenThread = nullptr;
    void *m_heartbeatThread = nullptr;
    std::atomic<bool> m_running{false};

    std::vector<ClientInfo> m_clients;
    mutable std::mutex m_clientsMutex;
    std::atomic<uint32_t> m_controllerLastInputTime{0};
    std::atomic<float> m_latestLossRate{0.0f};
    std::atomic<uint32_t> m_lastStatusTime{0};

    OnClientListChangedCallback m_onListChanged;
    int m_port = 55001;
    std::string m_codec;
};

extern HandshakeServer g_handshake;
extern HandshakeServer g_handshakeP2;

