/**
 * RemoteSlotManager.h
 *
 * Lobby state of this server's slot: Firebase registration (host, slot availability,
 * player name, heartbeat) and UPnP port opening.
 *
 * P1 (2026-11-16): the ViGEm virtual pad and the XInput UDP receiver were removed.
 * Remote input comes back as a CInputSystem (RemoteInputSystem, design P3) and the
 * session handling as SessionServer (design P4).
 */

#pragma once

#ifdef SUPERMODEL_WIN32

#include <string>
#include <functional>
#include "FirebaseMatchingCpp.h"
#include "UPnPHelper.h"

using StatusCallback = std::function<void(const std::string &message)>;

class RemoteSlotManager
{
public:
    RemoteSlotManager() = default;
    ~RemoteSlotManager();

    void Shutdown();

    // Remember this server's slot (ini LinkPlay) and open its streaming ports with UPnP (asynchronous).
    void StartStreaming(int linkplay);

    // Register the host and this slot on Firebase and start the heartbeat (asynchronous).
    // gameTitle: ROM name (e.g. spikeofe)
    void StartFirebaseAsync(const std::string &gameTitle = "", const std::string &serverName = "");

    // Host information
    const std::string &GetHostId() const { return m_hostId; }
    const std::string &GetExternalIp() const { return m_externalIp; }
    bool IsFirebaseReady() const { return m_firebase.IsInitialized(); }

    void SetStatusCallback(StatusCallback cb) { m_statusCb = cb; }

    // Slot state on Firebase
    void SetSlotOccupied();
    void SetSlotAvailable();
    void SetSlotPlayer(int slot, const std::string &player);
    void SetSlotSpectator(int slot, const std::string &spectator);
    void SetSlotClientCount(int slot, int count);

private:
    void UpdateAvailableSlots();
    void NotifyStatus(const std::string &msg);

    FirebaseMatchingCpp m_firebase;

    std::string m_hostId;
    std::string m_externalIp;
    StatusCallback m_statusCb;

    // linkplay number read from INI
    int m_linkplay = 0;
    bool m_portsOpened = false;

    // Name of the running ROM (e.g., spikeofe)
    std::string m_gameTitle;
    std::string m_serverName;

    bool m_shutdownCalled = false;
};

#endif // SUPERMODEL_WIN32
