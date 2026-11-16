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
#include "Stream/Http.h"
#include "Stream/StreamDpapi.h"
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
    std::lock_guard<std::mutex> lock(m_tokenLock);
    m_idToken.clear();
}

std::string FirebaseMatchingCpp::Token()
{
    std::lock_guard<std::mutex> lock(m_tokenLock);
    return m_idToken;
}

// ---------------------------------------------------------------------------
// Initialization (Anonymous Auth)
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::Initialize(FirebaseStatusCallback statusCb)
{
    m_statusCb = statusCb;
    if (m_statusCb)
        m_statusCb("Firebase: Initializing...");

    // keys are embedded at build time (Stream/Secrets.h); without them Firebase stays off
    if (!StreamSecrets::HasFirebase())
    {
        if (m_statusCb)
            m_statusCb("Firebase: off (this build has no Firebase keys)");
        return false;
    }

    // 11/16: a host slot keeps its Firebase session (DPAPI-encrypted file) so that, started again (also after a
    // crash), it is the same anonymous user and may write its own slot again (the rules allow only the owner).
    bool resumed = false;
    std::string saved;
    if (!m_sessionFile.empty() && StreamDpapi::ReadFile(m_sessionFile.c_str(), saved))
    {
        std::string uid, refresh;
        size_t p = 0;
        while (p < saved.size())
        {
            size_t nl = saved.find('\n', p);
            std::string line = saved.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
            p = (nl == std::string::npos) ? saved.size() : nl + 1;
            if (line.compare(0, 4, "uid=") == 0) uid = line.substr(4);
            else if (line.compare(0, 8, "refresh=") == 0) refresh = line.substr(8);
        }
        if (!uid.empty() && !refresh.empty())
        {
            {
                std::lock_guard<std::mutex> lock(m_tokenLock);
                m_uid = uid;
                m_refreshToken = refresh;
            }
            resumed = RefreshIdToken();
        }
    }
    if (!resumed && !SignInAnonymously())
    {
        if (m_statusCb)
            m_statusCb("Firebase: Fault");
        return false;
    }
    SaveSession();

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

    // 11/16: keep the uid (localId) and the refresh token too - the uid is the owner of this server's slots
    MiniJson::Value v;
    if (!MiniJson::Parse(response, v) || v.GetString("idToken").empty())
        return false;
    std::lock_guard<std::mutex> lock(m_tokenLock);
    m_idToken = v.GetString("idToken");
    m_refreshToken = v.GetString("refreshToken");
    m_uid = v.GetString("localId");
    return true;
}

// 11/16: a new ID token for the SAME anonymous user (signing up again would give a new uid, and the database
// rules would then refuse writes to the slots this server owns).
bool FirebaseMatchingCpp::RefreshIdToken()
{
    std::string refresh;
    {
        std::lock_guard<std::mutex> lock(m_tokenLock);
        refresh = m_refreshToken;
    }
    if (refresh.empty())
        return false;
    std::string body;
    int st = StreamHttp::Request(L"securetoken.googleapis.com", L"POST",
                                 "/v1/token?key=" + StreamSecrets::Get().firebaseApiKey,
                                 "Content-Type: application/x-www-form-urlencoded\r\n",
                                 "grant_type=refresh_token&refresh_token=" + StreamHttp::UrlEncode(refresh), &body);
    MiniJson::Value v;
    if (st != 200 || !MiniJson::Parse(body, v) || v.GetString("id_token").empty())
        return false;
    {
        std::lock_guard<std::mutex> lock(m_tokenLock);
        m_idToken = v.GetString("id_token");
        if (!v.GetString("refresh_token").empty())
            m_refreshToken = v.GetString("refresh_token");
        if (!v.GetString("user_id").empty())
            m_uid = v.GetString("user_id");
    }
    SaveSession();
    return true;
}

