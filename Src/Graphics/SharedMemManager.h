#pragma once
#include <string>
#include <vector>
#include <memory>

class SharedMemManager
{
public:
    SharedMemManager(int playerIndex, int width, int height);
    ~SharedMemManager();

    bool Init();
    void Cleanup();

    // Writes the locally rendered frame buffer (RGBA pixels) into local shared memory
    void WriteLocalFrame(const unsigned char* pixelData);

    // Reads the remote player's frame buffer (RGBA pixels) into outPixelData.
    // playerIdx should be 0, 1, 2, or 3 corresponding to P1, P2, P3, P4.
    // Returns true if new/valid frame was retrieved.
    bool ReadRemoteFrame(int playerIdx, unsigned char* outPixelData);

    // Write/read player+spectator nicknames for the debug panel
    void WriteNicknames(const std::string& player, const std::string& spectator);
    bool ReadNicknames(int playerIdx, std::string& outPlayer, std::string& outSpectator) const;

    // Returns true if at least one remote player's shared memory exists.
    bool HasAnyRemotePlayer() const;
    void ResetConnections();

    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    int m_playerIndex; // 0=P1, 1=P2, 2=P3, 3=P4
    int m_width;
    int m_height;
    size_t m_imageSize;
    std::string m_localShmName;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
