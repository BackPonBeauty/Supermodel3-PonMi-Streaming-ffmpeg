#pragma once
#ifdef SUPERMODEL_WIN32

#define MINIUPNP_STATICLIB
#include <string>

class UPnPHelper
{
public:
    static bool OpenPort(int port, const std::string& description);
    static void ClosePort(int port);
    static void OpenStreamingPorts(int linkplay);
    static void CloseStreamingPorts(int linkplay);
    // 11/23 netlink: a NETLINK slot also opens TCP base + 2 (the link data, Network/Netlink.h). Set before StartStreaming.
    static bool netlinkTcp;
};

#endif // SUPERMODEL_WIN32