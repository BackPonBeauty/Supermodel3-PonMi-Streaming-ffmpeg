/*
 * DiscordNotifier.h
 *
 * Streaming (2026-11-16): posts this host's slot status to a Discord channel, like PonLuncher's DiscordHelper,
 * and keeps that one message up to date.
 *
 *  - 11/16: through the status channel's webhook (embedded at build time, Stream/Secrets.h), no bot token any
 *    more: a webhook can only post to that channel and edit / delete its own messages. Without it nothing is posted.
 *  - One server per host posts (Supermodel.ini DiscordPost: -1 = only the slot-1 server (default),
 *    0 = off, 1 = on). The status of all slots is read from Firebase every 15 s, and the message is
 *    edited only when it changed.
 *  - On exit the message is deleted. Its id is kept in Config/DiscordMessage.txt, so a message left by a
 *    crash is deleted at the next start.
 */

#ifndef INCLUDED_STREAM_DISCORDNOTIFIER_H
#define INCLUDED_STREAM_DISCORDNOTIFIER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct DiscordHostStatus
{
  std::string servername;
  std::string gametitle;
  std::string ip;
  bool        locked = false;     // SV-18: room password
  std::string sess;               // 11/17: games in the link, "stage,area,players;..." (GAME A first)
  bool        present[9] = {};    // 1..8: a streaming server runs for this slot
  bool        available[9] = {};  // nobody connected
  std::string player[9];
};

class DiscordNotifier
{
public:
  using FetchFn = std::function<bool(DiscordHostStatus &out)>;

  ~DiscordNotifier() { Stop(); }

  // Starts the posting thread. fetch() reads this host's status (false = not available now).
  void Start(FetchFn fetch);
  // Stops the thread and deletes the message (blocking, a few hundred ms).
  void Stop();

  static bool Configured();

private:
  void Thread();
  static std::string BuildJson(const DiscordHostStatus &s);
  // Returns the HTTP status (0 = no answer). body receives the answer.
  static int Request(const wchar_t *verb, const std::string &path, const std::string &json, std::string *body);

  FetchFn                 m_fetch;
  std::thread             m_thread;
  std::atomic<bool>       m_running{false};
  std::mutex              m_mutex;
  std::condition_variable m_cv;
  std::string             m_messageId;
  std::string             m_lastJson;
};

#endif // INCLUDED_STREAM_DISCORDNOTIFIER_H
