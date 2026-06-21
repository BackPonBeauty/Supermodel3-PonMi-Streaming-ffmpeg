#ifdef SUPERMODEL_WIN32
#define MINIUPNP_STATICLIB
#include "UPnPHelper.h"
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <cstdio>
#include <string>

bool UPnPHelper::OpenPort(int port, const std::string& description)
{
    int error = 0;
    UPNPDev* devList = upnpDiscover(1000, nullptr, nullptr, 0, 0, 2, &error);
    if (!devList)
    {
        printf("[UPnP] No devices found (error=%d)\n", error);
        return false;
    }

    UPNPUrls urls = {};
    IGDdatas data = {};
    char localIP[64] = {};
    char wanIP[64] = {};

    int ret = UPNP_GetValidIGD(devList, &urls, &data, localIP, sizeof(localIP), wanIP, sizeof(wanIP));
    freeUPNPDevlist(devList);

    if (ret <= 0)
    {
        printf("[UPnP] No valid IGD found\n");
        return false;
    }

    std::string portStr = std::to_string(port);
    int r = UPNP_AddPortMapping(
        urls.controlURL,
        data.first.servicetype,
        portStr.c_str(),
        portStr.c_str(),
        localIP,
        description.c_str(),
        "UDP",
        nullptr,
        "0");

    FreeUPNPUrls(&urls);

    if (r == UPNPCOMMAND_SUCCESS)
    {
        printf("[UPnP] Port %d opened (%s)\n", port, description.c_str());
        return true;
    }
    else
    {
        printf("[UPnP] Port %d failed (err=%d)\n", port, r);
        return false;
    }
}

void UPnPHelper::ClosePort(int port)
{
    int error = 0;
    UPNPDev* devList = upnpDiscover(1000, nullptr, nullptr, 0, 0, 2, &error);
    if (!devList) return;

    UPNPUrls urls = {};
    IGDdatas data = {};
    char localIP[64] = {};
    char wanIP[64] = {};

    int ret = UPNP_GetValidIGD(devList, &urls, &data, localIP, sizeof(localIP), wanIP, sizeof(wanIP));
    freeUPNPDevlist(devList);
    if (ret <= 0) return;

    std::string portStr = std::to_string(port);
    UPNP_DeletePortMapping(
        urls.controlURL,
        data.first.servicetype,
        portStr.c_str(),
        "UDP",
        nullptr);

    FreeUPNPUrls(&urls);
    printf("[UPnP] Port %d closed\n", port);
}

void UPnPHelper::OpenStreamingPorts(int linkplay)
{
    int error = 0;
    printf("[UPnP] Discovering UPnP devices...\n");
    UPNPDev* devList = upnpDiscover(1000, nullptr, nullptr, 0, 0, 2, &error);
    if (!devList)
    {
        printf("[UPnP] No devices found during OpenStreamingPorts (error=%d)\n", error);
        return;
    }

    UPNPUrls urls = {};
    IGDdatas data = {};
    char localIP[64] = {};
    char wanIP[64] = {};

    int ret = UPNP_GetValidIGD(devList, &urls, &data, localIP, sizeof(localIP), wanIP, sizeof(wanIP));
    freeUPNPDevlist(devList);

    if (ret <= 0)
    {
        printf("[UPnP] No valid IGD found during OpenStreamingPorts\n");
        return;
    }

    int base = 55000 + (linkplay - 1) * 4;
    const char* descriptions[4] = {
        "Supermodel XInput",
        "Supermodel Handshake",
        "Supermodel Video",
        "Supermodel Audio"
    };

    for (int i = 0; i < 4; i++)
    {
        int port = base + i;
        std::string portStr = std::to_string(port);
        int r = UPNP_AddPortMapping(
            urls.controlURL,
            data.first.servicetype,
            portStr.c_str(),
            portStr.c_str(),
            localIP,
            descriptions[i],
            "UDP",
            nullptr,
            "0");

        if (r == UPNPCOMMAND_SUCCESS)
        {
            printf("[UPnP] Port %d opened (%s)\n", port, descriptions[i]);
        }
        else
        {
            printf("[UPnP] Port %d failed (err=%d)\n", port, r);
        }
    }

    FreeUPNPUrls(&urls);
}

void UPnPHelper::CloseStreamingPorts(int linkplay)
{
    int error = 0;
    printf("[UPnP] Discovering UPnP devices for close...\n");
    UPNPDev* devList = upnpDiscover(1000, nullptr, nullptr, 0, 0, 2, &error);
    if (!devList) return;

    UPNPUrls urls = {};
    IGDdatas data = {};
    char localIP[64] = {};
    char wanIP[64] = {};

    int ret = UPNP_GetValidIGD(devList, &urls, &data, localIP, sizeof(localIP), wanIP, sizeof(wanIP));
    freeUPNPDevlist(devList);
    if (ret <= 0) return;

    int base = 55000 + (linkplay - 1) * 4;
    for (int i = 0; i < 4; i++)
    {
        int port = base + i;
        std::string portStr = std::to_string(port);
        UPNP_DeletePortMapping(
            urls.controlURL,
            data.first.servicetype,
            portStr.c_str(),
            "UDP",
            nullptr);
        printf("[UPnP] Port %d closed\n", port);
    }

    FreeUPNPUrls(&urls);
}

#endif // SUPERMODEL_WIN32