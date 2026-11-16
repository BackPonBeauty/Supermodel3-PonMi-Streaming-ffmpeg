/*
 * Http.h
 *
 * Streaming (P6b, 2026-11-16): one small blocking HTTPS request with WinHTTP, shared by the Discord
 * status post (DiscordNotifier) and the Discord login / membership check (DiscordAuth).
 */

#ifndef INCLUDED_STREAM_HTTP_H
#define INCLUDED_STREAM_HTTP_H

#include <string>

namespace StreamHttp
{
  // verb: L"GET", L"POST", L"PATCH", L"DELETE". headers: "Name: value\r\n..." (may be empty).
  // Returns the HTTP status (0 = no answer: no network, timeout, TLS error). body receives the answer.
  int Request(const wchar_t *host, const wchar_t *verb, const std::string &path,
              const std::string &headers, const std::string &data, std::string *body,
              int timeoutMs = 5000);

  // application/x-www-form-urlencoded / URL query escaping
  std::string UrlEncode(const std::string &s);
}

#endif // INCLUDED_STREAM_HTTP_H
