/*
 * DiscordNotifier.cpp
 *
 * Streaming (2026-11-16, test stage): this host's slot status as one Discord message. See DiscordNotifier.h.
 */

#ifdef SUPERMODEL_WIN32

#include "DiscordNotifier.h"
#include "Secrets.h"
#include "MiniJson.h"
#include "Http.h"
#include <chrono>
#include <cstdio>
#include <fstream>

static const char *kMessageFile = "Config/DiscordMessage.txt";
static const int   kIntervalSec = 15;

bool DiscordNotifier::Configured()
{
  return StreamSecrets::HasDiscordBot();
}

static std::string EscapeJson(const std::string &s)
{
  std::string o;
  for (unsigned char c : s)
  {
    if (c == '"')       o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c < 0x20)  { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
    else                o += (char) c;
  }
  return o;
}

int DiscordNotifier::Request(const wchar_t *verb, const std::string &path, const std::string &json, std::string *body)
{
  std::string headers = "Authorization: Bot " + StreamSecrets::Get().discordBotToken + "\r\n";
  if (!json.empty())
    headers += "Content-Type: application/json\r\n";
  return StreamHttp::Request(L"discord.com", verb, path, headers, json, body);
}

// Text is kept ASCII here; the emoji and Japanese go in as JSON \u escapes.
std::string DiscordNotifier::BuildJson(const DiscordHostStatus &s)
{
  std::string title = (s.gametitle == "spikeofe" || s.gametitle.empty()) ? "Spikeout FE" : s.gametitle;
  if (!s.servername.empty())
    title += " - " + s.servername;

  // like PonLuncher: only the last two numbers of the IP
  std::string masked;
  int dots = 0;
  size_t second = std::string::npos;
  for (size_t i = 0; i < s.ip.size(); i++)
    if (s.ip[i] == '.' && ++dots == 2)
      second = i;
  if (dots == 3 && second != std::string::npos)
    masked = "xxx.xxx" + s.ip.substr(second);

  std::string fields;
  int used = 0, total = 0;
  for (int n = 1; n <= 8; n++)
  {
    if (!s.present[n])
      continue;
    total++;
    std::string value;
    if (s.available[n])
      value = "\\ud83d\\udfe2 \\u7a7a\\u304d";                                   // green, "free"
    else
    {
      used++;
      value = "\\ud83d\\udd34 " + (s.player[n].empty() ? std::string("\\u4f7f\\u7528\\u4e2d")  // red, "in use"
                                                       : EscapeJson(s.player[n]));
    }
    if (!fields.empty())
      fields += ",";
    fields += "{\"name\":\"Slot " + std::to_string(n) + "\",\"value\":\"" + value + "\",\"inline\":true}";
  }
  if (total == 0)
    fields = "{\"name\":\"-\",\"value\":\"\\u306a\\u3057\",\"inline\":false}";   // "none"

  std::string json = "{\"embeds\":[{\"title\":\"" + EscapeJson(title) + "\"";
  if (!masked.empty())
    json += ",\"description\":\"" + masked + "\"";
  json += ",\"color\":" + std::string(used < total ? "3066993" : "15158332");   // green while a slot is free, else red
  json += ",\"fields\":[" + fields + "]";
  json += ",\"footer\":{\"text\":\"Supermodel PonMi streaming\"}}]}";
  return json;
}

void DiscordNotifier::Start(FetchFn fetch)
{
  if (m_running.load() || !Configured())
    return;
  m_fetch = std::move(fetch);
  m_running = true;
  m_thread = std::thread(&DiscordNotifier::Thread, this);
  printf("[Discord] status post started (every %d s)\n", kIntervalSec);
}

void DiscordNotifier::Stop()
{
  if (!m_running.exchange(false))
    return;
  m_cv.notify_all();
  if (m_thread.joinable())
    m_thread.join();
  if (!m_messageId.empty())
  {
    int st = Request(L"DELETE", "/api/v10/channels/" + StreamSecrets::Get().discordChannelId + "/messages/" + m_messageId, "", nullptr);
    printf("[Discord] message deleted (HTTP %d)\n", st);
    m_messageId.clear();
  }
  std::remove(kMessageFile);
}

void DiscordNotifier::Thread()
{
  const std::string base = "/api/v10/channels/" + StreamSecrets::Get().discordChannelId + "/messages";

  // a message left by a crash
  {
    std::ifstream ifs(kMessageFile);
    std::string old;
    if (ifs >> old && !old.empty())
    {
      int st = Request(L"DELETE", base + "/" + old, "", nullptr);
      printf("[Discord] old message removed (HTTP %d)\n", st);
    }
  }
  std::remove(kMessageFile);

  while (m_running.load())
  {
    DiscordHostStatus s;
    if (m_fetch && m_fetch(s))
    {
      std::string json = BuildJson(s);
      if (json != m_lastJson || m_messageId.empty())
      {
        std::string body;
        int st = 0;
        if (!m_messageId.empty())
        {
          st = Request(L"PATCH", base + "/" + m_messageId, json, &body);
          if (st == 404)               // deleted by someone: post again
            m_messageId.clear();
        }
        if (m_messageId.empty())
        {
          body.clear();
          st = Request(L"POST", base, json, &body);
          MiniJson::Value v;
          if (st >= 200 && st < 300 && MiniJson::Parse(body, v))
          {
            m_messageId = v.GetString("id");
            std::ofstream ofs(kMessageFile);
            ofs << m_messageId << "\n";
            printf("[Discord] message posted\n");
          }
        }
        if (st >= 200 && st < 300)
          m_lastJson = json;
        else
          printf("[Discord] post failed (HTTP %d)\n", st);   // 401 token, 403 channel permission, 429 too fast
      }
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait_for(lock, std::chrono::seconds(kIntervalSec), [this] { return !m_running.load(); });
  }
}

#endif // SUPERMODEL_WIN32
