/*
 * StreamDpapi.h
 *
 * 11/16: small files encrypted with Windows DPAPI (CryptProtectData, current Windows user only), stored as
 * "DPAPI:<base64>". Used for the client's Discord login (DiscordAuth) and the host slots' Firebase session
 * (FirebaseMatchingCpp). Another Windows user, or the file copied to another PC, cannot read it.
 */

#ifndef INCLUDED_STREAM_STREAMDPAPI_H
#define INCLUDED_STREAM_STREAMDPAPI_H

#include <windows.h>
#include <wincrypt.h>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace StreamDpapi
{
  constexpr const char *kPrefix = "DPAPI:";

  inline std::string Protect(const std::string &plain)
  {
    DATA_BLOB in = { (DWORD) plain.size(), (BYTE *) plain.data() }, out = {};
    if (!CryptProtectData(&in, L"Supermodel PonMi", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
      return "";
    DWORD len = 0;
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
    std::string b64(len, '\0');
    CryptBinaryToStringA(out.pbData, out.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &b64[0], &len);
    b64.resize(len);
    LocalFree(out.pbData);
    return std::string(kPrefix) + b64;
  }

  // text = "DPAPI:<base64>" (whitespace allowed around)
  inline bool Unprotect(const std::string &text, std::string &plain)
  {
    if (text.compare(0, strlen(kPrefix), kPrefix) != 0)
      return false;
    std::string b64 = text.substr(strlen(kPrefix));
    while (!b64.empty() && (b64.back() == '\n' || b64.back() == '\r' || b64.back() == ' '))
      b64.pop_back();
    DWORD len = 0;
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD) b64.size(), CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr))
      return false;
    std::vector<BYTE> bin(len);
    if (!CryptStringToBinaryA(b64.c_str(), (DWORD) b64.size(), CRYPT_STRING_BASE64, bin.data(), &len, nullptr, nullptr))
      return false;
    DATA_BLOB in = { len, bin.data() }, out = {};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
      return false;
    plain.assign((const char *) out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
  }

  inline bool ReadFile(const char *path, std::string &plain)
  {
    std::ifstream ifs(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return Unprotect(text, plain);
  }

  inline bool WriteFile(const char *path, const std::string &plain)
  {
    std::string enc = Protect(plain);
    if (enc.empty())
      return false;
    std::ofstream ofs(path, std::ios::trunc | std::ios::binary);
    ofs << enc << "\n";
    return (bool) ofs;
  }
}

#endif // INCLUDED_STREAM_STREAMDPAPI_H
