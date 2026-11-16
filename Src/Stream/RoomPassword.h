/*
 * RoomPassword.h
 *
 * 11/16 (owner): a host may set a password (PonLuncher, blank = public). Requirement SV-18, design 3.6.
 *
 * - The password never goes over the network: the host sends a fresh nonce (REJECT R_CHALLENGE) and the
 *   client answers with Proof(nonce, password) = SHA-256("SPKS-room-v2" | nonce LE | password) in the next HELLO.
 * - The host keeps it in its ini as StreamPassword = "DPAPI:<base64>" (written by the launcher, this Windows user
 *   only). A value the host cannot decrypt locks the room (nobody can join) instead of opening it.
 */

#ifndef INCLUDED_STREAM_ROOMPASSWORD_H
#define INCLUDED_STREAM_ROOMPASSWORD_H

#include "StreamDpapi.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace RoomPassword
{
  constexpr size_t kMaxLength() { return 32; }   // = StreamProto::kMaxPasswordLength

  inline bool Sha256(const void *data, size_t len, uint8_t out[32])
  {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
              BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0 &&
              BCryptHashData(hash, (PUCHAR) data, (ULONG) len, 0) == 0 &&
              BCryptFinishHash(hash, out, 32, 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
  }

  inline void Proof(uint32_t nonce, const std::string &password, uint8_t out[32])
  {
    std::string msg = "SPKS-room-v2";
    for (int i = 0; i < 4; i++)
      msg.push_back((char) ((nonce >> (8 * i)) & 0xFF));
    msg += password;
    if (!Sha256(msg.data(), msg.size(), out))
      memset(out, 0, 32);
  }

  // Compares all 32 bytes (no early exit)
  inline bool ProofEquals(const uint8_t a[32], const uint8_t b[32])
  {
    uint8_t d = 0;
    for (int i = 0; i < 32; i++)
      d |= a[i] ^ b[i];
    return d == 0;
  }

  inline uint32_t NewNonce()
  {
    std::random_device rd;
    uint32_t n;
    do { n = ((uint32_t) rd() << 16) ^ (uint32_t) rd(); } while (n == 0);
    return n;
  }

  // The ini value -> the password ("" = public). "DPAPI:..." is decrypted; a value that cannot be decrypted
  // gives a random password nobody knows (the room stays closed) and sets *unreadable.
  inline std::string FromIni(const std::string &value, bool *unreadable = nullptr)
  {
    if (unreadable) *unreadable = false;
    std::string v = value;
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.pop_back();
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(0, 1);
    if (v.empty())
      return "";
    if (v.compare(0, strlen(StreamDpapi::kPrefix), StreamDpapi::kPrefix) != 0)
      return v.substr(0, kMaxLength());
    std::string plain;
    if (StreamDpapi::Unprotect(v, plain))
      return plain.substr(0, kMaxLength());
    if (unreadable) *unreadable = true;
    uint8_t r[32];
    uint32_t n = NewNonce();
    Sha256(&n, sizeof(n), r);
    std::string locked = "!locked!";
    for (int i = 0; i < 16; i++)
      locked.push_back("0123456789abcdef"[r[i] & 15]);
    return locked;
  }
}

#endif // INCLUDED_STREAM_ROOMPASSWORD_H
