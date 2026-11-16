/*
 * Ticket.cpp
 *
 * Entry tickets (11/16, design 3.8). See Ticket.h.
 */

#include "Ticket.h"
#include "Http.h"
#include "MiniJson.h"
#include "Secrets.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace Ticket
{
  static bool Base64Decode(const std::string &in, std::string &out)
  {
    DWORD len = 0;
    if (in.empty() || !CryptStringToBinaryA(in.c_str(), (DWORD) in.size(), CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr))
      return false;
    out.assign(len, '\0');
    if (!CryptStringToBinaryA(in.c_str(), (DWORD) in.size(), CRYPT_STRING_BASE64, (BYTE *) &out[0], &len, nullptr, nullptr))
      return false;
    out.resize(len);
    return true;
  }

  static std::string JsonEscape(const std::string &s)
  {
    std::string o;
    for (unsigned char c : s)
    {
      if (c == '"' || c == '\\') { o += '\\'; o += (char) c; }
      else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
      else o += (char) c;
    }
    return o;
  }

  // "https://host/path" -> host (wide), path
  static bool SplitUrl(const std::string &url, std::wstring &host, std::string &path)
  {
    const std::string pre = "https://";
    if (url.compare(0, pre.size(), pre) != 0)
      return false;
    std::string rest = url.substr(pre.size());
    size_t slash = rest.find('/');
    std::string h = rest.substr(0, slash);
    path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (h.empty())
      return false;
    host.assign(h.begin(), h.end());
    return true;
  }

  bool Configured()
  {
    const StreamSecrets::Keys &k = StreamSecrets::Get();
    return !k.ticketUrl.empty() && !k.ticketPublicKey.empty();
  }

  Fetch Request(const std::string &discordToken, const std::string &host, int slot, uint32_t nonce,
                std::string &payload, std::string &sig)
  {
    std::wstring whost;
    std::string path;
    if (!Configured() || !SplitUrl(StreamSecrets::Get().ticketUrl, whost, path))
      return Fetch::NotConfigured;
    std::string body = "{\"token\":\"" + JsonEscape(discordToken) + "\",\"host\":\"" + JsonEscape(host) +
                       "\",\"slot\":" + std::to_string(slot) + ",\"nonce\":" + std::to_string(nonce) + "}";
    std::string resp;
    int status = 0;
    for (int attempt = 0; attempt < 2; attempt++)
    {
      resp.clear();
      status = StreamHttp::Request(whost.c_str(), L"POST", path, "Content-Type: application/json\r\n", body, &resp, 8000);
      // 11/16 night: Discord's rate limit (joining, leaving and joining again within seconds) comes back as
      // 503 {"error":"discord busy","retry_after":s}. Wait that long once (the host's nonce lives 30 s) and ask again.
      if (status != 503 || attempt > 0)
        break;
      MiniJson::Value e;
      double wait = MiniJson::Parse(resp, e) ? e.GetNumber("retry_after", 0.0) : 0.0;
      if (wait <= 0.0 || wait > 10.0)
        break;
      printf("[Ticket] Discord is busy: asking again in %.1f s\n", wait);
      Sleep((DWORD) (wait * 1000.0) + 200);
    }
    if (status == 401) return Fetch::LoginInvalid;
    // 403 is "not member" only when the Worker says so; Cloudflare's own blocks (e.g. error 1010) are also 403
    if (status == 403 && resp.find("not member") != std::string::npos) return Fetch::NotMember;
    if (status != 200)
    {
      printf("[Ticket] the entry-ticket server answered HTTP %d\n", status);
      return Fetch::Unavailable;
    }
    MiniJson::Value v;
    if (!MiniJson::Parse(resp, v) || !Base64Decode(v.GetString("ticket"), payload) || !Base64Decode(v.GetString("sig"), sig) ||
        (int) sig.size() != kSigLength || payload.empty() || (int) payload.size() > kMaxPayload)
      return Fetch::Unavailable;
    return Fetch::Ok;
  }

  // The public key as a BCrypt key handle (made once)
  static BCRYPT_KEY_HANDLE PublicKey()
  {
    static BCRYPT_ALG_HANDLE alg = nullptr;
    static BCRYPT_KEY_HANDLE key = nullptr;
    static std::once_flag once;
    std::call_once(once, []() {
      std::string raw;
      if (!Base64Decode(StreamSecrets::Get().ticketPublicKey, raw) || raw.size() != 65 || (unsigned char) raw[0] != 0x04)
      {
        printf("[Ticket] no valid entry-ticket public key in this build\n");
        return;
      }
      if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0)
        return;
      std::vector<unsigned char> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
      BCRYPT_ECCKEY_BLOB *hdr = (BCRYPT_ECCKEY_BLOB *) blob.data();
      hdr->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
      hdr->cbKey = 32;
      memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), raw.data() + 1, 64);   // X | Y
      if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key, blob.data(), (ULONG) blob.size(), 0) != 0)
        key = nullptr;
    });
    return key;
  }

  static bool Sha256(const std::string &data, unsigned char out[32])
  {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0 &&
              BCryptHashData(h, (PUCHAR) data.data(), (ULONG) data.size(), 0) == 0 &&
              BCryptFinishHash(h, out, 32, 0) == 0;
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
  }

  bool Verify(const std::string &payload, const std::string &sig, Info &out)
  {
    BCRYPT_KEY_HANDLE key = PublicKey();
    if (!key || (int) sig.size() != kSigLength || payload.empty() || (int) payload.size() > kMaxPayload)
      return false;
    unsigned char hash[32];
    if (!Sha256(payload, hash))
      return false;
    if (BCryptVerifySignature(key, nullptr, hash, 32, (PUCHAR) sig.data(), (ULONG) sig.size(), 0) != 0)
      return false;
    MiniJson::Value v;
    if (!MiniJson::Parse(payload, v) || v.GetNumber("v") != 1)
      return false;
    out.id = v.GetString("id");
    out.name = v.GetString("name");
    out.host = v.GetString("host");
    out.slot = (int) v.GetNumber("slot");
    double n = v.GetNumber("nonce");
    out.nonce = (n > 0 && n <= 4294967295.0) ? (uint32_t) n : 0;
    out.exp = (long long) v.GetNumber("exp");
    return !out.id.empty() && out.nonce != 0;
  }
}
