/*
 * Lobby.h
 *
 * Streaming (P5, 2026-11-16): the lobby screen of the settings GUI (design 5.1/5.2, requirements CL-02,
 * CL-02b, LB-04). It opens instead of launching when the selected game is spikeofe (the ROM is still
 * required, CL-02). For now it only has the direct IP entry; the Firebase host list comes in P6.
 *
 * Exits: Connect -> the GUI closes and Main runs the stream client (StreamClient), then comes back here.
 *        Skip    -> the normal local spikeofe launch (no streaming).
 *        Back    -> the normal settings GUI.
 *
 * The lobby's own settings (last host, slot, name) are kept in streamclient.ini in the working folder,
 * not in Supermodel.ini (the GUI's ini writer would append new keys under the wrong section).
 */

#ifndef INCLUDED_STREAM_LOBBY_H
#define INCLUDED_STREAM_LOBBY_H

#include <string>

struct StreamConnectRequest
{
  bool requested = false;
  std::string host;
  int slot = 1;
  std::string name;
  std::string password;   // SV-18: room password typed in the lobby (kept in memory only, never saved)
  std::string serverName; // for the password box title
  bool netlink = false;   // 11/23: a NETLINK slot - the GUI launches the game's ROM and Main joins as the slot's clone
};

namespace Lobby
{
  enum class Action { None, Connect, Skip, Back };

  // Open the lobby (from the GUI's LAUNCH, or by Main when coming back from the client with a message).
  void Open(const std::string &message = "");
  void Close();
  bool IsOpen();

  // Draw the lobby as the whole GUI window content. Returns what the user chose this frame.
  Action Draw(float scale);

  // Filled by Draw() when it returns Action::Connect; cleared by Main after the client ran.
  StreamConnectRequest &Request();

  // SV-18: open the password box for this host / slot (the server asked for a password, or it was wrong)
  void AskPassword(const StreamConnectRequest &req, bool wrong);

  // 11/19 (owner): the pad the client sends (chosen in the lobby, kept in streamclient.ini as StreamPad /
  // StreamPadName). The client opens the pad with this name first, else this SDL index (0-based), else the first.
  int PadIndex();
  std::string PadName();

  // 11/24 (owner): where the game's ROM is (the GUI's ROM folder + spikeofe.zip). Joining as NETLINK needs it.
  void SetRomFile(const std::string &path);
}

#endif // INCLUDED_STREAM_LOBBY_H
