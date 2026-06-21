/**
 * XinputReceiverDialog.h
 *
 * Win32 Dialog - XInput Remote Controller Management UI
 * A separate window equivalent to Form1 in VB.NET.
 *
 * Appears alongside the emulator to toggle LOCAL/REMOTE
 * for slots P1 to P4.
 */
#pragma once

#ifdef SUPERMODEL_WIN32

#include <windows.h>
#include <string>
#include <thread>
#include "Remote/RemoteSlotManager.h"

class XinputReceiverDialog
{
public:
    XinputReceiverDialog();
    ~XinputReceiverDialog();

    // Start asynchronously alongside the emulator (creates window in a separate thread)
    bool StartAsync();
    // Close the window and stop the thread
    void Stop();

    bool IsRunning() const { return m_running; }

    // Access to RemoteSlotManager (usable from the emulator side)
    RemoteSlotManager& GetSlotManager() { return m_slotManager; }

private:
    // Window thread body
    void WindowThread();

    // Win32 Callbacks
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // UI Creation and Updates
    void CreateControls(HWND hwnd);
    void UpdateSlotUI(int slot);
    void UpdateStatusBar(const std::string& msg);

    // Triggered when a slot button is clicked
    void OnSlotButtonClicked(int slot);

    // Window constants
    static constexpr int WM_UPDATE_SLOT   = WM_USER + 1;
    static constexpr int WM_UPDATE_STATUS = WM_USER + 2;

    // Control IDs
    static constexpr int IDC_BTN_SLOT_BASE  = 100; // 100 to 103: Slot 1 to 4 buttons
    static constexpr int IDC_LBL_SLOT_BASE  = 110; // 110 to 113: Slot 1 to 4 labels
    static constexpr int IDC_LBL_HOSTID     = 120;
    static constexpr int IDC_LBL_IP         = 121;
    static constexpr int IDC_LBL_VIGEM      = 122;
    static constexpr int IDC_LBL_FIREBASE   = 123;
    static constexpr int IDC_LBL_STATUS     = 124;

    HWND m_hwnd = nullptr;
    HWND m_btnSlot[5]  = {};  // Indexes 1 to 4
    HWND m_lblSlot[5]  = {};  // Indexes 1 to 4
    HWND m_lblHostId   = nullptr;
    HWND m_lblIp       = nullptr;
    HWND m_lblViGEm    = nullptr;
    HWND m_lblFirebase = nullptr;
    HWND m_lblStatus   = nullptr;

    std::thread       m_thread;
    std::atomic<bool> m_running{ false };

    RemoteSlotManager m_slotManager;
    std::string       m_pendingStatus;
};

// Global instance (accessed from Main.cpp)
extern XinputReceiverDialog* g_xinputDialog;

#endif // SUPERMODEL_WIN32
