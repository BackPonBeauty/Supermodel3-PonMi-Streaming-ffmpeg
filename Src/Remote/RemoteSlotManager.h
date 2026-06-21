/**
 * RemoteSlotManager.h
 *
 * State management for slots P1 to P4 (integrating ViGEm, UDP reception, and Firebase).
 *
 * Corresponds to the core logic of Form1.vb on the VB.NET side.
 */
#pragma once

#ifdef SUPERMODEL_WIN32

#include <windows.h>
#include <array>
#include <string>
#include <functional>
#include "ViGEmManager.h"
#include "XinputReceiver.h"
#include "FirebaseMatchingCpp.h"
#include "UPnPHelper.h"

// Slot modes
enum class SlotMode
{
    LOCAL, // Physical controller (or direct emulator input)
    REMOTE // Receive remote XInput via UDP
};

// Slot states
struct SlotState
{
    SlotMode mode = SlotMode::LOCAL;
    bool isPhysical = false;  // Physical controller is plugged in
    bool isConnected = false; // Remotely connected (receiving UDP)
    std::string label;        // Display label
    XInputPacket lastPacket = {}; // Store last input packet for change detection
};

using SlotChangedCallback = std::function<void(int slot, const SlotState &state)>;
using StatusCallback = std::function<void(const std::string &message)>;

class RemoteSlotManager
{
public:
    RemoteSlotManager();
    ~RemoteSlotManager();

    // Initialization (ViGEm connection, physical controller detection, Firebase initialization)
    bool Initialize(SlotChangedCallback slotCb = nullptr,
                    StatusCallback statusCb = nullptr);
    void Shutdown();

    // Toggle slot mode (LOCAL <=> REMOTE)
    bool ToggleSlotMode(int slot);

    // Set slot to remote mode and start receiving UDP
    bool SetRemote(int slot);
    // Set slot to local mode and stop receiving UDP
    bool SetLocal(int slot);

    // Get slot state
    const SlotState &GetSlotState(int slot) const;

    // Host information
    const std::string &GetHostId() const { return m_hostId; }
    const std::string &GetExternalIp() const { return m_externalIp; }

    // ViGEm & Firebase state
    bool IsViGEmReady() const { return m_vigem.IsInitialized(); }
    bool IsFirebaseReady() const { return m_firebase.IsInitialized(); }

    // --- Standalone ViGEm Operations (for GUI integration and startup) ---
    // Initialize ViGEm only (does not start XInput receiving)
    bool InitViGEm();
    // Create one virtual controller for Slot 1
    bool AddVirtualController();
    // Remove the virtual controller for Slot 1
    void RemoveVirtualController();
    // Start UDP receiving for Slot 1 (port = 5000 + (linkplay-1)*4)
    bool StartListening(int linkplay);
    // Start Firebase thread asynchronously on launch
    // gameTitle: ROM name (e.g., spikeofe)
    void StartFirebaseAsync(const std::string &gameTitle = "", const std::string &serverName = "");

    // Check physical XInput controller connection (uses xinput1_4.dll)
    static bool IsXInputConnected(int userIndex); // userIndex: 0 to 3

    void SetSlotOccupied();
    void SetSlotAvailable();

    

private:
    void DetectPhysicalControllers();
    void OnXInputReceived(int slot, const XInputPacket &packet, const std::string &fromIP, int fromPort);
    void UpdateAvailableSlots();
    std::string GenerateHostId();

    ViGEmManager m_vigem;
    XinputReceiver m_xinput;
    FirebaseMatchingCpp m_firebase;

    std::array<SlotState, 5> m_slots; // Indexes 1 to 4 are used
    std::string m_hostId;
    std::string m_externalIp;

    SlotChangedCallback m_slotCb;
    StatusCallback m_statusCb;

    // Whether the virtual controller has been added (fixed to Slot 1)
    bool m_virtualControllerAdded = false;
    // linkplay number read from INI (1 to 4)
    int m_linkplay = 0;
    // Name of the running ROM (e.g., spikeofe)
    std::string m_gameTitle;
    std::string m_serverName;

    bool IsValidSlot(int slot) const { return slot >= 1 && slot <= 4; }
    void NotifySlotChanged(int slot);
    void NotifyStatus(const std::string &msg);

    bool m_shutdownCalled = false;
};

#endif // SUPERMODEL_WIN32
