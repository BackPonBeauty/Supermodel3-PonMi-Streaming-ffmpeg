/*
 * DiscordNotifier.cpp
 *
 * Streaming (2026-11-16): this host's slot status as one Discord message, through the status channel's WEBHOOK
 * (embedded at build time; a webhook can only post to its channel and edit / delete its own messages). See DiscordNotifier.h.
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
  return StreamSecrets::HasDiscordWebhook();
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

// "https://discord.com/api/webhooks/<id>/<token>" -> host L"discord.com", path "/api/webhooks/<id>/<token>"
static bool WebhookParts(std::wstring &host, std::string &path)
{
  std::string url = StreamSecrets::Get().discordWebhookUrl;
  size_t p = url.find("://");
  if (p == std::string::npos)
    return false;
  size_t slash = url.find('/', p + 3);
  if (slash == std::string::npos)
    return false;
  std::string h = url.substr(p + 3, slash - (p + 3));
  host.assign(h.begin(), h.end());
  path = url.substr(slash);
  while (!path.empty() && (path.back() == '/' || path.back() == ' '))
    path.pop_back();
  return path.find("/webhooks/") != std::string::npos;
}

// suffix: "?wait=true" (post) or "/messages/<id>" (edit, delete). No Authorization: the URL is the credential.
int DiscordNotifier::Request(const wchar_t *verb, const std::string &suffix, const std::string &json, std::string *body)
{
  std::wstring host;
  std::string path;
  if (!WebhookParts(host, path))
    return 0;
  std::string headers;
  if (!json.empty())
    headers = "Content-Type: application/json\r\n";
  return StreamHttp::Request(host.c_str(), verb, path + suffix, headers, json, body);
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

  // 11/17 (owner): two columns, Slot 1-4 on the left and Slot 5-8 on the right, one slot per line.
  // (Two inline fields: side by side on PC; the phone app puts the right one under the left one.)
  std::string fields;
  int used = 0, total = 0;
  for (int col = 0; col < 2; col++)
  {
    std::string lines;
    for (int n = col * 4 + 1; n <= col * 4 + 4; n++)
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
      if (!lines.empty())
        lines += "\\n";
      lines += "**" + std::to_string(n) + "** " + value;
    }
    if (lines.empty())
      continue;                                                                  // no slot of this column runs
    if (!fields.empty())
      fields += ",";
    fields += "{\"name\":\"Slot " + std::to_string(col * 4 + 1) + "\\u301c" + std::to_string(col * 4 + 4) +
              "\",\"value\":\"" + lines + "\",\"inline\":true}";
  }
  if (total == 0)
    fields = "{\"name\":\"-\",\"value\":\"\\u306a\\u3057\",\"inline\":false}";   // "none"

  // SV-18: a lock in front when the host has a room password
  std::string json = "{\"embeds\":[{\"title\":\"" + std::string(s.locked ? "\\ud83d\\udd12 " : "") + EscapeJson(title) + "\"";
  // 11/17 (owner): the games running on the host, one line each: "GAME A：STAGE 7・エリア75・6人"
  std::string desc = masked;
  {
    size_t p = 0;
    for (int g = 0; !s.sess.empty() && p <= s.sess.size() && g < 8; g++)
    {
      size_t e = s.sess.find(';', p);
      std::string part = s.sess.substr(p, e == std::string::npos ? std::string::npos : e - p);
      unsigned st = 0, ar = 0, pl = 0;
      if (sscanf(part.c_str(), "%u,%u,%u", &st, &ar, &pl) == 3)
      {
        char line[160];
        snprintf(line, sizeof(line), "GAME %c\\uff1aSTAGE %u\\u30fb\\u30a8\\u30ea\\u30a2%u\\u30fb%u\\u4eba", 'A' + g, st, ar, pl);
        if (!desc.empty())
          desc += "\\n";
        desc += line;
      }
      if (e == std::string::npos)
        break;
      p = e + 1;
    }
  }
  if (!desc.empty())
    json += ",\"description\":\"" + desc + "\"";
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
    int st = Request(L"DELETE", "/messages/" + m_messageId, "", nullptr);
    printf("[Discord] message deleted (HTTP %d)\n", st);
    m_messageId.clear();
  }
  std::remove(kMessageFile);
}

void DiscordNotifier::Thread()
{
  // a message left by a crash
  {
    std::ifstream ifs(kMessageFile);
    std::string old;
    if (ifs >> old && !old.empty())
    {
      int st = Request(L"DELETE", "/messages/" + old, "", nullptr);
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
          st = Request(L"PATCH", "/messages/" + m_messageId, json, &body);
          if (st == 404)               // deleted by someone: post again
            m_messageId.clear();
        }
        if (m_messageId.empty())
        {
          body.clear();
          st = Request(L"POST", "?wait=true", json, &body);   // wait=true: the answer holds the message id
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
          printf("[Discord] post failed (HTTP %d)\n", st);   // 401/404 webhook wrong or deleted, 429 too fast
      }
    }
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait_for(lock, std::chrono::seconds(kIntervalSec), [this] { return !m_running.load(); });
  }
}

#endif // SUPERMODEL_WIN32
