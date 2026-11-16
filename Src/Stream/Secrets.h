/*
 * Secrets.h
 *
 * Streaming keys and IDs. 11/16 (owner: hosts are ordinary users too): they are EMBEDDED into the exe at build time
 * (Scripts/gen_keys.ps1: Config/StreamingKeys.ini of the source tree -> Src/Stream/EmbeddedKeys.h, both not in git),
 * stored XOR-scrambled. No keys file is distributed or read at run time any more.
 *
 * Only values that may be public are embedded: Firebase web API key and database URL, Discord client ID, guild ID,
 * redirect port, invite link, and the webhook of the status channel. Anything in a distributed exe can be dug out,
 * so the real defence is the Firebase rules (slot owners, read-only /main5) and the host-side Discord member check.
 * Never a bot token or a client secret.
 * A build without Config/StreamingKeys.ini has empty keys: Firebase lobby and Discord are then off (direct IP works).
 */

#ifndef INCLUDED_STREAM_SECRETS_H
#define INCLUDED_STREAM_SECRETS_H

#include <string>

namespace StreamSecrets
{
  struct Keys
  {
    bool        fileFound = false;      // keys were embedded at build time
    std::string firebaseApiKey;
    std::string firebaseDbUrl;
    std::string discordClientId;
    std::string discordGuildId;
    int         discordRedirectPort = 53134;
    std::string discordInviteUrl;
    std::string discordWebhookUrl;      // status post (DiscordNotifier); "" = no post
  };

  const Keys &Get();

  bool HasFirebase();
  bool HasDiscordWebhook();
  // Host name of the database, e.g. L"xxx-default-rtdb.asia-southeast1.firebasedatabase.app"
  std::wstring FirebaseDbHost();
}

#endif // INCLUDED_STREAM_SECRETS_H