void FirebaseMatchingCpp::SaveSession()
{
    if (m_sessionFile.empty())
        return;
    std::string plain;
    {
        std::lock_guard<std::mutex> lock(m_tokenLock);
        if (m_uid.empty() || m_refreshToken.empty())
            return;
        plain = "uid=" + m_uid + "\nrefresh=" + m_refreshToken + "\n";
    }
    StreamDpapi::WriteFile(m_sessionFile.c_str(), plain);
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
    // 11/16: everything lives in this server's OWN slot (hosts/<ip>/slotN), with owner = our anonymous uid.
    // The database rules let only the owner write a slot (or anyone delete it once it is 10 minutes stale),
    // so other users cannot change or remove it. The lobby groups the slots of one <ip> into one host.
    // The timestamp is the server's clock ({".sv":"timestamp"}), the one the rules compare with.
    std::string key = SanitizeKeyFromIp(externalIp);
    m_currentHostId = key;
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();
    std::string uid;
    {
        std::lock_guard<std::mutex> lock(m_tokenLock);
        uid = m_uid;
    }
    std::string lan = GetLanIp();

    auto registerSlot = [&](int slot) -> bool {
        std::ostringstream ss;
        ss << "{\"owner\":\"" << EscapeJson(uid) << "\"";
        ss << ",\"timestamp\":{\".sv\":\"timestamp\"}";
        ss << ",\"ip\":\"" << EscapeJson(externalIp) << "\"";
        if (!lan.empty())
            ss << ",\"lanip\":\"" << EscapeJson(lan) << "\"";
        if (!gameTitle.empty())
            ss << ",\"gametitle\":\"" << EscapeJson(gameTitle) << "\"";
        if (!serverName.empty())
            ss << ",\"servername\":\"" << EscapeJson(serverName.substr(0, 48)) << "\"";
        ss << ",\"xinput\":" << GetSlotXInputPort(slot);
        ss << ",\"handshake\":" << GetSlotHandshakePort(slot);
        ss << ",\"video\":" << GetSlotVideoPort(slot);
        ss << ",\"audio\":" << GetSlotAudioPort(slot);
        ss << ",\"available\":" << (availableSlots[slot] ? "true" : "false");
        if (m_locked)
            ss << ",\"locked\":true";   // SV-18 (only written when set, so public hosts work with older rules)
        ss << "}";
        std::string path = "/hosts/" + key + "/slot" + std::to_string(slot) + ".json?auth=" + Token();
        std::string resp = HttpPatch(dbHost, std::wstring(path.begin(), path.end()), ss.str(), Token());
        bool ok = !resp.empty() && resp.find("\"error\"") == std::string::npos;
        printf("[Firebase] register host=%s slot%d %s\n", key.c_str(), slot, ok ? "ok" : ("REFUSED: " + resp).c_str());
        return ok;
    };
    bool ok = true;
    if (linkplay == 0)
    {
        // old two-slot mode: slots 1 and 2
        if (availableSlots[1]) ok &= registerSlot(1);
        if (availableSlots[2]) ok &= registerSlot(2);
    }
    else if (linkplay >= 1 && linkplay <= 8 && availableSlots[linkplay])
        ok = registerSlot(linkplay);
    if (m_statusCb)
        m_statusCb(std::string("Firebase: slot ") + (ok ? "registered" : "NOT registered") + " key=" + key);
    return ok;
}

bool FirebaseMatchingCpp::UnregisterHost(const std::string &hostId)
{
    printf("[Firebase] UnregisterHost called for hostId: %s\n", hostId.c_str());
    if (hostId.empty())
    {
        printf("[Firebase] UnregisterHost failed: hostId is empty\n");
        return false;
    }
    if (Token().empty())
    {
        printf("[Firebase] UnregisterHost failed: m_idToken is empty\n");
        return false;
    }

    std::string path = "/hosts/" + hostId + ".json?auth=" + Token();
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    printf("[Firebase] Sending DELETE request for host: %s\n", hostId.c_str());
    std::string res = HttpDelete(dbHost, wpath, Token());
    printf("[Firebase] DELETE response: %s\n", res.c_str());
    
    m_currentHostId.clear();
    return true;
}

