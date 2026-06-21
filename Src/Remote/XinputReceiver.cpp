/**
 * XinputReceiver.cpp
 *
 * Implementation of remote XInput state reception via UDP.
 */

#ifdef SUPERMODEL_WIN32

#include "XinputReceiver.h"
#include <ws2tcpip.h>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

// Slot number -> XInput UDP port number
// Must match SlotXInputPort in VB.NET (Form1.vb)
static const int s_slotPorts[5] = {0, 55000, 55004, 55008, 55012};

// ---------------------------------------------------------------------------
// Static Members: Winsock Initialization / Cleanup
// ---------------------------------------------------------------------------
bool XinputReceiver::InitWinsock()
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "WSAStartup failed: %d", result);
        return false;
    }
    return true;
}

void XinputReceiver::CleanupWinsock()
{
    WSACleanup();
}

#include "FirebaseMatchingCpp.h"

int XinputReceiver::SlotToPort(int slot)
{
    if (slot < 1 || slot > 4)
        return 0;
    return FirebaseMatchingCpp::GetSlotXInputPort(slot);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
XinputReceiver::XinputReceiver()
{
}

// Destructor
XinputReceiver::~XinputReceiver()
{
    StopAll();
}

// ---------------------------------------------------------------------------
// Start Listening
// ---------------------------------------------------------------------------
bool XinputReceiver::StartListening(int slot, int port, XInputCallback callback)
{
    if (!IsValidSlot(slot))
        return false;

    // Stop if already running
    if (m_slots[slot].running.load())
        StopListening(slot);

    m_slots[slot].running.store(true);
    m_slots[slot].thread = std::thread(&XinputReceiver::ListenThread, this, slot, port, callback);
    return true;
}

// ---------------------------------------------------------------------------
// Stop Listening
// ---------------------------------------------------------------------------
void XinputReceiver::StopListening(int slot)
{
    if (!IsValidSlot(slot))
        return;

    m_slots[slot].running.store(false);

    // Close socket to force-release the thread
    if (m_slots[slot].sock != INVALID_SOCKET)
    {
        closesocket(m_slots[slot].sock);
        m_slots[slot].sock = INVALID_SOCKET;
    }

    if (m_slots[slot].thread.joinable())
        m_slots[slot].thread.join();
}

void XinputReceiver::StopAll()
{
    for (int i = 1; i <= 4; i++)
        StopListening(i);
}

bool XinputReceiver::IsListening(int slot) const
{
    if (!IsValidSlot(slot))
        return false;
    return m_slots[slot].running.load();
}

// ---------------------------------------------------------------------------
// Receiver Thread Body
// ---------------------------------------------------------------------------
void XinputReceiver::ListenThread(int slot, int port, XInputCallback callback)
{
    // Create UDP socket
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        m_lastError = "socket() failed";
        m_slots[slot].running.store(false);
        return;
    }

    // Prevent WSAECONNRESET UDP reset error which is Windows-specific
    DWORD dwBytesReturned = 0;
    BOOL bNewBehavior = FALSE;
    WSAIoctl(sock, SIO_UDP_CONNRESET,
             &bNewBehavior, sizeof(bNewBehavior),
             NULL, 0, &dwBytesReturned, NULL, NULL);

    m_slots[slot].sock = sock;

    // Set timeout (500ms) to check thread termination state
    DWORD timeout = 500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    // Bind
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "bind() failed port=%d err=%d", port, WSAGetLastError());
        m_lastError = buf;
        closesocket(sock);
        m_slots[slot].sock = INVALID_SOCKET;
        m_slots[slot].running.store(false);
        return;
    }

    printf("[XinputReceiver] slot%d UDP receive start  port=%d\n", slot, port);

    // Receive loop
    while (m_slots[slot].running.load())
    {
        sockaddr_in fromAddr = {};
        int fromLen = sizeof(fromAddr);

        // Secure a larger receive buffer
        char recvBuf[64] = {};
        int received = recvfrom(sock, recvBuf, sizeof(recvBuf), 0,
                                (sockaddr *)&fromAddr, &fromLen);

        if (received >= (int)sizeof(XInputPacket))
        {
            XInputPacket packet = {};
            memcpy(&packet, recvBuf, sizeof(XInputPacket));
            std::string fromIP = inet_ntoa(fromAddr.sin_addr);
            int fromPort = ntohs(fromAddr.sin_port);
            //printf("[XinputReceiver] packet received slot=%d bytes=%d from=%s:%d\n", slot, received, fromIP.c_str(), fromPort);
            if (callback)
                callback(slot, packet, fromIP, fromPort);
        }
        else if (received == SOCKET_ERROR)
        {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || err == WSAECONNRESET)
                continue; // Ignore timeout and peer disconnection (ICMP error) to continue
            if (err == WSAENOTSOCK || err == WSAEINTR)
                break; // Socket closed
        }
    }

    closesocket(sock);
    m_slots[slot].sock = INVALID_SOCKET;
    m_slots[slot].running.store(false);
    printf("[XinputReceiver] slot%d UDP receive stopped\n", slot);
}

#endif // SUPERMODEL_WIN32
