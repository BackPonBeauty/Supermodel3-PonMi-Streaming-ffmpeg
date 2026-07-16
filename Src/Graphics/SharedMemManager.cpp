#include "SharedMemManager.h"
#define BOOST_USE_WINDOWS_H
#include <windows.h>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <iostream>
#include <cstring>

using namespace boost::interprocess;

struct ShmHeader
{
    uint32_t width;
    uint32_t height;
    uint32_t frameCount;
    char playerNick[64];
    char spectatorNick[64];
};

struct RemoteShmState
{
    std::string shmName;
    std::unique_ptr<shared_memory_object> remoteShm;
    std::unique_ptr<mapped_region> remoteRegion;
    std::unique_ptr<named_mutex> remoteMutex;
    uint32_t lastReadFrameCount = 0;
};

struct SharedMemManager::Impl
{
    std::unique_ptr<shared_memory_object> localShm;
    std::unique_ptr<mapped_region> localRegion;
    std::unique_ptr<named_mutex> localMutex;

    // Handles states for P1, P2, P3, P4
    RemoteShmState remotes[4];
};

SharedMemManager::SharedMemManager(int playerIndex, int width, int height)
    : m_playerIndex(playerIndex), m_width(width), m_height(height), m_impl(std::make_unique<Impl>())
{
    m_imageSize = m_width * m_height * 4; // RGBA
    
    // Set local shm name based on player index
    m_localShmName = "Spikofe_P" + std::to_string(m_playerIndex + 1) + "_Shm";

    // Set remote names for everyone
    for (int i = 0; i < 4; ++i)
    {
        m_impl->remotes[i].shmName = "Spikofe_P" + std::to_string(i + 1) + "_Shm";
    }
}

SharedMemManager::~SharedMemManager()
{
    Cleanup();
}

bool SharedMemManager::Init()
{
    size_t totalShmSize = sizeof(ShmHeader) + m_imageSize;

    // Setup local shared memory
    try
    {
        shared_memory_object::remove(m_localShmName.c_str());
        named_mutex::remove((m_localShmName + "_mutex").c_str());

        m_impl->localShm = std::make_unique<shared_memory_object>(create_only, m_localShmName.c_str(), read_write);
        m_impl->localShm->truncate(totalShmSize);
        m_impl->localRegion = std::make_unique<mapped_region>(*m_impl->localShm, read_write);
        m_impl->localMutex = std::make_unique<named_mutex>(create_only, (m_localShmName + "_mutex").c_str());

        ShmHeader* header = static_cast<ShmHeader*>(m_impl->localRegion->get_address());
        header->width = m_width;
        header->height = m_height;
        header->frameCount = 0;
        
        std::memset(static_cast<char*>(m_impl->localRegion->get_address()) + sizeof(ShmHeader), 0, m_imageSize);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[ShmManager] Local initialization failed for " << m_localShmName << ": " << ex.what() << std::endl;
        return false;
    }

    std::cout << "[ShmManager] Initialized local shared memory: " << m_localShmName << std::endl;
    return true;
}

void SharedMemManager::Cleanup()
{
    m_impl->localRegion.reset();
    m_impl->localShm.reset();
    m_impl->localMutex.reset();

    for (int i = 0; i < 4; ++i)
    {
        m_impl->remotes[i].remoteRegion.reset();
        m_impl->remotes[i].remoteShm.reset();
        m_impl->remotes[i].remoteMutex.reset();
    }

    shared_memory_object::remove(m_localShmName.c_str());
    named_mutex::remove((m_localShmName + "_mutex").c_str());
}

void SharedMemManager::ResetConnections()
{
    for (int i = 0; i < 4; ++i)
    {
        m_impl->remotes[i].remoteRegion.reset();
        m_impl->remotes[i].remoteShm.reset();
        m_impl->remotes[i].remoteMutex.reset();
        m_impl->remotes[i].lastReadFrameCount = 0;
    }
    std::cout << "[ShmManager] Reset all remote shared memory connections" << std::endl;
}

void SharedMemManager::WriteLocalFrame(const unsigned char* pixelData)
{
    if (!m_impl->localRegion || !m_impl->localMutex) return;

    try
    {
        scoped_lock<named_mutex> lock(*m_impl->localMutex);
        ShmHeader* header = static_cast<ShmHeader*>(m_impl->localRegion->get_address());
        char* pixelDest = static_cast<char*>(m_impl->localRegion->get_address()) + sizeof(ShmHeader);
        
        std::memcpy(pixelDest, pixelData, m_imageSize);
        header->frameCount++;
    }
    catch (...)
    {
    }
}