bool FirebaseMatchingCpp::PatchSlotAvailable(const std::string& hostId, int linkplay, bool available)
{
    if (hostId.empty() || Token().empty()) return false;

    std::string slotKey = "slot" + std::to_string(linkplay);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + Token();
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
    HttpPatch(dbHost, wpath, body, Token());
    printf("[Firebase] PatchSlotAvailable slot%d=%s\n", linkplay, available ? "true" : "false");
    return true;
}

bool FirebaseMatchingCpp::PatchSlotClientCount(const std::string& hostId, int slot, int count)
{
    if (hostId.empty() || Token().empty()) return false;

    std::string slotKey = "slot" + std::to_string(slot);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + Token();
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string body = "{\"clientCount\":" + std::to_string(count) + "}";
    HttpPatch(dbHost, wpath, body, Token());
    printf("[Firebase] PatchSlotClientCount slot%d count=%d\n", slot, count);
    return true;
}

bool FirebaseMatchingCpp::PatchSlotPlayer(const std::string& hostId, int slot, const std::string& player)
{
    if (hostId.empty() || Token().empty()) return false;

    std::string slotKey = "slot" + std::to_string(slot);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + Token();
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string body = "{\"player\":\"" + EscapeJson(player) + "\"}";
    HttpPatch(dbHost, wpath, body, Token());
    printf("[Firebase] PatchSlotPlayer slot%d player=%s\n", slot, player.c_str());
    return true;
}

