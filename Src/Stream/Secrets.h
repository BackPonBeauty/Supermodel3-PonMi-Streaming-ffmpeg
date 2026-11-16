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
 *
 * No Discord bot token and no client secret, ever (requirement CL-07/SV-16).
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
  };

  // Loads Config/StreamingKeys.ini once.
  const Keys &Get();

  bool HasFirebase();
  // Host name of the database, e.g. L"xxx-default-rtdb.asia-southeast1.firebasedatabase.app"
  std::wstring FirebaseDbHost();
}

#endif // INCLUDED_STREAM_SECRETS_H
