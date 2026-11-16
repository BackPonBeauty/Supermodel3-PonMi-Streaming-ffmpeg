/*
 * RemoteInputSystem.h
 *
 * Streaming (P3, 2026-11-16): the input system used with "InputSystem = NONE" on a streaming server
 * (requirements IN-03/IN-06/IN-07, design 4.3).
 *
 * - The host PC's own keyboard and pads are NOT read, except ESCAPE (so Supermodel can still be closed
 *   on the host; only while the window has the focus, as SDL reports it).
 * - The client's raw input (keyboard W/A/S/D and pad 1: buttons, axes, hat) is handed in with
 *   SetRemoteState() from the network thread. It shows up as KEY_W/KEY_A/KEY_S/KEY_D and as one
 *   joystick (JOY1_...), so the ini mappings (InputStart1 = JOY1_BUTTON8 ...) work unchanged.
 * - With no client (ClearRemoteState) everything reads as released: the character just stands still.
 *
 * It derives from CSDLInputSystem to reuse the SDL window event pump (Poll) and the key name table.
 */

#ifndef INCLUDED_REMOTEINPUTSYSTEM_H
#define INCLUDED_REMOTEINPUTSYSTEM_H

#include "OSD/SDL/SDLInputSystem.h"
#include <cstdint>
#include <mutex>

class CRemoteInputSystem : public CSDLInputSystem
{
public:
  // Raw state of the client's keyboard keys and first pad (same meaning as the INPUT packet, design 3.4).
  struct RemoteState
  {
    uint16_t keys = 0;          // bit0 W, bit1 A, bit2 S, bit3 D (more keys can be added later)
    bool     padConnected = false;
    uint32_t buttons = 0;       // bit n = SDL joystick button n (JOY1_BUTTON(n+1))
    int16_t  axes[6] = {};      // SDL joystick axes 0..5 (X, Y, Z, RX, RY, RZ), -32768..32767
    uint8_t  hat = 0;           // SDL hat bits: 1 up, 2 right, 4 down, 8 left
  };

  enum RemoteKeyBit : uint16_t
  {
    REMOTE_KEY_W = 1 << 0,
    REMOTE_KEY_A = 1 << 1,
    REMOTE_KEY_S = 1 << 2,
    REMOTE_KEY_D = 1 << 3,
  };

  explicit CRemoteInputSystem(const Util::Config::Node &config);
  ~CRemoteInputSystem();

  // Called from the network thread (SessionServer, P4). Takes effect on the next Poll().
  void SetRemoteState(const RemoteState &state);
  // No client / client dropped: everything released.
  void ClearRemoteState();

  // The one instance in this process (nullptr unless InputSystem = NONE).
  static CRemoteInputSystem *GetInstance() { return s_instance; }

  // CInputSystem
  int GetNumJoysticks() const override;
  const JoyDetails *GetJoyDetails(int joyNum) override;
  bool Poll() override;

protected:
  bool InitializeSystem() override;
  bool IsKeyPressed(int kbdNum, int keyIndex) const override;
  int GetMouseAxisValue(int mseNum, int axisNum) const override;
  int GetMouseWheelDir(int mseNum) const override;
  bool IsMouseButPressed(int mseNum, int butNum) const override;
  int GetJoyAxisValue(int joyNum, int axisNum) const override;
  bool IsJoyPOVInDir(int joyNum, int povNum, int povDir) const override;
  bool IsJoyButPressed(int joyNum, int butNum) const override;
  bool ProcessForceFeedbackCmd(int joyNum, int axisNum, ForceFeedbackCmd ffCmd) override;

private:
  static CRemoteInputSystem *s_instance;

  std::mutex m_lock;
  RemoteState m_pending;      // written by the network thread
  RemoteState m_current;      // read by the emulator thread (copied from m_pending in Poll)

  JoyDetails m_remoteJoy = {};

  // Key indices (in the SDL key table) of the keys this system answers for
  int m_keyEscape = -1;
  int m_keyW = -1, m_keyA = -1, m_keyS = -1, m_keyD = -1;
};

#endif // INCLUDED_REMOTEINPUTSYSTEM_H
