/*
 * DiscordAuth.h
 *
 * Streaming (P6b, 2026-11-16; requirements CL-07, SV-16, LB-07; design chapter 6).
 *
 * Client: Discord login with OAuth2 authorization code + PKCE. No client secret and no bot token
 *   (the Discord application must have "Public Client" on). The browser comes back to
 *   http://127.0.0.1:<DiscordRedirectPort>/callback, where a tiny loopback receiver takes the code.
 *   Scopes: identify (who) and guilds.members.read (is a member of the guild).
 *   The tokens are kept in Config/DiscordLogin.ini of the client folder (not in git) and refreshed
 *   when they expire (an access token lasts 7 days).
 *
 * Host: checks a client's access token with GET /users/@me/guilds/{DiscordGuildId}/member.
 *   The host never stores the token.
 *
 * IDs are embedded at build time (Stream/Secrets.h): DiscordClientId, DiscordGuildId, DiscordRedirectPort.
 */

#ifndef INCLUDED_STREAM_DISCORDAUTH_H
#define INCLUDED_STREAM_DISCORDAUTH_H

#include <string>

namespace DiscordAuth
{
  // ---- client ----
  enum class State { NotConfigured, LoggedOut, Waiting, LoggedIn, Failed };

  bool  ClientConfigured();          // DiscordClientId is set
  State GetState();
  std::string StateText();           // for the lobby (user name, or what went wrong)
  std::string UserName();            // Discord display name when logged in

  void StartLogin();                 // opens the browser; finishes in the background (up to 3 minutes)
  void CancelLogin();
  void Logout();                     // forgets the saved tokens

  // A valid access token (refreshed if needed). Blocking (network) when a refresh is needed.
  // Returns false when not logged in or the refresh failed.
  bool GetAccessToken(std::string &token);

  // ---- host ----
  enum class Check { Member, NotMember, InvalidToken, Unavailable };

  bool  HostConfigured();            // DiscordGuildId is set
  // Blocking (network, up to ~5 s). name: guild nick, else global name, else user name. userId: Discord ID.
  Check CheckMember(const std::string &token, std::string &name, std::string &userId);
}

#endif // INCLUDED_STREAM_DISCORDAUTH_H
