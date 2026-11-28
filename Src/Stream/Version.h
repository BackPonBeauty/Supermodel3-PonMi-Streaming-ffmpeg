/*
 * Version.h
 *
 * 11/16 (owner): the build number of main5 (host and client are the same supermodel.exe), compared with the
 * newest one published in Firebase at /main5/supermodel (written by the owner in the Firebase console only;
 * the database rules make /main5 read-only for the program). When Firebase has a larger number:
 *   - the client (lobby) shows "please update" and does not connect (playing locally with SKIP still works),
 *   - the launcher does not START.
 * /main5/url (optional) is the download page; kDefaultDownloadUrl otherwise.
 *
 * Raise kBuild (YYYYMMDD, or YYYYMMDDn for several builds a day) for every release, then the number in Firebase.
 * "supermodel.exe -build-version" prints it (the launcher reads it from there).
 */

#ifndef INCLUDED_STREAM_VERSION_H
#define INCLUDED_STREAM_VERSION_H

namespace StreamVersion
{
  constexpr long long kBuild = 20261127;   // 11/23: release 20261123 (netlink); 11/23 night: release 20261124 (bots); 11/24: 20261125; 11/26: 20261126; 11/28: 20261127
  constexpr const char *kDefaultDownloadUrl = "https://github.com/BackPonBeauty/Supermodel3-PonMi-Streaming-ffmpeg/releases";
}

#endif // INCLUDED_STREAM_VERSION_H
