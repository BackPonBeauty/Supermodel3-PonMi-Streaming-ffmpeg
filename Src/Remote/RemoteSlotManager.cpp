/**
 * RemoteSlotManager.cpp
 *
 * Lobby state of this server's slot (Firebase) and UPnP port opening.
 * P1 (2026-11-16): ViGEm / XInput receiving removed (see RemoteSlotManager.h).
 */

#ifdef SUPERMODEL_WIN32

#include <winsock2.h>   // before windows.h (pulled in by FirebaseMatchingCpp.h)
#include "RemoteSlotManager.h"
#include <cstdio>
#include <thread>
#include "Util/NewConfig.h"

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
RemoteSlotManager::~RemoteSlotManager()
{
    Shutdown();
}

void RemoteSlotManager::Shutdown()
{
    if (m_shutdownCalled)
        return;
    m_shutdownCalled = true;

    if (m_portsOpened)
    {
        if (m_linkplay == 0)
        {
            UPnPHelper::CloseStreamingPorts(1);
            UPnPHelper::CloseStreamingPorts(2);
        }
        else if (m_linkplay >= 1 && m_linkplay <= 8)
            UPnPHelper::CloseStreamingPorts(m_linkplay);
    }

    // Discord status message: delete it first (it reads the slots from Firebase)
    m_discord.Stop();

    // P6: take this server's slot off the lobby at once (instead of waiting for the 10-minute stale cleanup)
    if (m_firebase.IsInitialized() && !m_hostId.empty())
    {
        if (m_linkplay == 0)
        {
            m_firebase.RemoveSlot(m_hostId, 1);
            m_firebase.RemoveSlot(m_hostId, 2);
        }
        else
            m_firebase.RemoveSlot(m_hostId, m_linkplay);
    }

    m_firebase.Shutdown();
}

// ---------------------------------------------------------------------------
// Streaming ports (UPnP)
// ---------------------------------------------------------------------------
void RemoteSlotManager::StartStreaming(int linkplay)
{
    if (linkplay < 0 || linkplay > 8)
    {
        printf("[RemoteSlotManager] StartStreaming: invalid linkplay=%d\n", linkplay);
        return;
    }
    m_linkplay = linkplay;

    // miniupnpc uses sockets; make sure Winsock is up (reference-counted, safe to call again)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    m_portsOpened = true;
    std::thread([linkplay]()
                {
                    if (linkplay == 0)
                    {
                        UPnPHelper::OpenStreamingPorts(1);
                        UPnPHelper::OpenStreamingPorts(2);
                    }
                    else
                    {
                        UPnPHelper::OpenStreamingPorts(linkplay);
                    }
                })
        .detach();

    printf("[RemoteSlotManager] StartStreaming: linkplay=%d (UPnP ports requested)\n", linkplay);
}

// ---------------------------------------------------------------------------
// Start Firebase Thread on Launch
// ---------------------------------------------------------------------------
void RemoteSlotManager::StartFirebaseAsync(const std::string &gameTitle, const std::string &serverName)
{
    m_gameTitle = gameTitle; // Save ROM name
    m_serverName = serverName; // Save server name
    printf("[RemoteSlotManager] StartFirebaseAsync: gameTitle=%s serverName=%s\n",
           m_gameTitle.c_str(), m_serverName.c_str());

    // Get external IP -> Initialize Firebase, register host, start heartbeat sequentially in one thread
    std::thread([this]()
                {
        // Get external IP first
        m_externalIp = FirebaseMatchingCpp::GetExternalIp();
        printf("[RemoteSlotManager] External IP: %s\n", m_externalIp.c_str());

        m_hostId = m_externalIp;
        for (char& c : m_hostId)
            if (c == '.') c = '-';

        printf("[RemoteSlotManager] HostId: %s\n", m_hostId.c_str());

        if (m_externalIp.empty() || m_externalIp == "127.0.0.1")
        {
            printf("[RemoteSlotManager] Invalid IP, skipping Firebase\n");
            return;
        }

        auto cb = [this](const std::string& msg) { NotifyStatus(msg); };
        // 11/16: one Firebase session per slot (same uid after a restart: the database rules let only the owner write)
        m_firebase.SetSessionFile("Config/FirebaseSlot" + std::to_string(m_linkplay) + ".dat");
        if (m_firebase.Initialize(cb))
        {
            m_firebase.CleanupStaleHosts();
            UpdateAvailableSlots();
            m_firebase.StartHeartbeat(m_hostId, m_linkplay == 0 ? std::vector<int>{1, 2} : std::vector<int>{m_linkplay});
            StartDiscordPost();
        }
        else
        {
            printf("[RemoteSlotManager] Firebase init failed\n");
        } })
        .detach();
}

