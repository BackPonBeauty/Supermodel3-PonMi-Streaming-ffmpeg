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
    std::map<int, SlotInfoCpp> slots; // key: slot number 1 to 4
};

using FirebaseStatusCallback = std::function<void(const std::string &message)>;

class FirebaseMatchingCpp
{
public:
    // Firebase project settings (matching FirebaseMatching.vb in VB.NET)
    static constexpr const char *API_KEY = "API_KEY";
    static constexpr const char *DB_URL = "DB_URL";
    static constexpr const char *AUTH_DOMAIN = "AUTH_DOMAIN";

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
                      const bool availableSlots[5],
                      int linkplay,
                      const std::string &gameTitle = "",
                      const std::string &serverName = "");

    // Unregister host
    bool UnregisterHost(const std::string &hostId);
    bool PatchSlotAvailable(const std::string &hostId, int linkplay, bool available);

    // Clean up stale host entries (no updates for 10+ minutes)
    bool CleanupStaleHosts();

    // Get external IP address (uses api.ipify.org)
    static std::string GetExternalIp();

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
