/*
 * Http.cpp
 *
 * Streaming (P6b, 2026-11-16): see Http.h.
 */

#ifdef SUPERMODEL_WIN32

#include "Http.h"
#include <windows.h>
#include <winhttp.h>
#include <cstdio>

namespace StreamHttp
{
  int Request(const wchar_t *host, const wchar_t *verb, const std::string &path,
              const std::string &headers, const std::string &data, std::string *body, int timeoutMs)
  {
    // Discord asks bots for this form of User-Agent; it is harmless for the OAuth endpoints too.
    HINTERNET session = WinHttpOpen(L"DiscordBot (https://www.supermodel3.com, 1.0)",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
      return 0;
    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    HINTERNET conn = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    int status = 0;
    if (conn)
    {
      std::wstring wpath(path.begin(), path.end());
      HINTERNET req = WinHttpOpenRequest(conn, verb, wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
      if (req)
      {
        if (!headers.empty())
        {
          std::wstring wh(headers.begin(), headers.end());
          WinHttpAddRequestHeaders(req, wh.c_str(), (DWORD) -1, WINHTTP_ADDREQ_FLAG_ADD);
        }
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               data.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID) data.data(),
                               (DWORD) data.size(), (DWORD) data.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr))
        {
          DWORD code = 0, len = sizeof(code);
          WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
          status = (int) code;
          DWORD size = 0;
          while (body && WinHttpQueryDataAvailable(req, &size) && size > 0)
          {
            std::string chunk(size, '\0');
            DWORD got = 0;
            WinHttpReadData(req, &chunk[0], size, &got);
            body->append(chunk, 0, got);
          }
        }
        WinHttpCloseHandle(req);
      }
      WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    return status;
  }

  std::string UrlEncode(const std::string &s)
  {
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s)
    {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        o += (char) c;
      else
      {
        o += '%';
        o += hex[c >> 4];
        o += hex[c & 15];
      }
    }
    return o;
  }
}

#endif // SUPERMODEL_WIN32
