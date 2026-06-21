/**
 * XinputReceiver.h
 *
 * Receives remote XInput state via UDP and forwards it to the ViGEm controller.
 *
 * Port assignment (matching the VB.NET side):
 *   Slot 1: XInput=5000
 *   Slot 2: XInput=5004
 *   Slot 3: XInput=5008
 *   Slot 4: XInput=5012
 */
#pragma once

#ifdef SUPERMODEL_WIN32

// Prevent double-inclusion conflict between winsock.h and winsock2.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_   // Suppress automatic inclusion of winsock.h
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <atomic>
#include <functional>
#include <array>
#include <string>

// XInput state packet (structure sent/received via UDP, must match the VB.NET side)
#pragma pack(push, 1)
struct XInputPacket
{
    WORD  wButtons;       // Button bitfield (XINPUT_GAMEPAD_* constants)
    BYTE  bLeftTrigger;   // Left trigger 0 to 255
    BYTE  bRightTrigger;  // Right trigger 0 to 255
    SHORT sThumbLX;       // Left stick X-axis -32768 to 32767
    SHORT sThumbLY;       // Left stick Y-axis
    SHORT sThumbRX;       // Right stick X-axis
    SHORT sThumbRY;       // Right stick Y-axis
};
#pragma pack(pop)

// Receive callback: slot=1 to 4, packet=received data, fromIP=source IP, fromPort=source port
using XInputCallback = std::function<void(int slot, const XInputPacket& packet, const std::string& fromIP, int fromPort)>;

class XinputReceiver
{
public:
    XinputReceiver();
    ~XinputReceiver();

    // Winsock initialization (once for the whole program)
    static bool InitWinsock();
    static void CleanupWinsock();

    // Start UDP receiving for the specified slot
    bool StartListening(int slot, int port, XInputCallback callback);
    // Stop UDP receiving for the specified slot
    void StopListening(int slot);
    // Stop all slots
    void StopAll();

    bool IsListening(int slot) const;
    std::string GetLastError() const { return m_lastError; }

    // Get port number from slot number
    static int SlotToPort(int slot);

private:
    struct SlotState
    {
        std::thread       thread;
        std::atomic<bool> running{ false };
        SOCKET            sock = INVALID_SOCKET;
    };

    void ListenThread(int slot, int port, XInputCallback callback);

    std::array<SlotState, 5> m_slots; // Indexes 1 to 4 are used
    std::string              m_lastError;

    bool IsValidSlot(int slot) const { return slot >= 1 && slot <= 4; }
};

#endif // SUPERMODEL_WIN32