bool SharedMemManager::ReadRemoteFrame(int playerIdx, unsigned char* outPixelData)
{
    if (playerIdx < 0 || playerIdx >= 4 || playerIdx == m_playerIndex) return false;

    RemoteShmState& remote = m_impl->remotes[playerIdx];

    // Try to open remote memory if not already connected
    if (!remote.remoteShm)
    {
        try
        {
            remote.remoteShm = std::make_unique<shared_memory_object>(open_only, remote.shmName.c_str(), read_write);
            remote.remoteRegion = std::make_unique<mapped_region>(*remote.remoteShm, read_write);
            remote.remoteMutex = std::make_unique<named_mutex>(open_only, (remote.shmName + "_mutex").c_str());
            std::cout << "[ShmManager] Connected to remote shared memory: " << remote.shmName << std::endl;
        }
        catch (...)
        {
            return false;
        }
    }

    if (!remote.remoteRegion || !remote.remoteMutex) return false;

    try
    {
        scoped_lock<named_mutex> lock(*remote.remoteMutex);
        ShmHeader* header = static_cast<ShmHeader*>(remote.remoteRegion->get_address());
        
        if (header->frameCount == remote.lastReadFrameCount)
        {
            return false;
        }

        const char* pixelSrc = static_cast<const char*>(remote.remoteRegion->get_address()) + sizeof(ShmHeader);
        std::memcpy(outPixelData, pixelSrc, m_imageSize);
        remote.lastReadFrameCount = header->frameCount;
        return true;
    }
    catch (...)
    {
        // Ignore error, do not reset handles, so it keeps attempting to read next time
        return false;
    }
}

void SharedMemManager::WriteNicknames(const std::string& player, const std::string& spectator)
{
    if (!m_impl->localRegion || !m_impl->localMutex) return;
    try
    {
        scoped_lock<named_mutex> lock(*m_impl->localMutex);
        ShmHeader* header = static_cast<ShmHeader*>(m_impl->localRegion->get_address());
        strncpy(header->playerNick,    player.c_str(),    sizeof(header->playerNick) - 1);
        strncpy(header->spectatorNick, spectator.c_str(), sizeof(header->spectatorNick) - 1);
        header->playerNick[sizeof(header->playerNick) - 1]       = '\0';
        header->spectatorNick[sizeof(header->spectatorNick) - 1] = '\0';
    }
    catch (...) {}
}

bool SharedMemManager::ReadNicknames(int playerIdx, std::string& outPlayer, std::string& outSpectator) const
{
    if (playerIdx < 0 || playerIdx >= 4) return false;
    if (playerIdx == m_playerIndex)
    {
        // own instance — read from local region
        if (!m_impl->localRegion || !m_impl->localMutex) return false;
        try
        {
            scoped_lock<named_mutex> lock(*m_impl->localMutex);
            ShmHeader* h = static_cast<ShmHeader*>(m_impl->localRegion->get_address());
            outPlayer    = h->playerNick;
            outSpectator = h->spectatorNick;
            return true;
        }
        catch (...) { return false; }
    }

    // remote instance — use already-mapped region or open temporarily
    RemoteShmState& remote = m_impl->remotes[playerIdx];
    if (remote.remoteRegion)
    {
        try
        {
            scoped_lock<named_mutex> lock(*remote.remoteMutex);
            ShmHeader* h = static_cast<ShmHeader*>(remote.remoteRegion->get_address());
            outPlayer    = h->playerNick;
            outSpectator = h->spectatorNick;
            return true;
        }
        catch (...) { return false; }
    }

    // not yet mapped — open read-only temporarily
    try
    {
        shared_memory_object shm(open_only, remote.shmName.c_str(), read_only);
        mapped_region         region(shm, read_only);
        std::string mutexName = remote.shmName + "_mutex";
        named_mutex           mutex(open_only, mutexName.c_str());
        scoped_lock<named_mutex> lock(mutex);
        const ShmHeader* h = static_cast<const ShmHeader*>(region.get_address());
        outPlayer    = h->playerNick;
        outSpectator = h->spectatorNick;
        return true;
    }
    catch (...) { return false; }
}

bool SharedMemManager::HasAnyRemotePlayer() const
{
    for (int i = 0; i < 4; ++i)
    {
        if (i == m_playerIndex) continue;
        // Try to open the remote shared memory by name — existence means the player is running
        std::string shmName = "Spikofe_P" + std::to_string(i + 1) + "_Shm";
        try
        {
            shared_memory_object shm(open_only, shmName.c_str(), read_only);
            return true;
        }
        catch (...) {}
    }
    return false;
}
