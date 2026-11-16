/*
 * Secrets.cpp
 *
 * Streaming keys embedded at build time (11/16). See Secrets.h.
 */

#include "Secrets.h"
#include <cstdio>
#include <cstdlib>
#include <mutex>

#if __has_include("EmbeddedKeys.h")
#include "EmbeddedKeys.h"
#define HAVE_EMBEDDED_KEYS 1
#endif

namespace StreamSecrets
{
#ifdef HAVE_EMBEDDED_KEYS
  // Same scrambling as Scripts/gen_keys.ps1 (index = position of the key in its list)
  static std::string Unscramble(const unsigned char *b, int n, int idx)
  {
    std::string s;
    s.resize(n > 0 ? n : 0);
    for (int i = 0; i < n; i++)
      s[i] = (char) (b[i] ^ (unsigned char) ((0x5A + i * 31 + idx * 17) & 0xFF));
    return s;
  }
#endif

  const Keys &Get()
  {
    static Keys keys;
    static std::once_flag once;
    std::call_once(once, []() {
#ifdef HAVE_EMBEDDED_KEYS
      using namespace EmbeddedKeys;
      keys.firebaseApiKey    = Unscramble(kFirebaseApiKey, kFirebaseApiKeyLen, 0);
      keys.firebaseDbUrl     = Unscramble(kFirebaseDbUrl, kFirebaseDbUrlLen, 1);
      keys.discordClientId   = Unscramble(kDiscordClientId, kDiscordClientIdLen, 2);
      keys.discordGuildId    = Unscramble(kDiscordGuildId, kDiscordGuildIdLen, 3);
      int port = atoi(Unscramble(kDiscordRedirectPort, kDiscordRedirectPortLen, 4).c_str());
      if (port > 0 && port < 65536)
        keys.discordRedirectPort = port;
      keys.discordInviteUrl  = Unscramble(kDiscordInviteUrl, kDiscordInviteUrlLen, 5);
      keys.discordWebhookUrl = Unscramble(kDiscordWebhookUrl, kDiscordWebhookUrlLen, 6);
      keys.fileFound = !keys.firebaseApiKey.empty() || !keys.discordClientId.empty();
#endif
      printf("[Keys] embedded keys: Firebase %s, Discord login %s, Discord post %s\n",
             (!keys.firebaseApiKey.empty() && !keys.firebaseDbUrl.empty()) ? "yes" : "no",
             (!keys.discordClientId.empty() && !keys.discordGuildId.empty()) ? "yes" : "no",
             !keys.discordWebhookUrl.empty() ? "yes" : "no");
    });
    return keys;
  }

  bool HasFirebase()
  {
    const Keys &k = Get();
    return !k.firebaseApiKey.empty() && !FirebaseDbHost().empty();
  }

  bool HasDiscordWebhook()
  {
    return !Get().discordWebhookUrl.empty();
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
