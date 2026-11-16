/*
 * StreamClient.h
 *
 * Streaming (P5, 2026-11-16): the client side (design 5.3, requirements CL-01, CL-03..12, IN-01/02/05).
 * Runs after the settings GUI has closed (the lobby asked to connect); no emulation.
 *
 * - Session: HELLO -> WELCOME, heartbeat every 2 s, PING every 0.5 s (F3), STATUS (video loss) every 1 s,
 *   KEYREQ at the start and after packet loss, BYE when leaving. No answer from the server for 6 s -> leave.
 * - Video: RTP (H.264/H.265, as sent by RtpSender) -> Annex-B frames -> avcodec (CPU, design D-2) ->
 *   RGBA texture, drawn keeping the aspect ratio (CL-08). After a loss, frames are skipped until the next
 *   keyframe.
 * - Audio: 4-byte timestamp + Opus (48 kHz stereo, 20 ms) -> SDL audio queue (kept short).
 * - Input: keyboard W/A/S/D and Up/Down (bitrate) only while the window has the focus; pad 1 (SDL joystick:
 *   32 buttons, 6 axes, 1 hat) also in the background (IN-05). Sent every 8 ms and on every change.
 * - Keys: F11 fullscreen, F3 latency overlay (top left), ESC: fullscreen -> window, window -> leave.
 */

#ifndef INCLUDED_STREAM_STREAMCLIENT_H
#define INCLUDED_STREAM_STREAMCLIENT_H

#include "Lobby.h"
#include <string>

enum class StreamClientExit
{
  BackToLobby,   // ESC, server gone, refused: show the lobby again with `message`
  QuitApp        // the window was closed
};

StreamClientExit RunStreamClient(const StreamConnectRequest &request, std::string &message);

#endif // INCLUDED_STREAM_STREAMCLIENT_H
