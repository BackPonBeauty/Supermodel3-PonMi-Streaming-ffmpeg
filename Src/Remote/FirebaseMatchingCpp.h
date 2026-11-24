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
#include <mutex>

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
        bool locked = false;     // SV-18: the host has a room password
        bool netlink = false;    // 11/23: "mode":"netlink" - played from the player's own ROM (link data, no video)
        bool remote = false;     // 11/24: "mode":"auto" - a REMOTE slot: the player picks STREAM (video) or NETLINK
    } slots[9];                  // 1..8
    bool locked = false;         // any slot locked
    std::string sess;            // 11/17: games in the link, "stage,area,players;..." (GAME A first)
};

using FirebaseStatusCallback = std::function<void(const std::string &message)>;

class FirebaseMatchingCpp
{
public:
    // P6 / 11/16: the Firebase API key and database URL are not in the source. They are
    // embedded at build time (Stream/Secrets.h, Scripts/gen_keys.ps1). Without them, Initialize() fails and Firebase is off.

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
    // SV-18 (11/16): the slot is written with "locked":true (the lobby draws a lock). Set before RegisterHost.
    void SetLocked(bool locked) { m_locked = locked; }
    // 11/23 netlink: the slot is written with "mode":"netlink" (the lobby starts the player's own ROM for it). Only
    // written when set, so video slots work with older database rules. Set before RegisterHost.
    void SetNetlink(bool netlink) { m_netlink = netlink; }
    // 11/24: a REMOTE slot, written with "mode":"auto" (needs the database rule to allow 'auto')
    void SetRemote(bool remote) { m_remote = remote; }

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

    // 11/16: the newest main5 build published by the owner at /main5 (supermodel = build number, url = download
    // page, optional). Returns false if it could not be read; supermodel = 0 when the key is not there yet.
    bool FetchMain5Version(long long &supermodel, std::string &url);

    // Get external IP address (uses api.ipify.org)
    static std::string GetExternalIp();
    // P6: this PC's address on its local network (no packet is sent)
    static std::string GetLanIp();

    // Start heartbeat: every 2 minutes the timestamp of this server's own slot(s) (11/16: per slot) and,
    // every 30 minutes, a new ID token from the refresh token (the uid stays the same: the slots are ours).
    void StartHeartbeat(const std::string &hostId, const std::vector<int> &slots);
    void StopHeartbeat();

    bool IsInitialized() const { return m_initialized; }

    // 11/16: host slots keep their Firebase session in this DPAPI-encrypted file (same uid after a restart). Before Initialize.
    void SetSessionFile(const std::string &path) { m_sessionFile = path; }

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

    // Firebase Auth anonymous sign-in (keeps the uid and the refresh token)
    bool SignInAnonymously();
    // 11/16: new ID token from the refresh token (same uid). Falls back to nothing on failure.
    bool RefreshIdToken();
    // the current ID token (thread-safe copy)
    std::string Token();
    void SaveSession();

    // Get timestamp (Unix milliseconds)
    static long long GetUnixTimestamp();

    // JSON Utility (lightweight implementation)
    static std::string MakeHostJson(const HostInfoCpp &info);
    static std::string EscapeJson(const std::string &s);

    bool m_initialized = false;
    std::mutex m_tokenLock;      // guards the three below (the heartbeat thread refreshes them)
    std::string m_idToken;       // Firebase ID token
    std::string m_refreshToken;  // 11/16: to get new ID tokens for the same uid
    std::string m_uid;           // 11/16: anonymous user id = "owner" of this server's slots (database rules)
    std::string m_currentHostId;
    bool m_locked = false;
    bool m_netlink = false;   // 11/23
    bool m_remote = false;    // 11/24
    std::string m_sessionFile;   // 11/16: "" = do not keep the session (the lobby)

    // Heartbeat thread
    std::thread m_heartbeatThread;
    std::atomic<bool> m_heartbeatRunning{false};

    FirebaseStatusCallback m_statusCb;

    void HeartbeatThread(std::string hostId, std::vector<int> slots);
};

#endif // SUPERMODEL_WIN32
