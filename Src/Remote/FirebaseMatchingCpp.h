/**
 * FirebaseMatchingCpp.h
 *
 * Firebase Realtime Database REST API Wrapper (using WinHTTP)
 *
 * Provides functions equivalent to FirebaseMatching.vb on the VB.NET side:
 *   - Anonymous authentication (Firebase Auth REST API)
 *   - Registration and updates of host information
 *   - Heartbeat (every 2 minutes)
 *   - External IP address retrieval
 *   - Cleanup of stale host entries
 */
#pragma once

#ifdef SUPERMODEL_WIN32

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <map>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// Slot info (corresponds to SlotInfo class in VB.NET)
struct SlotInfoCpp
{
    int xinputPort;
    int handshakePort;
    int videoPort;
    int audioPort;
    bool available;
};

// Host info (corresponds to HostInfo class in VB.NET)
struct HostInfoCpp
{
    long long timestamp;
    std::string ip;
    std::string gametitle; // Name of the running ROM (e.g., spikeofe)
    std::string servername;
    std::string lanip;     // P6: address on the host's own network (for clients behind the same router)
    std::map<int, SlotInfoCpp> slots; // key: slot number 1 to 8
};

// A host as the lobby sees it (P6, read from /hosts.json)
struct LobbyHostCpp
{
    std::string key;
    std::string ip;
    std::string lanip;
    std::string servername;
    std::string gametitle;
    long long timestamp = 0;
    struct Slot
    {
        bool present = false;    // the host has this slot (a streaming server runs for it)
        bool available = false;  // nobody connected
        std::string player;
    } slots[9];                  // 1..8
};

using FirebaseStatusCallback = std::function<void(const std::string &message)>;

class FirebaseMatchingCpp
{
public:
    // P6 (2026-11-16): the Firebase API key and database URL are no longer compiled in. They come from
    // Config/StreamingKeys.ini (Stream/Secrets.h, not in git). Without it, Initialize() fails and Firebase is off.

    // Dynamic Port definitions based on supermodel.ini s_runtime_config
    static int GetSlotXInputPort(int slot);
    static int GetSlotHandshakePort(int slot);
    static int GetSlotVideoPort(int slot);
    static int GetSlotAudioPort(int slot);

    FirebaseMatchingCpp();
    ~FirebaseMatchingCpp();

    // Perform anonymous authentication and initialize DB client
    bool Initialize(FirebaseStatusCallback statusCb = nullptr);
    void Shutdown();

    // Register host information to Firebase (PUT=create new / PATCH=update slot)
    // Key is automatically determined by SanitizeKeyFromIp(externalIp)
    bool RegisterHost(const std::string &externalIp,
                      const bool availableSlots[9],   // index 1..8
                      int linkplay,
                      const std::string &gameTitle = "",
                      const std::string &serverName = "");

    // Unregister host
    bool UnregisterHost(const std::string &hostId);
    // P6: remove one slot of this host (the server of that slot is closing)
    bool RemoveSlot(const std::string &hostId, int slot);
    bool PatchSlotAvailable(const std::string &hostId, int linkplay, bool available);
    bool PatchSlotClientCount(const std::string &hostId, int slot, int count);
    bool PatchSlotPlayer(const std::string &hostId, int slot, const std::string &player);
    bool PatchSlotSpectator(const std::string &hostId, int slot, const std::string &spectator);

    // Clean up stale host entries (no updates for 10+ minutes)
    bool CleanupStaleHosts();

    // Lobby (P6): all hosts, without the ones not updated for 10+ minutes (LB-01/LB-06)
    bool FetchHosts(std::vector<LobbyHostCpp> &out);

    // Get external IP address (uses api.ipify.org)
    static std::string GetExternalIp();
    // P6: this PC's address on its local network (no packet is sent)
    static std::string GetLanIp();

    // Start heartbeat (updates timestamp every 2 minutes)
    void StartHeartbeat(const std::string &hostId);
    void StopHeartbeat();

    bool IsInitialized() const { return m_initialized; }

private:
    // Send HTTP request using WinHTTP
    std::string HttpPost(const std::wstring &host, const std::wstring &path,
                         const std::string &jsonBody, bool useHttps = true);
    std::string HttpGet(const std::wstring &host, const std::wstring &path,
                        const std::string &authToken = "", bool useHttps = true);
    std::string HttpPut(const std::wstring &host, const std::wstring &path,
                        const std::string &jsonBody,
                        const std::string &authToken = "", bool useHttps = true);
    std::string HttpPatch(const std::wstring &host, const std::wstring &path,
                          const std::string &jsonBody,
                          const std::string &authToken = "", bool useHttps = true);
    std::string HttpDelete(const std::wstring &host, const std::wstring &path,
                           const std::string &authToken = "", bool useHttps = true);

    // IP address -> Firebase key conversion (replaces dots etc. with '-')
    static std::string SanitizeKeyFromIp(const std::string &ip);

    // Firebase Auth anonymous sign-in
    bool SignInAnonymously();

    // Get timestamp (Unix milliseconds)
    static long long GetUnixTimestamp();

    // JSON Utility (lightweight implementation)
    static std::string MakeHostJson(const HostInfoCpp &info);
    static std::string EscapeJson(const std::string &s);

    bool m_initialized = false;
    std::string m_idToken; // Firebase ID token
    std::string m_currentHostId;

    // Heartbeat thread
    std::thread m_heartbeatThread;
    std::atomic<bool> m_heartbeatRunning{false};

    FirebaseStatusCallback m_statusCb;

    void HeartbeatThread(std::string hostId);
};

#endif // SUPERMODEL_WIN32