bool FirebaseMatchingCpp::PatchSlotSpectator(const std::string& hostId, int slot, const std::string& spectator)
{
    if (hostId.empty() || Token().empty()) return false;

    std::string slotKey = "slot" + std::to_string(slot);
    std::string path = "/hosts/" + hostId + "/" + slotKey + ".json?auth=" + Token();
    std::wstring wpath(path.begin(), path.end());
    std::wstring dbHost = StreamSecrets::FirebaseDbHost();

    std::string body = "{\"spectator\":\"" + EscapeJson(spectator) + "\"}";
    HttpPatch(dbHost, wpath, body, Token());
    printf("[Firebase] PatchSlotSpectator slot%d spectator=%s\n", slot, spectator.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Cleanup stale slots (no heartbeat for 10+ minutes). 11/16: per slot - the database rules allow anybody to
// delete a slot only when its (server-clock) timestamp is that old; a host disappears with its last slot.
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::CleanupStaleHosts()
{
    if (Token().empty())
        return false;
    std::string path = "/hosts.json?auth=" + Token();
    std::string resp = HttpGet(StreamSecrets::FirebaseDbHost(), std::wstring(path.begin(), path.end()), Token());
    MiniJson::Value root;
    if (resp.empty() || !MiniJson::Parse(resp, root) || root.type != MiniJson::Value::Object)
        return true;
    long long threshold = GetUnixTimestamp() - (long long) TIMEOUT_MINUTES * 60 * 1000;
    for (const auto &kv : root.obj)
    {
        if (kv.second.type != MiniJson::Value::Object)
            continue;
        for (int s = 1; s <= 8; s++)
        {
            const MiniJson::Value *slot = kv.second.Get("slot" + std::to_string(s));
            if (!slot || slot->type != MiniJson::Value::Object)
                continue;
            long long ts = (long long) slot->GetNumber("timestamp");
            if (ts > 0 && ts < threshold)
            {
                std::string del = "/hosts/" + kv.first + "/slot" + std::to_string(s) + ".json?auth=" + Token();
                HttpDelete(StreamSecrets::FirebaseDbHost(), std::wstring(del.begin(), del.end()), Token());
                printf("[Firebase] stale slot removed: %s slot%d\n", kv.first.c_str(), s);
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
    std::string path = "/hosts/" + hostId + "/slot" + std::to_string(slot) + ".json?auth=" + Token();
    HttpDelete(StreamSecrets::FirebaseDbHost(), std::wstring(path.begin(), path.end()), Token());
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
void FirebaseMatchingCpp::StartHeartbeat(const std::string &hostId, const std::vector<int> &slots)
{
    StopHeartbeat();
    m_heartbeatRunning = true;
    m_heartbeatThread = std::thread(&FirebaseMatchingCpp::HeartbeatThread, this, hostId, slots);
}

void FirebaseMatchingCpp::StopHeartbeat()
{
    m_heartbeatRunning = false;
    if (m_heartbeatThread.joinable())
        m_heartbeatThread.join();
}

void FirebaseMatchingCpp::HeartbeatThread(std::string hostId, std::vector<int> slots)
{
    int heartbeatCount = 0;
    while (m_heartbeatRunning.load())
    {
        for (int i = 0; i < HEARTBEAT_INTERVAL_MS / 100 && m_heartbeatRunning.load(); i++)
            Sleep(100);
        if (!m_heartbeatRunning.load())
            break;

        // ID tokens expire after 1 hour: a new one every 30 minutes, for the same uid (11/16: refresh token)
        if (++heartbeatCount % 15 == 0)
            printf("[Firebase] token refresh %s\n", RefreshIdToken() ? "ok" : "FAILED");

        // timestamp of our own slot(s), server clock (the rules and the stale cleanup compare with it)
        for (int slot : slots)
        {
            std::string path = "/hosts/" + hostId + "/slot" + std::to_string(slot) + ".json?auth=" + Token();
            std::string resp = HttpPatch(StreamSecrets::FirebaseDbHost(), std::wstring(path.begin(), path.end()),
                                         "{\"timestamp\":{\".sv\":\"timestamp\"}}", Token());
            if (resp.find("\"error\"") != std::string::npos)
                printf("[Firebase] heartbeat slot%d refused: %s\n", slot, resp.c_str());
        }
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
    std::string path = "/hosts.json?auth=" + Token();
    std::string resp = HttpGet(StreamSecrets::FirebaseDbHost(), std::wstring(path.begin(), path.end()), Token());
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
        // 11/16: every slot carries the host fields and its own timestamp; slots not updated for 10 minutes are
        // left out, and the host fields come from its most recently updated slot.
        LobbyHostCpp host;
        host.key = kv.first;
        bool anySlot = false;
        for (int s = 1; s <= 8; s++)
        {
            const MiniJson::Value *slot = h.Get("slot" + std::to_string(s));
            if (!slot || slot->type != MiniJson::Value::Object)
                continue;
            long long ts = (long long) slot->GetNumber("timestamp");
            if (ts <= 0 || now - ts > staleMs || slot->GetString("ip").empty())
                continue;
            host.slots[s].present = true;
            host.slots[s].available = slot->GetBool("available");
            host.slots[s].player = slot->GetString("player");
            host.slots[s].locked = slot->GetBool("locked");
            if (host.slots[s].locked)
                host.locked = true;
            anySlot = true;
            if (ts > host.timestamp)
            {
                host.timestamp = ts;
                host.ip = slot->GetString("ip");
                host.lanip = slot->GetString("lanip");
                host.servername = slot->GetString("servername");
                host.gametitle = slot->GetString("gametitle");
            }
        }
        if (anySlot)
            out.push_back(host);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 11/16: newest main5 build (/main5, written by the owner in the console)
// ---------------------------------------------------------------------------
bool FirebaseMatchingCpp::FetchMain5Version(long long &supermodel, std::string &url)
{
    supermodel = 0;
    url.clear();
    if (!m_initialized)
        return false;
    std::string path = "/main5.json?auth=" + Token();
    std::string resp = HttpGet(StreamSecrets::FirebaseDbHost(), std::wstring(path.begin(), path.end()), Token());
    if (resp.empty())
        return false;
    MiniJson::Value root;
    if (!MiniJson::Parse(resp, root))
        return false;
    if (root.type != MiniJson::Value::Object)
        return true;   // "null": not published yet
    supermodel = (long long) root.GetNumber("supermodel");
    if (supermodel == 0)
        supermodel = atoll(root.GetString("supermodel").c_str());   // also accept "20261116" typed as a string
    url = root.GetString("url");
    return true;
}

#endif // SUPERMODEL_WIN32
