/**
 * RemoteSlotManager.cpp
 *
 * Implementation of slots P1 to P4 management.
 */

#ifdef SUPERMODEL_WIN32

#include "RemoteSlotManager.h"
#include <xinput.h>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <random>
#include "UPnPHelper.h"
#include "Network/HandshakeServer.h"
#include "Util/NewConfig.h"
#include <cmath>

// #pragma comment(lib, "xinput.lib")

static HMODULE s_xinputDll = nullptr;
typedef DWORD(WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE *);
static PFN_XInputGetState s_XInputGetState = nullptr;

static void LoadXInputForRemote()
{
    if (s_xinputDll)
        return;
    const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (auto dll : dlls)
    {
        s_xinputDll = LoadLibraryA(dll);
        if (s_xinputDll)
        {
            s_XInputGetState = (PFN_XInputGetState)GetProcAddress(s_xinputDll, "XInputGetState");
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// XInput Physical Controller Detection
// ---------------------------------------------------------------------------
bool RemoteSlotManager::IsXInputConnected(int userIndex)
{
    LoadXInputForRemote();
    if (!s_XInputGetState)
        return false;
    XINPUT_STATE state = {};
    DWORD result = s_XInputGetState(userIndex, &state);
    return result == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
RemoteSlotManager::RemoteSlotManager()
{
    for (int i = 1; i <= 4; i++)
    {
        m_slots[i].mode = SlotMode::LOCAL;
        m_slots[i].isPhysical = false;
        m_slots[i].isConnected = false;
        m_slots[i].label = "";
    }
}

RemoteSlotManager::~RemoteSlotManager()
{
    Shutdown();
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
bool RemoteSlotManager::Initialize(SlotChangedCallback slotCb,
                                   StatusCallback statusCb)
{
    m_slotCb = slotCb;
    m_statusCb = statusCb;

    NotifyStatus("Initializing...");

    // Initialize Winsock
    XinputReceiver::InitWinsock();

    // Initialize ViGEm
    if (!m_vigem.Initialize())
    {
        NotifyStatus("warning: " + m_vigem.GetStatusMessage());
        // Continue even if ViGEm fails (only local mode will work)
    }
    else
    {
        NotifyStatus(m_vigem.GetStatusMessage());
    }

    // Detect physical controllers
    DetectPhysicalControllers();

    // Generate Host ID (8 random hex characters)
    m_hostId = GenerateHostId();

    // Get external IP address (asynchronously in a separate thread)
    std::thread([this]()
                {
        m_externalIp = FirebaseMatchingCpp::GetExternalIp();
        if (!m_externalIp.empty())
            NotifyStatus("output IP: " + m_externalIp); })
        .detach();

    // Initialize Firebase (asynchronously in a separate thread)
    std::thread([this]()
                {
        auto cb = [this](const std::string& msg) { NotifyStatus(msg); };
        if (m_firebase.Initialize(cb))
        {
            m_firebase.CleanupStaleHosts();
            UpdateAvailableSlots(); // Initial registration
            m_firebase.StartHeartbeat(m_hostId);
        } })
        .detach();

    NotifyStatus("Ready");
    return true;
}

void RemoteSlotManager::Shutdown()
{
    if (m_shutdownCalled)
        return;
    m_shutdownCalled = true;

    if (m_linkplay >= 1 && m_linkplay <= 4)
        UPnPHelper::CloseStreamingPorts(m_linkplay);

    m_xinput.StopAll();
    for (int i = 1; i <= 4; i++)
        m_vigem.RemoveController(i);
    m_virtualControllerAdded = false;
    m_firebase.Shutdown();
    m_vigem.Shutdown();
    
    XinputReceiver::CleanupWinsock();
}

// ---------------------------------------------------------------------------
// Standalone ViGEm Operations (for GUI integration and startup)
// ---------------------------------------------------------------------------
bool RemoteSlotManager::InitViGEm()
{
    if (m_vigem.IsInitialized())
        return true; // Already initialized

    // Winsock initialized first
    XinputReceiver::InitWinsock();

    if (!m_vigem.Initialize())
    {
        printf("[RemoteSlotManager] InitViGEm failed: %s\n", m_vigem.GetStatusMessage().c_str());
        return false;
    }
    printf("[RemoteSlotManager] InitViGEm OK\n");
    return true;
}

bool RemoteSlotManager::AddVirtualController()
{
    if (m_virtualControllerAdded)
    {
        printf("[RemoteSlotManager] AddVirtualController: already added\n");
        return true;
    }
    if (!m_vigem.IsInitialized())
    {
        if (!InitViGEm())
            return false;
    }
    if (!m_vigem.AddController(1))
    {
        printf("[RemoteSlotManager] AddVirtualController failed: %s\n", m_vigem.GetStatusMessage().c_str());
        return false;
    }
    m_virtualControllerAdded = true;
    printf("[RemoteSlotManager] AddVirtualController: slot 1 created\n");
    return true;
}

void RemoteSlotManager::RemoveVirtualController()
{
    if (!m_virtualControllerAdded)
        return;
    m_xinput.StopListening(1);
    m_vigem.RemoveController(1);
    m_vigem.RemoveController(2);
    m_virtualControllerAdded = false;
    printf("[RemoteSlotManager] RemoveVirtualController: slots removed\n");
}

bool RemoteSlotManager::StartListening(int linkplay)
{
    if (linkplay < 0 || linkplay > 4)
    {
        printf("[RemoteSlotManager] StartListening: invalid linkplay=%d\n", linkplay);
        return false;
    }

    m_linkplay = linkplay; // Save INI value

    if (linkplay == 0)
    {
        // LinkPlay=0: Set up both P1 (slot 1) and P2 (slot 2) as remote
        if (!m_vigem.IsInitialized())
        {
            if (!InitViGEm()) return false;
        }

        // Add virtual controllers to slots 1 and 2
        m_vigem.AddController(1);
        m_vigem.AddController(2);
        m_virtualControllerAdded = true;

        // Open UPnP ports (asynchronous)
        std::thread([]()
                    {
                        UPnPHelper::OpenStreamingPorts(1);
                        UPnPHelper::OpenStreamingPorts(2);
                    })
            .detach();

        auto cb = [this](int s, const XInputPacket &p, const std::string &ip, int portVal)
        { OnXInputReceived(s, p, ip, portVal); };

        int p1Port = FirebaseMatchingCpp::GetSlotXInputPort(1);
        int p2Port = FirebaseMatchingCpp::GetSlotXInputPort(2);

        // Start UDP receiving on both slots 1 and 2
        m_xinput.StartListening(1, p1Port, cb);
        m_xinput.StartListening(2, p2Port, cb);

        m_slots[1].mode = SlotMode::REMOTE;
        m_slots[1].isConnected = false;
        m_slots[1].label = "Standing by... (P1, port " + std::to_string(p1Port) + ")";

        m_slots[2].mode = SlotMode::REMOTE;
        m_slots[2].isConnected = false;
        m_slots[2].label = "Standing by... (P2, port " + std::to_string(p2Port) + ")";

        NotifySlotChanged(1);
        NotifySlotChanged(2);

        printf("[RemoteSlotManager] StartListening (LinkPlay=0): P1 (port %d) & P2 (port %d) ACTIVE\n", p1Port, p2Port);
    }
    else
    {
        // Traditional single slot
        if (!m_virtualControllerAdded)
        {
            printf("[RemoteSlotManager] StartListening: no virtual controller, call AddVirtualController first\n");
            return false;
        }

        int port = FirebaseMatchingCpp::GetSlotXInputPort(linkplay);

        // Open UPnP ports (asynchronous)
        std::thread([linkplay]()
                    { UPnPHelper::OpenStreamingPorts(linkplay); })
            .detach();

        auto cb = [this](int s, const XInputPacket &p, const std::string &ip, int portVal)
        { OnXInputReceived(s, p, ip, portVal); };
        m_xinput.StartListening(1, port, cb);

        m_slots[1].mode = SlotMode::REMOTE;
        m_slots[1].isConnected = false;
        m_slots[1].label = "Standing by... (port " + std::to_string(port) + ")";
        NotifySlotChanged(1);

        printf("[RemoteSlotManager] StartListening: linkplay=%d port=%d\n", linkplay, port);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Start Firebase Thread on Launch (independent from Initialize())
// ---------------------------------------------------------------------------
void RemoteSlotManager::StartFirebaseAsync(const std::string &gameTitle, const std::string &serverName)
{
    if (m_hostId.empty())
        m_hostId = GenerateHostId();

    m_gameTitle = gameTitle; // Save ROM name
    m_serverName = serverName; // Save server name
    printf("[RemoteSlotManager] StartFirebaseAsync: hostId=%s gameTitle=%s serverName=%s\n",
           m_hostId.c_str(), m_gameTitle.c_str(), m_serverName.c_str());

    // Get external IP -> Initialize Firebase, register host, start heartbeat sequentially in one thread
    std::thread([this]()
                {
        // Get external IP first
        m_externalIp = FirebaseMatchingCpp::GetExternalIp();
        printf("[RemoteSlotManager] External IP: %s\n", m_externalIp.c_str());

        m_hostId = m_externalIp;
        for (char& c : m_hostId)
            if (c == '.') c = '-';

        printf("[RemoteSlotManager] HostId: %s\n", m_hostId.c_str());

        if (m_externalIp.empty() || m_externalIp == "127.0.0.1")
        {
            printf("[RemoteSlotManager] Invalid IP, skipping Firebase\n");
            return;
        }

        auto cb = [this](const std::string& msg) { NotifyStatus(msg); };
        if (m_firebase.Initialize(cb))
        {
            m_firebase.CleanupStaleHosts();
            UpdateAvailableSlots();
            m_firebase.StartHeartbeat(m_hostId);
        }
        else
        {
            printf("[RemoteSlotManager] Firebase init failed\n");
        } })
        .detach();
}

// ---------------------------------------------------------------------------
// Detect Physical Controllers
// ---------------------------------------------------------------------------
void RemoteSlotManager::DetectPhysicalControllers()
{
    int physicalCount = 0;
    for (int i = 0; i <= 3; i++)
    {
        if (IsXInputConnected(i))
            physicalCount++;
        else
            break;
    }

    for (int slot = 1; slot <= 4; slot++)
    {
        if (slot <= physicalCount)
        {
            m_slots[slot].isPhysical = true;
            m_slots[slot].mode = SlotMode::LOCAL;
            m_slots[slot].isConnected = false;
            m_slots[slot].label = "Local Controller #" + std::to_string(slot);
        }
        else
        {
            m_slots[slot].isPhysical = false;
            m_slots[slot].mode = SlotMode::LOCAL;
            m_slots[slot].isConnected = false;
            m_slots[slot].label = "";
        }
        NotifySlotChanged(slot);
    }

    printf("[RemoteSlotManager] Local Controller %d found\n", physicalCount);
}

// ---------------------------------------------------------------------------
// Slot Mode Toggle
// ---------------------------------------------------------------------------
bool RemoteSlotManager::ToggleSlotMode(int slot)
{
    if (!IsValidSlot(slot))
        return false;
    if (m_slots[slot].isPhysical)
        return false; // Physical controllers cannot be changed

    if (m_slots[slot].mode == SlotMode::LOCAL)
        return SetRemote(slot);
    else
        return SetLocal(slot);
}

bool RemoteSlotManager::SetRemote(int slot)
{
    if (!IsValidSlot(slot) || m_slots[slot].isPhysical)
        return false;

    // Add ViGEm virtual controller
    if (m_vigem.IsInitialized())
    {
        if (!m_vigem.AddController(slot))
        {
            NotifyStatus("Slot " + std::to_string(slot) + ": Virtual controller failed to initialize");
            return false;
        }
    }

    // Start UDP receiving
    int port = XinputReceiver::SlotToPort(slot);
    auto cb = [this](int s, const XInputPacket &p, const std::string &ip, int portVal)
    { OnXInputReceived(s, p, ip, portVal); };
    m_xinput.StartListening(slot, port, cb);

    m_slots[slot].mode = SlotMode::REMOTE;
    m_slots[slot].isConnected = false;
    m_slots[slot].label = "Standing by... (port " + std::to_string(port) + ")";

    NotifySlotChanged(slot);
    UpdateAvailableSlots();

    printf("[RemoteSlotManager] Slot %d → REMOTE (port=%d)\n", slot, port);
    return true;
}

bool RemoteSlotManager::SetLocal(int slot)
{
    if (!IsValidSlot(slot))
        return false;

    // Stop UDP receiving
    m_xinput.StopListening(slot);

    // Remove ViGEm virtual controller
    m_vigem.RemoveController(slot);

    m_slots[slot].mode = SlotMode::LOCAL;
    m_slots[slot].isConnected = false;
    m_slots[slot].label = "";

    NotifySlotChanged(slot);
    UpdateAvailableSlots();

    printf("[RemoteSlotManager] Slot %d → LOCAL\n", slot);
    return true;
}

// ---------------------------------------------------------------------------
// XInput Receiving Callback (called from thread)
// ---------------------------------------------------------------------------
void RemoteSlotManager::OnXInputReceived(int slot, const XInputPacket &packet, const std::string &fromIP, int fromPort)
{
    if (!IsValidSlot(slot) || !m_vigem.IsInitialized())
        return;
    bool inputChanged = (packet.wButtons != m_slots[slot].lastPacket.wButtons) ||
                        (packet.bLeftTrigger != m_slots[slot].lastPacket.bLeftTrigger) ||
                        (packet.bRightTrigger != m_slots[slot].lastPacket.bRightTrigger) ||
                        (packet.sThumbLX != m_slots[slot].lastPacket.sThumbLX) ||
                        (packet.sThumbLY != m_slots[slot].lastPacket.sThumbLY) ||
                        (packet.sThumbRX != m_slots[slot].lastPacket.sThumbRX) ||
                        (packet.sThumbRY != m_slots[slot].lastPacket.sThumbRY);

    m_slots[slot].lastPacket = packet;

    // When LinkPlay=0, Slot 1 uses g_handshake and Slot 2 uses g_handshakeP2 to verify the IP
    if (m_linkplay == 0)
    {
        if (slot == 1)
        {
            std::string controllerIP = g_handshake.GetControllerIP();
            if (fromIP != controllerIP) return;
            if (inputChanged)
                g_handshake.NotifyControllerInput(fromIP, fromPort);
        }
        else if (slot == 2)
        {
            std::string controllerIP = g_handshakeP2.GetControllerIP();
            if (fromIP != controllerIP) return;
            if (inputChanged)
                g_handshakeP2.NotifyControllerInput(fromIP, fromPort);
        }
        else
        {
            return; // Slots P3/P4 are not accepted when LinkPlay=0
        }
    }
    else
    {
        // Verify for single slot (traditional)
        std::string controllerIP = g_handshake.GetControllerIP();
        if (fromIP != controllerIP)
        {
            return; // Ignore inputs from clients without operational authority
        }
        if (inputChanged)
            g_handshake.NotifyControllerInput(fromIP, fromPort);
    }

    // Convert XInputPacket to XUSB_REPORT
    XUSB_REPORT report = {};
    report.wButtons = packet.wButtons;
    report.bLeftTrigger = packet.bLeftTrigger;
    report.bRightTrigger = packet.bRightTrigger;
    report.sThumbLX = packet.sThumbLX;
    report.sThumbLY = packet.sThumbLY;
    report.sThumbRX = packet.sThumbRX;
    report.sThumbRY = packet.sThumbRY;

    m_vigem.UpdateController(slot, report);
    m_vigem.UpdateController(slot, report);

    if (!m_slots[slot].isConnected)
    {
        m_slots[slot].isConnected = true;
        m_slots[slot].label = "connecting (Remote)";
        NotifySlotChanged(slot);
    }
}

// ---------------------------------------------------------------------------
// Update slot states on Firebase
// ---------------------------------------------------------------------------
void RemoteSlotManager::UpdateAvailableSlots()
{
    if (!m_firebase.IsInitialized() || m_externalIp.empty())
        return;

    // INI設定のStreaming有効・無効をリアルタイムに取得
    // (s_runtime_config は Main.cpp で設定されるグローバル設定ノードです)
    extern Util::Config::Node s_runtime_config;
    bool streamingEnabled = s_runtime_config["Streaming"].ValueAsDefault<bool>(false);

    bool available[5] = {false};
    if (streamingEnabled)
    {
        if (m_linkplay == 0)
        {
            available[1] = true;
            available[2] = true;
        }
        else
        {
            available[m_linkplay] = true;
        }
    }
    // streamingEnabled が false の場合は available は全て false のままになります

    // Key = sanitized IP, PUT/PATCH based on linkplay number
    m_firebase.RegisterHost(m_externalIp, available, m_linkplay, m_gameTitle, m_serverName);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
const SlotState &RemoteSlotManager::GetSlotState(int slot) const
{
    static SlotState empty;
    if (!IsValidSlot(slot))
        return empty;
    return m_slots[slot];
}

void RemoteSlotManager::NotifySlotChanged(int slot)
{
    if (m_slotCb && IsValidSlot(slot))
        m_slotCb(slot, m_slots[slot]);
}

void RemoteSlotManager::NotifyStatus(const std::string &msg)
{
    printf("[RemoteSlotManager] %s\n", msg.c_str());
    if (m_statusCb)
        m_statusCb(msg);
}

std::string RemoteSlotManager::GenerateHostId()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char hex[] = "0123456789abcdef";
    std::string id(8, '0');
    for (char &c : id)
        c = hex[dis(gen)];
    return id;
}

void RemoteSlotManager::SetSlotOccupied()
{
    if (!m_firebase.IsInitialized() || m_hostId.empty())
        return;
    std::thread([this]()
                {
                    if (m_linkplay == 0)
                    {
                        m_firebase.PatchSlotAvailable(m_hostId, 1, false);
                        m_firebase.PatchSlotAvailable(m_hostId, 2, false);
                    }
                    else
                    {
                        m_firebase.PatchSlotAvailable(m_hostId, m_linkplay, false);
                    }
                })
        .detach();
    printf("[RemoteSlotManager] SetSlotOccupied for linkplay=%d\n", m_linkplay);
}

void RemoteSlotManager::SetSlotAvailable()
{
    if (!m_firebase.IsInitialized() || m_hostId.empty())
        return;
    std::thread([this]()
                {
                    if (m_linkplay == 0)
                    {
                        m_firebase.PatchSlotAvailable(m_hostId, 1, true);
                        m_firebase.PatchSlotAvailable(m_hostId, 2, true);
                    }
                    else
                    {
                        m_firebase.PatchSlotAvailable(m_hostId, m_linkplay, true);
                    }
                })
        .detach();
    printf("[RemoteSlotManager] SetSlotAvailable for linkplay=%d\n", m_linkplay);
}
#endif // SUPERMODEL_WIN32
