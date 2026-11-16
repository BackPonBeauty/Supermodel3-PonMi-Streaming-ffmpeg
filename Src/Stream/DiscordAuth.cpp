/*
 * DiscordAuth.cpp
 *
 * Streaming (P6b, 2026-11-16): Discord login (PKCE) for the client and the membership check for the host.
 * See DiscordAuth.h.
 */

#ifdef SUPERMODEL_WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include "DiscordAuth.h"
#include "Http.h"
#include "MiniJson.h"
#include "Secrets.h"
#include "StreamDpapi.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <cstring>
#include <iterator>
#include <shellapi.h>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace DiscordAuth
{
  static const char *kLoginFile = "Config/DiscordLogin.ini";
  static const char *kScopes = "identify guilds.members.read";

  // ---------------------------------------------------------------------------------------------------------
  // Saved login
  // ---------------------------------------------------------------------------------------------------------
  struct Saved
  {
    std::string accessToken;
    std::string refreshToken;
    long long   expiresAt = 0;   // unix seconds
    std::string userId;
    std::string userName;
  };

  static std::mutex          s_lock;
  static Saved               s_saved;
  static bool                s_loaded = false;
  static std::atomic<State>  s_state{State::LoggedOut};
  static std::string         s_error;
  static std::thread         s_loginThread;
  static std::atomic<bool>   s_cancel{false};

  static std::string Trim(const std::string &s)
  {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
  }

  // 11/16: the login file is encrypted with Windows DPAPI (CryptProtectData, this Windows user only), stored as
  // "DPAPI:<base64>". Another user account, or the file copied to another PC, cannot read it. An old plain file
  // is still read once and then saved encrypted.
  static const char *kDpapiPrefix = StreamDpapi::kPrefix;
  static void SaveLocked();
  static std::string Protect(const std::string &plain) { return StreamDpapi::Protect(plain); }
  static bool Unprotect(const std::string &text, std::string &plain) { return StreamDpapi::Unprotect(text, plain); }

  static void LoadLocked()
  {
    if (s_loaded)
      return;
    s_loaded = true;
    std::ifstream ifs(kLoginFile, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    bool wasPlain = false;
    if (text.compare(0, strlen(kDpapiPrefix), kDpapiPrefix) == 0)
    {
      std::string plain;
      if (!Unprotect(text, plain))
      {
        printf("[Discord] login file could not be decrypted (other user / PC?) -> please log in again\n");
        return;
      }
      text = plain;
    }
    else
      wasPlain = !text.empty();
    size_t p = 0;
    while (p < text.size())
    {
      size_t nl = text.find('\n', p);
      std::string line = text.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
      p = (nl == std::string::npos) ? text.size() : nl + 1;
      size_t eq = line.find('=');
      if (eq == std::string::npos || (!line.empty() && line[0] == ';'))
        continue;
      std::string k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
      if (k == "AccessToken")       s_saved.accessToken = v;
      else if (k == "RefreshToken") s_saved.refreshToken = v;
      else if (k == "ExpiresAt")    s_saved.expiresAt = atoll(v.c_str());
      else if (k == "UserId")       s_saved.userId = v;
      else if (k == "UserName")     s_saved.userName = v;
    }
    if (!s_saved.refreshToken.empty() || !s_saved.accessToken.empty())
    {
      s_state = State::LoggedIn;
      if (wasPlain)
        SaveLocked();   // upgrade an old plain file to the encrypted form
    }
  }

  static void SaveLocked()
  {
    std::string plain = "AccessToken = " + s_saved.accessToken + "\n" +
                        "RefreshToken = " + s_saved.refreshToken + "\n" +
                        "ExpiresAt = " + std::to_string(s_saved.expiresAt) + "\n" +
                        "UserId = " + s_saved.userId + "\n" +
                        "UserName = " + s_saved.userName + "\n";
    std::string enc = Protect(plain);
    if (enc.empty())
    {
      printf("[Discord] could not encrypt the login file; not saved (log in again next time)\n");
      std::remove(kLoginFile);
      return;
    }
    std::ofstream ofs(kLoginFile, std::ios::trunc | std::ios::binary);
    ofs << enc << "\n";
  }

  // ---------------------------------------------------------------------------------------------------------
  // PKCE helpers
  // ---------------------------------------------------------------------------------------------------------
  static std::string Base64Url(const unsigned char *p, size_t n)
  {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string o;
    for (size_t i = 0; i < n; i += 3)
    {
      unsigned v = p[i] << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
      o += t[(v >> 18) & 63];
      o += t[(v >> 12) & 63];
      if (i + 1 < n) o += t[(v >> 6) & 63];
      if (i + 2 < n) o += t[v & 63];
    }
    return o;   // no padding (RFC 7636)
  }

  static std::string RandomString(size_t bytes)
  {
    std::vector<unsigned char> b(bytes);
    BCryptGenRandom(nullptr, b.data(), (ULONG) b.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return Base64Url(b.data(), b.size());
  }

  static std::string Sha256Base64Url(const std::string &s)
  {
    unsigned char hash[32] = {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
    {
      if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0)
      {
        BCryptHashData(h, (PUCHAR) s.data(), (ULONG) s.size(), 0);
        BCryptFinishHash(h, hash, sizeof(hash), 0);
        BCryptDestroyHash(h);
      }
      BCryptCloseAlgorithmProvider(alg, 0);
    }
    return Base64Url(hash, sizeof(hash));
  }

  static std::string RedirectUri()
  {
    return "http://127.0.0.1:" + std::to_string(StreamSecrets::Get().discordRedirectPort) + "/callback";
  }

  static std::string QueryParam(const std::string &query, const std::string &key)
  {
    size_t p = 0;
    while (p <= query.size())
    {
      size_t amp = query.find('&', p);
      std::string part = query.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
      size_t eq = part.find('=');
      if (eq != std::string::npos && part.substr(0, eq) == key)
      {
        std::string v = part.substr(eq + 1), o;
        for (size_t i = 0; i < v.size(); i++)
        {
          if (v[i] == '%' && i + 2 < v.size()) { o += (char) strtol(v.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
          else if (v[i] == '+') o += ' ';
          else o += v[i];
        }
        return o;
      }
      if (amp == std::string::npos)
        break;
      p = amp + 1;
    }
    return "";
  }

  // ---------------------------------------------------------------------------------------------------------
  // Token endpoint and /users/@me
  // ---------------------------------------------------------------------------------------------------------
  // grant: "authorization_code" (code + verifier) or "refresh_token" (code = refresh token)
  static bool TokenRequest(const std::string &grant, const std::string &code, const std::string &verifier, Saved &out, std::string &err)
  {
    const StreamSecrets::Keys &k = StreamSecrets::Get();
    std::string form = "client_id=" + StreamHttp::UrlEncode(k.discordClientId) + "&grant_type=" + grant;
    if (grant == "authorization_code")
      form += "&code=" + StreamHttp::UrlEncode(code) + "&redirect_uri=" + StreamHttp::UrlEncode(RedirectUri()) +
              "&code_verifier=" + StreamHttp::UrlEncode(verifier);
    else
      form += "&refresh_token=" + StreamHttp::UrlEncode(code);
    std::string body;
    int st = StreamHttp::Request(L"discord.com", L"POST", "/api/oauth2/token",
                                 "Content-Type: application/x-www-form-urlencoded\r\n", form, &body);
    MiniJson::Value v;
    if (st != 200 || !MiniJson::Parse(body, v) || v.GetString("access_token").empty())
    {
      std::string e = MiniJson::Parse(body, v) ? v.GetString("error") : std::string();
      err = "token request failed (HTTP " + std::to_string(st) + (e.empty() ? "" : ", " + e) + ")";
      return false;
    }
    out.accessToken = v.GetString("access_token");
    std::string rt = v.GetString("refresh_token");
    if (!rt.empty())
      out.refreshToken = rt;
    long long expiresIn = (long long) v.GetNumber("expires_in");
    out.expiresAt = (long long) time(nullptr) + (expiresIn > 0 ? expiresIn : 3600);
    return true;
  }

  static std::string DisplayName(const MiniJson::Value &user)
  {
    std::string n = user.GetString("global_name");
    return n.empty() ? user.GetString("username") : n;
  }

  static bool FetchMe(Saved &s, std::string &err)
  {
    std::string body;
    int st = StreamHttp::Request(L"discord.com", L"GET", "/api/v10/users/@me",
                                 "Authorization: Bearer " + s.accessToken + "\r\n", "", &body);
    MiniJson::Value v;
    if (st != 200 || !MiniJson::Parse(body, v))
    {
      err = "could not read the Discord user (HTTP " + std::to_string(st) + ")";
      return false;
    }
    s.userId = v.GetString("id");
    s.userName = DisplayName(v);
    return true;
  }

  // ---------------------------------------------------------------------------------------------------------
  // Loopback receiver: waits for GET /callback?code=...&state=...
  // ---------------------------------------------------------------------------------------------------------
  static bool WaitForCallback(SOCKET ls, const std::string &state, std::string &code, std::string &err)
  {
    DWORD start = GetTickCount();
    while (!s_cancel.load())
    {
      if (GetTickCount() - start > 180000)
      {
        err = "no answer from the browser (3 minutes)";
        return false;
      }
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(ls, &rd);
      timeval tv = { 0, 200000 };
      if (select(0, &rd, nullptr, nullptr, &tv) <= 0)
        continue;
      SOCKET c = accept(ls, nullptr, nullptr);
      if (c == INVALID_SOCKET)
        continue;
      DWORD rto = 3000;
      setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *) &rto, sizeof(rto));
      std::string req;
      char buf[2048];
      int n;
      while (req.find("\r\n\r\n") == std::string::npos && (n = recv(c, buf, sizeof(buf), 0)) > 0 && req.size() < 16384)
        req.append(buf, n);

      // "GET /callback?code=...&state=... HTTP/1.1"
      std::string path;
      if (req.compare(0, 4, "GET ") == 0)
        path = req.substr(4, req.find(' ', 4) - 4);
      bool isCallback = path.compare(0, 9, "/callback") == 0;
      std::string query = (path.find('?') != std::string::npos) ? path.substr(path.find('?') + 1) : "";
      std::string gotCode = QueryParam(query, "code"), gotState = QueryParam(query, "state"), gotErr = QueryParam(query, "error");

      bool ok = isCallback && !gotCode.empty() && gotState == state;
      std::string page = ok ? "<html><head><meta charset=\"utf-8\"></head><body style=\"font-family:sans-serif\">"
                              "<h2>Discord login OK</h2><p>You can close this tab and go back to Supermodel.</p></body></html>"
                            : "<html><head><meta charset=\"utf-8\"></head><body style=\"font-family:sans-serif\">"
                              "<h2>Discord login failed</h2><p>Please go back to Supermodel and try again.</p></body></html>";
      std::string resp = std::string(isCallback ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found") +
                         "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(page.size()) +
                         "\r\nConnection: close\r\n\r\n" + page;
      send(c, resp.data(), (int) resp.size(), 0);
      shutdown(c, SD_SEND);
      closesocket(c);

      if (!isCallback)
        continue;   // favicon etc.
      if (ok)
      {
        code = gotCode;
        return true;
      }
      err = !gotErr.empty() ? "Discord said: " + gotErr : (gotState != state ? "state did not match" : "no code");
      return false;
    }
    err = "cancelled";
    return false;
  }

  static void LoginThread()
  {
    const StreamSecrets::Keys &k = StreamSecrets::Get();
    std::string err;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short) k.discordRedirectPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (ls == INVALID_SOCKET || bind(ls, (sockaddr *) &a, sizeof(a)) != 0 || listen(ls, 4) != 0)
    {
      err = "port " + std::to_string(k.discordRedirectPort) + " is in use";
      if (ls != INVALID_SOCKET)
        closesocket(ls);
    }
    else
    {
      std::string verifier = RandomString(48);   // 64 characters
      std::string state = RandomString(16);
      std::string url = "https://discord.com/oauth2/authorize?response_type=code&client_id=" + StreamHttp::UrlEncode(k.discordClientId) +
                        "&scope=" + StreamHttp::UrlEncode(kScopes) +
                        "&redirect_uri=" + StreamHttp::UrlEncode(RedirectUri()) +
                        "&state=" + state +
                        "&code_challenge=" + Sha256Base64Url(verifier) + "&code_challenge_method=S256";
      ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      printf("[Discord] Login: browser opened, waiting on %s\n", RedirectUri().c_str());

      std::string code;
      bool ok = WaitForCallback(ls, state, code, err);
      closesocket(ls);
      Saved s;
      if (ok && TokenRequest("authorization_code", code, verifier, s, err) && FetchMe(s, err))
      {
        std::lock_guard<std::mutex> lock(s_lock);
        s_saved = s;
        SaveLocked();
        s_error.clear();
        s_state = State::LoggedIn;
        printf("[Discord] Login OK: %s\n", s.userName.c_str());
        return;
      }
    }
    std::lock_guard<std::mutex> lock(s_lock);
    s_error = err;
    s_state = s_cancel.load() ? State::LoggedOut : State::Failed;
    printf("[Discord] Login failed: %s\n", err.c_str());
  }

  // ---------------------------------------------------------------------------------------------------------
  // Client API
  // ---------------------------------------------------------------------------------------------------------
  bool ClientConfigured()
  {
    return !StreamSecrets::Get().discordClientId.empty();
  }

  State GetState()
  {
    if (!ClientConfigured())
      return State::NotConfigured;
    std::lock_guard<std::mutex> lock(s_lock);
    LoadLocked();
    return s_state.load();
  }

  std::string UserName()
  {
    std::lock_guard<std::mutex> lock(s_lock);
    LoadLocked();
    return s_saved.userName;
  }

  std::string StateText()
  {
    switch (GetState())
    {
    case State::NotConfigured: return "Discord login is off (this build has no Discord keys)";
    case State::LoggedOut:     return "Not logged in";
    case State::Waiting:       return "Waiting for the browser... (log in to Discord and press Authorize)";
    case State::LoggedIn:      return "Logged in as " + UserName();
    case State::Failed:
    {
      std::lock_guard<std::mutex> lock(s_lock);
      return "Login failed: " + s_error;
    }
    }
    return "";
  }

  void StartLogin()
  {
    if (!ClientConfigured() || s_state.load() == State::Waiting)
      return;
    if (s_loginThread.joinable())
      s_loginThread.join();
    s_cancel = false;
    s_state = State::Waiting;
    s_loginThread = std::thread(LoginThread);
  }

  void CancelLogin()
  {
    s_cancel = true;
    if (s_loginThread.joinable())
      s_loginThread.join();
  }

  void Logout()
  {
    CancelLogin();
    std::lock_guard<std::mutex> lock(s_lock);
    LoadLocked();
    s_saved = Saved();
    std::remove(kLoginFile);
    s_error.clear();
    s_state = State::LoggedOut;
  }

  bool GetAccessToken(std::string &token)
  {
    if (!ClientConfigured())
      return false;
    std::lock_guard<std::mutex> lock(s_lock);
    LoadLocked();
    if (s_state.load() != State::LoggedIn)
      return false;
    long long now = (long long) time(nullptr);
    if (s_saved.accessToken.empty() || now > s_saved.expiresAt - 600)
    {
      std::string err;
      Saved s = s_saved;
      if (s_saved.refreshToken.empty() || !TokenRequest("refresh_token", s_saved.refreshToken, "", s, err))
      {
        printf("[Discord] Refresh failed: %s -> please log in again\n", err.c_str());
        s_error = "login expired, please log in again";
        s_state = State::Failed;
        return false;
      }
      s_saved = s;
      SaveLocked();
    }
    token = s_saved.accessToken;
    return true;
  }

  // ---------------------------------------------------------------------------------------------------------
  // Host API
  // ---------------------------------------------------------------------------------------------------------
  bool HostConfigured()
  {
    return !StreamSecrets::Get().discordGuildId.empty();
  }

  Check CheckMember(const std::string &token, std::string &name, std::string &userId)
  {
    if (!HostConfigured())
      return Check::Unavailable;
    // The same client re-sends HELLO every 500 ms while we wait; remember answers for a minute.
    struct Cached { Check result; std::string name, id; DWORD at; };
    static std::mutex cacheLock;
    static std::map<std::string, Cached> cache;
    {
      std::lock_guard<std::mutex> lock(cacheLock);
      auto it = cache.find(token);
      if (it != cache.end() && GetTickCount() - it->second.at < 60000)
      {
        name = it->second.name;
        userId = it->second.id;
        return it->second.result;
      }
    }

    std::string body;
    int st = StreamHttp::Request(L"discord.com", L"GET",
                                 "/api/v10/users/@me/guilds/" + StreamSecrets::Get().discordGuildId + "/member",
                                 "Authorization: Bearer " + token + "\r\n", "", &body);
    Check result;
    MiniJson::Value v;
    if (st == 200 && MiniJson::Parse(body, v))
    {
      result = Check::Member;
      const MiniJson::Value *user = v.Get("user");
      std::string nick = v.GetString("nick");
      userId = user ? user->GetString("id") : "";
      name = !nick.empty() ? nick : (user ? DisplayName(*user) : "");
    }
    else if (st == 401)
      result = Check::InvalidToken;
    else if (st == 404 || st == 403)
      result = Check::NotMember;      // 10004 Unknown Guild / not a member
    else
      result = Check::Unavailable;    // network, 429, 5xx
    printf("[Discord] Member check: HTTP %d -> %s\n", st,
           result == Check::Member ? "member" : result == Check::NotMember ? "not a member" :
           result == Check::InvalidToken ? "invalid token" : "unavailable");

    if (result != Check::Unavailable)
    {
      std::lock_guard<std::mutex> lock(cacheLock);
      if (cache.size() > 64)
        cache.clear();
      cache[token] = { result, name, userId, GetTickCount() };
    }
    return result;
  }
}

#endif // SUPERMODEL_WIN32