// ---------------------------------------------------------------------------
// Discord status post (test stage, 2026-11-16): one server per host posts the status of all slots
// ini DiscordPost: -1 = only the slot-1 server (default), 0 = off, 1 = on
// ---------------------------------------------------------------------------
void RemoteSlotManager::StartDiscordPost()
{
    if (!DiscordNotifier::Configured() || m_shutdownCalled)
        return;
    extern Util::Config::Node s_runtime_config;
    int mode = s_runtime_config["DiscordPost"].ValueAsDefault<int>(-1);
    bool post = (mode == 1) || (mode < 0 && (m_linkplay == 1 || m_linkplay == 0));
    if (!post)
        return;
    m_discord.Start([this](DiscordHostStatus &out) -> bool
                    {
        std::vector<LobbyHostCpp> hosts;
        if (!m_firebase.FetchHosts(hosts))
            return false;
        for (const LobbyHostCpp &h : hosts)
        {
            if (h.key != m_hostId)
                continue;
            out.servername = h.servername;
            out.gametitle = h.gametitle;
            out.ip = h.ip;
            for (int n = 1; n <= 8; n++)
            {
                out.present[n] = h.slots[n].present;
                out.available[n] = h.slots[n].available;
                out.player[n] = h.slots[n].player;
            }
            return true;
        }
        return false; });
}

// ---------------------------------------------------------------------------
// Update slot states on Firebase
// ---------------------------------------------------------------------------
void RemoteSlotManager::UpdateAvailableSlots()
{
    if (!m_firebase.IsInitialized() || m_externalIp.empty())
        return;

    // INI設定のStreaming有効・無効をリアルタイムに取得
    // (s_runtime_config は Main.cpp で設定されるグローバル設定ノードです)
    extern Util::Config::Node s_runtime_config;
    bool streamingEnabled = s_runtime_config["Streaming"].ValueAsDefault<bool>(false);

    bool available[9] = {false};   // P6: slots 1..8
    if (streamingEnabled)
    {
        if (m_linkplay == 0)
        {
            available[1] = true;
            available[2] = true;
        }
        else
        {
            available[m_linkplay] = true;
        }
    }
    // streamingEnabled が false の場合は available は全て false のままになります

    // Key = sanitized IP, PUT/PATCH based on linkplay number
    m_firebase.RegisterHost(m_externalIp, available, m_linkplay, m_gameTitle, m_serverName);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
void RemoteSlotManager::NotifyStatus(const std::string &msg)
{
    printf("[RemoteSlotManager] %s\n", msg.c_str());
    if (m_statusCb)
        m_statusCb(msg);
}

void RemoteSlotManager::SetSlotOccupied()
{
    if (!m_firebase.IsInitialized() || m_hostId.empty())
        return;
    // P4 (2026-11-16, requirement S-8d/Q32): no spectators any more -> a connected slot shows as in use.
    int slot = m_linkplay;
    std::thread([this, slot]()
                { m_firebase.PatchSlotAvailable(m_hostId, slot, false); })
        .detach();
    printf("[RemoteSlotManager] SetSlotOccupied (available=false) for linkplay=%d\n", m_linkplay);
}

void RemoteSlotManager::SetSlotAvailable()
{
    if (!m_firebase.IsInitialized() || m_hostId.empty())
        return;
    std::thread([this]()
                {
                    if (m_linkplay == 0)
                    {
                        m_firebase.PatchSlotAvailable(m_hostId, 1, true);
                        m_firebase.PatchSlotAvailable(m_hostId, 2, true);
                    }
                    else
                    {
                        m_firebase.PatchSlotAvailable(m_hostId, m_linkplay, true);
                    }
                })
        .detach();
    printf("[RemoteSlotManager] SetSlotAvailable for linkplay=%d\n", m_linkplay);
}

void RemoteSlotManager::SetSlotPlayer(int slot, const std::string &player)
{
    if (!m_firebase.IsInitialized() || m_hostId.empty()) return;
    std::string hostId = m_hostId;
    std::thread([this, hostId, slot, player]() {
        m_firebase.PatchSlotPlayer(hostId, slot, player);
    }).detach();
}

void RemoteSlotManager::SetSlotSpectator(int slot, const std::string &spectator)
{
    if (!m_firebase.IsInitialized() || m_hostId.empty()) return;
    std::string hostId = m_hostId;
    std::thread([this, hostId, slot, spectator]() {
        m_firebase.PatchSlotSpectator(hostId, slot, spectator);
    }).detach();
}

void RemoteSlotManager::SetSlotClientCount(int slot, int count)
{
    if (!m_firebase.IsInitialized() || m_hostId.empty()) return;
    std::string hostId = m_hostId;
    std::thread([this, hostId, slot, count]() {
        m_firebase.PatchSlotClientCount(hostId, slot, count);
    }).detach();
}
#endif // SUPERMODEL_WIN32
