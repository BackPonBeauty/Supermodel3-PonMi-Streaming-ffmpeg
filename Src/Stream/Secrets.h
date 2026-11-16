/*
 * Secrets.h
 *
 * Streaming (P6, 2026-11-16, requirement "安全" / design chapter 6): keys and IDs are not compiled into the exe.
 * They are read from Config/StreamingKeys.ini (relative to the working folder), which is NOT in git.
 * Config/StreamingKeys.example.ini (in git) shows the format.
 *
 *   FirebaseApiKey      = ...            Firebase Web API key (anonymous sign-in)
 *   FirebaseDbUrl       = https://....firebasedatabase.app
 *   DiscordClientId     = ...            (P6b)
 *   DiscordGuildId      = ...            (P6b)
 *   DiscordRedirectPort = 53134          (P6b)
 *   DiscordBotToken     = ...            status post to a Discord channel (host PCs only, see DiscordNotifier.h)
 *   DiscordChannelId    = ...
 *
 * No client secret, ever. The bot token is only for the host's status post (test stage, owner's decision
 * 2026-11-16) and must never be put in a client folder; clients never need it (requirement CL-07).
 * Without the file, Firebase and Discord are simply off (direct IP still works).
 */

#ifndef INCLUDED_STREAM_SECRETS_H
#define INCLUDED_STREAM_SECRETS_H

#include <string>

namespace StreamSecrets
{
  struct Keys
  {
    bool        fileFound = false;
    std::string firebaseApiKey;
    std::string firebaseDbUrl;
    std::string discordClientId;
    std::string discordGuildId;
    int         discordRedirectPort = 53134;
    std::string discordBotToken;    // without the "Bot " prefix
    std::string discordChannelId;
  };

  // Loads Config/StreamingKeys.ini once.
  const Keys &Get();

  bool HasFirebase();
  bool HasDiscordBot();
  // Host name of the database, e.g. L"xxx-default-rtdb.asia-southeast1.firebasedatabase.app"
  std::wstring FirebaseDbHost();
}

#endif // INCLUDED_STREAM_SECRETS_H
