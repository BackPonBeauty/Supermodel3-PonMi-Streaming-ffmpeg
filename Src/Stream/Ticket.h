/*
 * Ticket.h
 *
 * 11/16 (owner): entry tickets (requirement SV-16, design 3.8). The client's Discord token goes only to the
 * entry-ticket Worker (Server/ticket-worker, Cloudflare), never to a host. The Worker checks the guild membership and
 * signs {"v":1,"id","name","host","slot","nonce","exp"} with ECDSA P-256. The host checks the signature with the
 * public key embedded in the exe (StreamSecrets::ticketPublicKey) and that host / slot / nonce are its own.
 * A collected ticket is useless elsewhere: it names one host, one slot and one single-use nonce.
 */

#ifndef INCLUDED_STREAM_TICKET_H
#define INCLUDED_STREAM_TICKET_H

#include <cstdint>
#include <string>

namespace Ticket
{
  constexpr int kSigLength = 64;        // r | s, 32 bytes each
  constexpr int kMaxPayload = 400;

  bool Configured();                    // the build has the Worker URL and public key

  // --- Client: ask the Worker. On Ok, payload (JSON bytes) and sig (64 bytes) are filled.
  enum class Fetch { Ok, LoginInvalid, NotMember, Unavailable, NotConfigured };
  Fetch Request(const std::string &discordToken, const std::string &host, int slot, uint32_t nonce,
                std::string &payload, std::string &sig);

  // --- Host: signature check + fields. False if the signature is wrong or the payload is not a v1 ticket.
  struct Info
  {
    std::string id;     // Discord user ID
    std::string name;
    std::string host;
    int         slot = 0;
    uint32_t    nonce = 0;
    long long   exp = 0; // unix seconds
  };
  bool Verify(const std::string &payload, const std::string &sig, Info &out);
}

#endif // INCLUDED_STREAM_TICKET_H
