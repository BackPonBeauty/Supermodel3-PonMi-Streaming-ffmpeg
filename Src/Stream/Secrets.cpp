/*
 * Secrets.cpp
 *
 * Streaming (P6, 2026-11-16): reads Config/StreamingKeys.ini. See Secrets.h.
 */

#include "Secrets.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>

namespace StreamSecrets
{
  static std::string Trim(const std::string &s)
  {
    size_t a = s.find_first_not_of(" \t\r\n\"");
    size_t b = s.find_last_not_of(" \t\r\n\"");
    return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
  }

  const Keys &Get()
  {
    static Keys keys;
    static std::once_flag once;
    std::call_once(once, []() {
      std::ifstream ifs("Config/StreamingKeys.ini");
      if (!ifs.is_open())
      {
        printf("[Keys] Config/StreamingKeys.ini not found: Firebase lobby and Discord login are off.\n");
        return;
      }
      keys.fileFound = true;
      std::string line;
      while (std::getline(ifs, line))
      {
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[')
          continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos)
          continue;
        std::string k = Trim(t.substr(0, eq)), v = Trim(t.substr(eq + 1));
        if (k == "FirebaseApiKey")           keys.firebaseApiKey = v;
        else if (k == "FirebaseDbUrl")       keys.firebaseDbUrl = v;
        else if (k == "DiscordClientId")     keys.discordClientId = v;
        else if (k == "DiscordGuildId")      keys.discordGuildId = v;
        else if (k == "DiscordRedirectPort") { int p = atoi(v.c_str()); if (p > 0 && p < 65536) keys.discordRedirectPort = p; }
      }
      printf("[Keys] Config/StreamingKeys.ini loaded (Firebase: %s, Discord: %s)\n",
             (!keys.firebaseApiKey.empty() && !keys.firebaseDbUrl.empty()) ? "yes" : "no",
             (!keys.discordClientId.empty() && !keys.discordGuildId.empty()) ? "yes" : "no");
    });
    return keys;
  }

  bool HasFirebase()
  {
    const Keys &k = Get();
    return !k.firebaseApiKey.empty() && !FirebaseDbHost().empty();
  }

  std::wstring FirebaseDbHost()
  {
    std::string url = Get().firebaseDbUrl;
    size_t p = url.find("://");
    if (p != std::string::npos)
      url = url.substr(p + 3);
    while (!url.empty() && url.back() == '/')
      url.pop_back();
    return std::wstring(url.begin(), url.end());
  }
}
