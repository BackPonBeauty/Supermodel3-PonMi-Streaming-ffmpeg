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
};

#endif // SUPERMODEL_WIN32