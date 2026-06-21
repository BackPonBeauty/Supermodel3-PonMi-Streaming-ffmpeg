/**
 * FirebaseMatchingCpp.cpp
 *
 * Firebase Realtime Database REST API implementation (using WinHTTP)
 */

#ifdef SUPERMODEL_WIN32

#include "FirebaseMatchingCpp.h"
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
        return base + (slot - 1) * 4;
    }
    else
    {
        return base + (slot - currentLinkPlay) * 4;
    }
}

int FirebaseMatchingCpp::GetSlotAudioPort(int slot)
{
    int base = GetPortConfig("AudioPort", "Audioport", 55003);
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
    if (!m_currentHostId.empty())
        UnregisterHost(m_currentHostId);
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
    path += API_KEY; 

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
                                       const bool availableSlots[5],
                                       int linkplay,
                                       const std::string &gameTitle,
                                       const std::string &serverName)
{
    // Key = Sanitize IP (replace dot with '-')
    std::string key = SanitizeKeyFromIp(externalIp);
    m_currentHostId = key;

    std::wstring dbHost = L"supermodel3-8343f-default-rtdb.asia-southeast1.firebasedatabase.app";

    // Verify record existence via GET
    std::string getPath = "/hosts/" + key + ".json?auth=" + m_idToken;
    std::string existing = HttpGet(dbHost, std::wstring(getPath.begin(), getPath.end()), m_idToken);
    bool exists = !existing.empty() && existing != "null";

    if (!exists)
    {
        // ----- PUT: Create new record -----
        HostInfoCpp info;
        info.timestamp = GetUnixTimestamp();
        info.ip = externalIp;
        info.gametitle = gameTitle; // ROM name (e.g., spikeofe)
        info.servername = serverName; // Register serverName for any linkplay number

        for (int slot = 1; slot <= 4; slot++)
        {
            bool isSlotAvailable = false;
            if (linkplay == 0)
            {
                // LinkPlay=0 opens both Slot 1 and Slot 2
                isSlotAvailable = (slot == 1 || slot == 2) ? availableSlots[slot] : false;
            }
            else
            {
                isSlotAvailable = (slot == linkplay) ? availableSlots[slot] : false;
            }

            // ストリーミングが有効(available=true)なスロットのみ、Firebase上にスロットキーを作成
            if (isSlotAvailable)
            {
                SlotInfoCpp s;
                s.xinputPort = GetSlotXInputPort(slot);
                s.handshakePort = GetSlotHandshakePort(slot);
                s.videoPort = GetSlotVideoPort(slot);
                s.audioPort = GetSlotAudioPort(slot);
                s.available = true;
                info.slots[slot] = s;
            }
        }

        std::string putPath = "/hosts/" + key + ".json?auth=" + m_idToken;
        std::string body = MakeHostJson(info);
        std::string resp = HttpPut(dbHost, std::wstring(putPath.begin(), putPath.end()), body, m_idToken);

        printf("[Firebase] PUT (new record) key=%s linkplay=%d\n", key.c_str(), linkplay);
        if (m_statusCb)
            m_statusCb("Firebase: Host registered (PUT) key=" + key);
        return !resp.empty();
    }
    else
    {
        // PATCH: Update only current slot available state and timestamp
        long long ts = GetUnixTimestamp();

        if (linkplay == 0)
        {
            // LinkPlay=0 patches both Slot 1 and Slot 2
            for (int slot = 1; slot <= 2; slot++)
            {
                std::string slotKey = "slot" + std::to_string(slot);
                std::string patchPath = "/hosts/" + key + "/" + slotKey + ".json?auth=" + m_idToken;
                std::ostringstream ss;
                if (availableSlots[slot])
                {
                    ss << "{\"available\":true,\"audio\":" << GetSlotAudioPort(slot)
                       << ",\"video\":" << GetSlotVideoPort(slot)
                       << ",\"handshake\":" << GetSlotHandshakePort(slot)
                       << ",\"xinput\":" << GetSlotXInputPort(slot) << "}";
                }
                else
                {
                    ss << "{\"available\":false}";
                }
                HttpPatch(dbHost, std::wstring(patchPath.begin(), patchPath.end()), ss.str(), m_idToken);
            }
            printf("[Firebase] PATCH slot1 and slot2 available key=%s\n", key.c_str());
        }
        else
        {
            std::string slotKey = "slot" + std::to_string(linkplay);
            std::string patchPath = "/hosts/" + key + "/" + slotKey + ".json?auth=" + m_idToken;
            std::ostringstream ss;
            if (availableSlots[linkplay])
            {
                ss << "{\"available\":true,\"audio\":" << GetSlotAudioPort(linkplay)
                   << ",\"video\":" << GetSlotVideoPort(linkplay)
                   << ",\"handshake\":" << GetSlotHandshakePort(linkplay)
                   << ",\"xinput\":" << GetSlotXInputPort(linkplay) << "}";
            }
            else
            {
                ss << "{\"available\":false}";
            }
            HttpPatch(dbHost, std::wstring(patchPath.begin(), patchPath.end()), ss.str(), m_idToken);
            printf("[Firebase] PATCH slot%d available=%s key=%s\n",
                   linkplay, availableSlots[linkplay] ? "true" : "false", key.c_str());
        }

        // Put timestamp separately
        std::string tsPath = "/hosts/" + key + "/timestamp.json?auth=" + m_idToken;
        HttpPut(dbHost, std::wstring(tsPath.begin(), tsPath.end()), std::to_string(ts), m_idToken);

        if (m_statusCb)
            m_statusCb("Firebase: Slots updated (PATCH)");
        return true;
    }
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
    std::wstring dbHost = L"supermodel3-8343f-default-rtdb.asia-southeast1.firebasedatabase.app";

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
    std::wstring dbHost = L"supermodel3-8343f-default-rtdb.asia-southeast1.firebasedatabase.app";

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

// ---------------------------------------------------------------------------
// Cleanup Stale Hosts (no updates for 10+ minutes)
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::CleanupStaleHosts()
{
    if (m_idToken.empty())
        return false;

    std::string path = "/hosts.json?auth=" + m_idToken;
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = L"supermodel3-8343f-default-rtdb.asia-southeast1.firebasedatabase.app";

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
        std::wstring dbHost = L"supermodel3-8343f-default-rtdb.asia-southeast1.firebasedatabase.app";

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

#endif // SUPERMODEL_WIN32
