/**
 * FirebaseMatchingCpp.cpp
 *
 * Firebase Realtime Database REST API implementation (using WinHTTP)
 */

#ifdef SUPERMODEL_WIN32

#include <winsock2.h>   // P6: GetLanIp (before windows.h)
#include <ws2tcpip.h>
#include "FirebaseMatchingCpp.h"
#include "Stream/Secrets.h"
#include "Stream/MiniJson.h"
#include <winhttp.h>
#include <sstream>
#include <ctime>
#include <cstdio>
#include <chrono>

constexpr int HEARTBEAT_INTERVAL_MS = 120000; // 2 minutes
constexpr int TIMEOUT_MINUTES = 10;

#include "Util/NewConfig.h"
extern Util::Config::Node s_runtime_config;

static int GetPortConfig(const std::string &primaryKey, const std::string &secondaryKey, int defaultValue)
{
    if (s_runtime_config.TryGet(primaryKey) && s_runtime_config[primaryKey].Exists())
    {
        return s_runtime_config[primaryKey].ValueAs<int>();
    }
    if (s_runtime_config.TryGet(secondaryKey) && s_runtime_config[secondaryKey].Exists())
    {
        return s_runtime_config[secondaryKey].ValueAs<int>();
    }
    return defaultValue;
}

int FirebaseMatchingCpp::GetSlotXInputPort(int slot)
{
    int base = GetPortConfig("XinputPort", "XInputPort", 55000);
    int currentLinkPlay = s_runtime_config["LinkPlay"].ValueAsDefault<int>(1);
    if (currentLinkPlay == 0)
    {
        return base + (slot - 1) * 4;
    }
    else
    {
        return base + (slot - currentLinkPlay) * 4;
    }
}

int FirebaseMatchingCpp::GetSlotHandshakePort(int slot)
{
    int base = GetPortConfig("HandshakePort", "Handshakeport", 55001);
    int currentLinkPlay = s_runtime_config["LinkPlay"].ValueAsDefault<int>(1);
    if (currentLinkPlay == 0)
    {
        return base + (slot - 1) * 4;
    }
    else
    {
        return base + (slot - currentLinkPlay) * 4;
    }
}

int FirebaseMatchingCpp::GetSlotVideoPort(int slot)
{
    int base = GetPortConfig("VideoPort", "Videoport", 55002);
    int currentLinkPlay = s_runtime_config["LinkPlay"].ValueAsDefault<int>(1);
    if (currentLinkPlay == 0)
    {
        // LinkPlay=0はRtpSenderが1つしかなく全スロット同じポートをbind
        return base;
    }
    return base + (slot - currentLinkPlay) * 4;
}

int FirebaseMatchingCpp::GetSlotAudioPort(int slot)
{
    int base = GetPortConfig("AudioPort", "Audioport", 55003);
    int currentLinkPlay = s_runtime_config["LinkPlay"].ValueAsDefault<int>(1);
    if (currentLinkPlay == 0)
    {
        // LinkPlay=0はUdpAudioSenderが1つしかなく全スロット同じポートをbind
        return base;
    }
    return base + (slot - currentLinkPlay) * 4;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
FirebaseMatchingCpp::FirebaseMatchingCpp() {}

FirebaseMatchingCpp::~FirebaseMatchingCpp()
{
    Shutdown();
}

void FirebaseMatchingCpp::Shutdown()
{
    StopHeartbeat();
    // P6: do NOT delete the whole host here - other servers of the same PC (other slots) may still run.
    // RemoteSlotManager removes this server's own slot (RemoveSlot); hosts with no slots are hidden by the
    // lobby and deleted by the 10-minute stale cleanup.
    m_initialized = false;
    m_idToken.clear();
}

// ---------------------------------------------------------------------------
// Initialization (Anonymous Auth)
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::Initialize(FirebaseStatusCallback statusCb)
{
    m_statusCb = statusCb;
    if (m_statusCb)
        m_statusCb("Firebase: Initializing...");

    // P6: keys come from Config/StreamingKeys.ini; without them Firebase stays off
    if (!StreamSecrets::HasFirebase())
    {
        if (m_statusCb)
            m_statusCb("Firebase: off (no Config/StreamingKeys.ini or no Firebase keys in it)");
        return false;
    }

    if (!SignInAnonymously())
    {
        if (m_statusCb)
            m_statusCb("Firebase: Fault");
        return false;
    }

    m_initialized = true;
    if (m_statusCb)
        m_statusCb("Firebase: Ready");
    return true;
}

// ---------------------------------------------------------------------------
// Firebase Auth Anonymous Sign-In
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::SignInAnonymously()
{
    // POST https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=API_KEY
    std::string path = "/v1/accounts:signUp?key=";
    path += StreamSecrets::Get().firebaseApiKey;

    std::string body = "{\"returnSecureToken\":true}";

    std::string response = HttpPost(
        L"identitytoolkit.googleapis.com",
        std::wstring(path.begin(), path.end()),
        body, true);

    if (response.empty())
        return false;

    // Retrieve idToken from JSON (lightweight parse)
    const std::string key1 = "\"idToken\":\"";
    const std::string key2 = "\"idToken\": \"";

    size_t pos = response.find(key1);
    size_t keyLen = key1.size();
    if (pos == std::string::npos)
    {
        pos = response.find(key2);
        keyLen = key2.size();
    }
    if (pos == std::string::npos)
        return false;

    pos += keyLen;
    size_t end = response.find('"', pos);
    if (end == std::string::npos)
        return false;

    m_idToken = response.substr(pos, end - pos);
    return !m_idToken.empty();
}

// ---------------------------------------------------------------------------
// Host Registration
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::RegisterHost(const std::string &externalIp,
                                       const bool availableSlots[9],
                                       int linkplay,
                                       const std::string &gameTitle,
                                       const std::string &serverName)
{
    // P6 (2026-11-16): one PATCH on the host record. It creates the record if needed and only replaces the
    // children it names (host fields + this server's own slot), so up to 8 servers of one PC can register at
    // the same time without overwriting each other's slots (the old GET-then-PUT replaced the whole record).
    std::string key = SanitizeKeyFromIp(externalIp);
    m_currentHostId = key;
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::ostringstream ss;
    ss << "{\"timestamp\":" << GetUnixTimestamp();
    ss << ",\"ip\":\"" << EscapeJson(externalIp) << "\"";
    std::string lan = GetLanIp();
    if (!lan.empty())
        ss << ",\"lanip\":\"" << EscapeJson(lan) << "\"";
    if (!gameTitle.empty())
        ss << ",\"gametitle\":\"" << EscapeJson(gameTitle) << "\"";
    if (!serverName.empty())
        ss << ",\"servername\":\"" << EscapeJson(serverName) << "\"";

    auto slotJson = [&](int slot) {
        ss << ",\"slot" << slot << "\":{";
        ss << "\"xinput\":" << GetSlotXInputPort(slot) << ",";
        ss << "\"handshake\":" << GetSlotHandshakePort(slot) << ",";
        ss << "\"video\":" << GetSlotVideoPort(slot) << ",";
        ss << "\"audio\":" << GetSlotAudioPort(slot) << ",";
        ss << "\"available\":" << (availableSlots[slot] ? "true" : "false");
        ss << "}";
    };
    if (linkplay == 0)
    {
        // old two-slot mode: slots 1 and 2
        if (availableSlots[1]) slotJson(1);
        if (availableSlots[2]) slotJson(2);
    }
    else if (linkplay >= 1 && linkplay <= 8 && availableSlots[linkplay])
        slotJson(linkplay);
    ss << "}";

    std::string path = "/hosts/" + key + ".json?auth=" + m_idToken;
    std::string resp = HttpPatch(dbHost, std::wstring(path.begin(), path.end()), ss.str(), m_idToken);
    printf("[Firebase] PATCH host=%s linkplay=%d\n", key.c_str(), linkplay);
    if (m_statusCb)
        m_statusCb("Firebase: Host/slot registered (PATCH) key=" + key);
    return !resp.empty();
}

bool FirebaseMatchingCpp::UnregisterHost(const std::string &hostId)
{
    printf("[Firebase] UnregisterHost called for hostId: %s\n", hostId.c_str());
    if (hostId.empty())
    {
        printf("[Firebase] UnregisterHost failed: hostId is empty\n");
        return false;
    }
    if (m_idToken.empty())
    {
        printf("[Firebase] UnregisterHost failed: m_idToken is empty\n");
        return false;
    }

    std::string path = "/hosts/" + hostId + ".json?auth=" + m_idToken;
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    printf("[Firebase] Sending DELETE request for host: %s\n", hostId.c_str());
    std::string res = HttpDelete(dbHost, wpath, m_idToken);
    printf("[Firebase] DELETE response: %s\n", res.c_str());
    
    m_currentHostId.clear();
    return true;
}

bool FirebaseMatchingCpp::PatchSlotAvailable(const std::string& hostId, int linkplay, bool available)
{
    if (hostId.empty() || m_idToken.empty()) return false;

    std::string slotKey = "slot" + std::to_string(linkplay);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + m_idToken;
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string body;
    if (available)
    {
        body = "{\"available\":true,\"audio\":" + std::to_string(GetSlotAudioPort(linkplay))
             + ",\"video\":" + std::to_string(GetSlotVideoPort(linkplay))
             + ",\"handshake\":" + std::to_string(GetSlotHandshakePort(linkplay))
             + ",\"xinput\":" + std::to_string(GetSlotXInputPort(linkplay)) + "}";
    }
    else
    {
        body = "{\"available\":false}";
    }
    HttpPatch(dbHost, wpath, body, m_idToken);
    printf("[Firebase] PatchSlotAvailable slot%d=%s\n", linkplay, available ? "true" : "false");
    return true;
}

bool FirebaseMatchingCpp::PatchSlotClientCount(const std::string& hostId, int slot, int count)
{
    if (hostId.empty() || m_idToken.empty()) return false;

    std::string slotKey = "slot" + std::to_string(slot);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + m_idToken;
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string body = "{\"clientCount\":" + std::to_string(count) + "}";
    HttpPatch(dbHost, wpath, body, m_idToken);
    printf("[Firebase] PatchSlotClientCount slot%d count=%d\n", slot, count);
    return true;
}

bool FirebaseMatchingCpp::PatchSlotPlayer(const std::string& hostId, int slot, const std::string& player)
{
    if (hostId.empty() || m_idToken.empty()) return false;

    std::string slotKey = "slot" + std::to_string(slot);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + m_idToken;
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string body = "{\"player\":\"" + EscapeJson(player) + "\"}";
    HttpPatch(dbHost, wpath, body, m_idToken);
    printf("[Firebase] PatchSlotPlayer slot%d player=%s\n", slot, player.c_str());
    return true;
}

bool FirebaseMatchingCpp::PatchSlotSpectator(const std::string& hostId, int slot, const std::string& spectator)
{
    if (hostId.empty() || m_idToken.empty()) return false;

    std::string slotKey = "slot" + std::to_string(slot);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + m_idToken;
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string body = "{\"spectator\":\"" + EscapeJson(spectator) + "\"}";
    HttpPatch(dbHost, wpath, body, m_idToken);
    printf("[Firebase] PatchSlotSpectator slot%d spectator=%s\n", slot, spectator.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Cleanup Stale Hosts (no updates for 10+ minutes)
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::CleanupStaleHosts()
{
    if (m_idToken.empty())
        return false;

    std::string path = "/hosts.json?auth=" + m_idToken;
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string resp = HttpGet(dbHost, wpath, m_idToken);
    if (resp.empty() || resp == "null")
        return true;

    long long now = GetUnixTimestamp();                                   // milliseconds
    long long threshold = now - (long long)(TIMEOUT_MINUTES * 60 * 1000); // sec -> milliseconds

    int depth = 0;
    std::string currentHostId = "";
    long long currentTimestamp = 0;

    for (size_t i = 0; i < resp.size(); i++)
    {
        char c = resp[i];
        if (c == '{')
        {
            depth++;
        }
        else if (c == '}')
        {
            if (depth == 2)
            {
                if (!currentHostId.empty() && currentTimestamp > 0 && currentTimestamp < threshold)
                {
                    std::string delPath = "/hosts/" + currentHostId + ".json?auth=" + m_idToken;
                    std::wstring wDelPath(delPath.begin(), delPath.end());
                    HttpDelete(dbHost, wDelPath, m_idToken);
                    printf("[Firebase] Stale host removed: %s (ts=%lld)\n", currentHostId.c_str(), currentTimestamp);
                }
                currentHostId = "";
                currentTimestamp = 0;
            }
            depth--;
        }
        else if (c == '"' && depth == 1)
        {
            size_t start = i + 1;
            size_t end = resp.find('"', start);
            if (end != std::string::npos)
            {
                currentHostId = resp.substr(start, end - start);
                i = end;
            }
        }
        else if (c == '"' && depth == 2)
        {
            size_t start = i + 1;
            size_t end = resp.find('"', start);
            if (end != std::string::npos)
            {
                std::string key = resp.substr(start, end - start);
                i = end;
                if (key == "timestamp")
                {
                    size_t colon = resp.find(':', i + 1);
                    if (colon != std::string::npos)
                    {
                        size_t valStart = colon + 1;
                        while (valStart < resp.size() && (resp[valStart] == ' ' || resp[valStart] == '\t'))
                            valStart++;
                        size_t valEnd = valStart;
                        while (valEnd < resp.size() && isdigit(resp[valEnd]))
                            valEnd++;
                        if (valEnd > valStart)
                        {
                            currentTimestamp = std::stoll(resp.substr(valStart, valEnd - valStart));
                            i = valEnd - 1;
                        }
                    }
                }
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// External IP Address Retrieval
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// P6: this PC's LAN address (connect a UDP socket towards a public address; nothing is sent)
// ---------------------------------------------------------------------------
std::string FirebaseMatchingCpp::GetLanIp()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    std::string result;
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET)
    {
        sockaddr_in to = {};
        to.sin_family = AF_INET;
        to.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &to.sin_addr);
        if (connect(s, (sockaddr *)&to, sizeof(to)) == 0)
        {
            sockaddr_in local = {};
            int len = sizeof(local);
            if (getsockname(s, (sockaddr *)&local, &len) == 0)
            {
                char buf[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
                result = buf;
            }
        }
        closesocket(s);
    }
    return result;
}

// ---------------------------------------------------------------------------
// P6: remove one slot (its server is closing), so the lobby stops offering it at once
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::RemoveSlot(const std::string &hostId, int slot)
{
    if (!m_initialized || hostId.empty() || slot < 1 || slot > 8)
        return false;
    std::string path = "/hosts/" + hostId + "/slot" + std::to_string(slot) + ".json?auth=" + m_idToken;
    HttpDelete(StreamSecrets::FirebaseDbHost(), std::wstring(path.begin(), path.end()), m_idToken);
    printf("[Firebase] RemoveSlot slot%d host=%s\n", slot, hostId.c_str());
    return true;
}

std::string FirebaseMatchingCpp::GetExternalIp()
{
    // GET https://api.ipify.org
    HINTERNET session = WinHttpOpen(L"Supermodel3/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return "";

    HINTERNET conn = WinHttpConnect(session, L"api.ipify.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        return "";
    }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", L"/", nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE);
    if (!req)
    {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return "";
    }

    std::string result;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
         WinHttpReceiveResponse(req, nullptr))
    {
        DWORD size = 0;
        char buf[64] = {};
        WinHttpQueryDataAvailable(req, &size);
        if (size > 0 && size < sizeof(buf) - 1)
        {
            DWORD read = 0;
            WinHttpReadData(req, buf, size, &read);
            result = std::string(buf, read);
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------
void FirebaseMatchingCpp::StartHeartbeat(const std::string &hostId)
{
    StopHeartbeat();
    m_heartbeatRunning.store(true);
    m_heartbeatThread = std::thread(&FirebaseMatchingCpp::HeartbeatThread, this, hostId);
}

void FirebaseMatchingCpp::StopHeartbeat()
{
    m_heartbeatRunning.store(false);
    if (m_heartbeatThread.joinable())
        m_heartbeatThread.join();
}

void FirebaseMatchingCpp::HeartbeatThread(std::string hostId)
{
    int heartbeatCount = 0;

    while (m_heartbeatRunning.load())
    {
        for (int i = 0; i < HEARTBEAT_INTERVAL_MS / 100 && m_heartbeatRunning.load(); i++)
            Sleep(100);

        if (!m_heartbeatRunning.load())
            break;

        // Re-acquire token every 30 minutes (expires in 1 hour)
        heartbeatCount++;
        if (heartbeatCount % 15 == 0)  // 2 minutes * 15 = 30 minutes
        {
            printf("[Firebase] Refreshing token...\n");
            if (!SignInAnonymously())
            {
                printf("[Firebase] Token refresh failed\n");
            }
            else
            {
                printf("[Firebase] Token refreshed OK\n");
            }
        }

        // Update timestamp
        std::string path = "/hosts/" + hostId + "/timestamp.json?auth=" + m_idToken;
        std::wstring wpath(path.begin(), path.end());
        std::wstring dbHost = StreamSecrets::FirebaseDbHost();

        long long ts = GetUnixTimestamp();
        std::string body = std::to_string(ts);
        HttpPut(dbHost, wpath, body, m_idToken);
        printf("[Firebase] Heartbeat updated: %s ts=%lld\n", hostId.c_str(), ts);
    }
}

// ---------------------------------------------------------------------------
// WinHTTP Helper Functions
// ---------------------------------------------------------------------------
std::string FirebaseMatchingCpp::HttpPost(const std::wstring &host, const std::wstring &path,
                                          const std::string &jsonBody, bool useHttps)
{
    HINTERNET session = WinHttpOpen(L"Supermodel3/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return "";

    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        return "";
    }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"POST", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req)
    {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return "";
    }

    std::wstring contentType = L"Content-Type: application/json\r\n";
    WinHttpAddRequestHeaders(req, contentType.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    std::string result;
    if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size(),
                           (DWORD)jsonBody.size(), 0) &&
        WinHttpReceiveResponse(req, nullptr))
    {
        DWORD size = 0;
        while (WinHttpQueryDataAvailable(req, &size) && size > 0)
        {
            std::string chunk(size, '\0');
            DWORD read = 0;
            WinHttpReadData(req, &chunk[0], size, &read);
            result.append(chunk, 0, read);
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

std::string FirebaseMatchingCpp::HttpGet(const std::wstring &host, const std::wstring &path,
                                         const std::string & /*authToken*/, bool useHttps)
{
    HINTERNET session = WinHttpOpen(L"Supermodel3/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return "";

    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        return "";
    }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    std::string result;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr))
    {
        DWORD size = 0;
        while (WinHttpQueryDataAvailable(req, &size) && size > 0)
        {
            std::string chunk(size, '\0');
            DWORD read = 0;
            WinHttpReadData(req, &chunk[0], size, &read);
            result.append(chunk, 0, read);
        }
    }

    if (req)
        WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

std::string FirebaseMatchingCpp::HttpPut(const std::wstring &host, const std::wstring &path,
                                         const std::string &jsonBody,
                                         const std::string & /*authToken*/, bool useHttps)
{
    HINTERNET session = WinHttpOpen(L"Supermodel3/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return "";

    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        return "";
    }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"PUT", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    std::string result;
    if (req)
    {
        std::wstring ct = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(req, ct.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size(),
                               (DWORD)jsonBody.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr))
        {
            DWORD size = 0;
            while (WinHttpQueryDataAvailable(req, &size) && size > 0)
            {
                std::string chunk(size, '\0');
                DWORD read = 0;
                WinHttpReadData(req, &chunk[0], size, &read);
                result.append(chunk, 0, read);
            }
        }
        WinHttpCloseHandle(req);
    }

    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

std::string FirebaseMatchingCpp::HttpPatch(const std::wstring &host, const std::wstring &path,
                                           const std::string &jsonBody,
                                           const std::string & /*authToken*/, bool useHttps)
{
    HINTERNET session = WinHttpOpen(L"Supermodel3/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return "";

    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        return "";
    }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"PATCH", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    std::string result;
    if (req)
    {
        std::wstring ct = L"Content-Type: application/json\r\n";
        WinHttpAddRequestHeaders(req, ct.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.size(),
                               (DWORD)jsonBody.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr))
        {
            DWORD size = 0;
            while (WinHttpQueryDataAvailable(req, &size) && size > 0)
            {
                std::string chunk(size, '\0');
                DWORD read = 0;
                WinHttpReadData(req, &chunk[0], size, &read);
                result.append(chunk, 0, read);
            }
        }
        WinHttpCloseHandle(req);
    }

    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

std::string FirebaseMatchingCpp::HttpDelete(const std::wstring &host, const std::wstring &path,
                                            const std::string & /*authToken*/, bool useHttps)
{
    HINTERNET session = WinHttpOpen(L"Supermodel3/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return "";

    INTERNET_PORT port = useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET conn = WinHttpConnect(session, host.c_str(), port, 0);
    if (!conn)
    {
        WinHttpCloseHandle(session);
        return "";
    }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"DELETE", path.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    std::string result;
    if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr))
    {
        result = "ok";
    }

    if (req)
        WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return result;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
long long FirebaseMatchingCpp::GetUnixTimestamp()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch())
         .count();
}

std::string FirebaseMatchingCpp::EscapeJson(const std::string &s)
{
    std::string out;
    for (char c : s)
    {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else
            out += c;
    }
    return out;
}

// IP address -> Firebase key conversion (replaces '.', '#', '$', '/', '[', ']' with '-')
std::string FirebaseMatchingCpp::SanitizeKeyFromIp(const std::string &ip)
{
    std::string key = ip;
    for (char &c : key)
    {
        if (c == '.' || c == '#' || c == '$' ||
            c == '/' || c == '[' || c == ']')
            c = '-';
    }
    return key;
}

std::string FirebaseMatchingCpp::MakeHostJson(const HostInfoCpp &info)
{
    std::ostringstream ss;
    ss << "{";
    ss << "\"timestamp\":" << info.timestamp << ",";
    ss << "\"ip\":\"" << EscapeJson(info.ip) << "\"";
    if (!info.lanip.empty())
        ss << ",\"lanip\":\"" << EscapeJson(info.lanip) << "\"";

    if (!info.gametitle.empty())
        ss << ",\"gametitle\":\"" << EscapeJson(info.gametitle) << "\"";
    if (!info.servername.empty())
        ss << ",\"servername\":\"" << EscapeJson(info.servername) << "\"";

    for (const auto &kv : info.slots)
    {
        const SlotInfoCpp &s = kv.second;
        ss << ",\"slot" << kv.first << "\":{";
        ss << "\"xinput\":" << s.xinputPort << ",";
        ss << "\"handshake\":" << s.handshakePort << ",";
        ss << "\"video\":" << s.videoPort << ",";
        ss << "\"audio\":" << s.audioPort << ",";
        ss << "\"available\":" << (s.available ? "true" : "false");
        ss << "}";
    }

    ss << "}";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Lobby (P6): read all hosts
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::FetchHosts(std::vector<LobbyHostCpp> &out)
{
    out.clear();
    if (!m_initialized)
        return false;
    std::string path = "/hosts.json?auth=" + m_idToken;
    std::string resp = HttpGet(StreamSecrets::FirebaseDbHost(), std::wstring(path.begin(), path.end()), m_idToken);
    if (resp.empty())
        return false;
    MiniJson::Value root;
    if (!MiniJson::Parse(resp, root))
    {
        printf("[Firebase] FetchHosts: could not read the answer\n");
        return false;
    }
    if (root.type != MiniJson::Value::Object)
        return true;   // "null": no hosts

    long long now = GetUnixTimestamp();
    const long long staleMs = 10LL * 60 * 1000;   // LB-06: hide hosts not updated for 10 minutes
    for (const auto &kv : root.obj)
    {
        const MiniJson::Value &h = kv.second;
        if (h.type != MiniJson::Value::Object)
            continue;
        LobbyHostCpp host;
        host.key = kv.first;
        host.ip = h.GetString("ip");
        host.lanip = h.GetString("lanip");
        host.servername = h.GetString("servername");
        host.gametitle = h.GetString("gametitle");
        host.timestamp = (long long) h.GetNumber("timestamp");
        if (host.ip.empty() || host.timestamp <= 0 || now - host.timestamp > staleMs)
            continue;
        bool anySlot = false;
        for (int s = 1; s <= 8; s++)
        {
            const MiniJson::Value *slot = h.Get("slot" + std::to_string(s));
            if (!slot || slot->type != MiniJson::Value::Object)
                continue;
            host.slots[s].present = true;
            host.slots[s].available = slot->GetBool("available");
            host.slots[s].player = slot->GetString("player");
            anySlot = true;
        }
        if (anySlot)
            out.push_back(host);
    }
    return true;
}

#endif // SUPERMODEL_WIN32
