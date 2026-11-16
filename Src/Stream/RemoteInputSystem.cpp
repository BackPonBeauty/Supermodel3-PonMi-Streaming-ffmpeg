/*
 * RemoteInputSystem.cpp
 *
 * Streaming (P3, 2026-11-16): input system for "InputSystem = NONE". See RemoteInputSystem.h.
 */

#include "Supermodel.h"
#include "RemoteInputSystem.h"
#include "Inputs/Input.h"   // ForceFeedbackCmd
#include <cstring>

CRemoteInputSystem *CRemoteInputSystem::s_instance = nullptr;

CRemoteInputSystem::CRemoteInputSystem(const Util::Config::Node &config)
  : CSDLInputSystem(config, false)
{
  s_instance = this;

  strcpy(m_remoteJoy.name, "Remote pad (streaming client)");
  m_remoteJoy.numAxes = 6;
  m_remoteJoy.numPOVs = 1;
  m_remoteJoy.numButtons = NUM_JOY_BUTTONS;
  m_remoteJoy.hasFFeedback = false;
  static const char *axisNames[NUM_JOY_AXES] = { "X", "Y", "Z", "RX", "RY", "RZ", "S1", "S2" };
  for (int a = 0; a < NUM_JOY_AXES; a++)
  {
    m_remoteJoy.hasAxis[a] = (a < 6);
    strcpy(m_remoteJoy.axisName[a], axisNames[a]);
    m_remoteJoy.axisHasFF[a] = false;
  }
}

CRemoteInputSystem::~CRemoteInputSystem()
{
  if (s_instance == this)
    s_instance = nullptr;
}

void CRemoteInputSystem::SetRemoteState(const RemoteState &state)
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_pending = state;
}

void CRemoteInputSystem::ClearRemoteState()
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_pending = RemoteState();
}

bool CRemoteInputSystem::InitializeSystem()
{
  // The host's pads are deliberately not opened (IN-06). Only remember the key indices we answer for.
  m_keyEscape = GetKeyIndex("ESCAPE");
  m_keyW = GetKeyIndex("W");
  m_keyA = GetKeyIndex("A");
  m_keyS = GetKeyIndex("S");
  m_keyD = GetKeyIndex("D");
  InfoLog("Input system: NONE (streaming server). Host keyboard/pads ignored except ESCAPE; input comes from the client.");
  return true;
}

bool CRemoteInputSystem::Poll()
{
  // Window events and the host keyboard state (used for ESCAPE only)
  bool ok = CSDLInputSystem::Poll();
  {
    std::lock_guard<std::mutex> lock(m_lock);
    m_current = m_pending;
  }
  return ok;
}

bool CRemoteInputSystem::IsKeyPressed(int kbdNum, int keyIndex) const
{
  if (keyIndex < 0)
    return false;
  if (keyIndex == m_keyEscape)
    return CSDLInputSystem::IsKeyPressed(kbdNum, keyIndex);   // host keyboard (IN-07)
  if (keyIndex == m_keyW) return (m_current.keys & REMOTE_KEY_W) != 0;
  if (keyIndex == m_keyA) return (m_current.keys & REMOTE_KEY_A) != 0;
  if (keyIndex == m_keyS) return (m_current.keys & REMOTE_KEY_S) != 0;
  if (keyIndex == m_keyD) return (m_current.keys & REMOTE_KEY_D) != 0;
  return false;
}

// The host mouse is not used.
int CRemoteInputSystem::GetMouseAxisValue(int, int) const { return 0; }
int CRemoteInputSystem::GetMouseWheelDir(int) const { return 0; }
bool CRemoteInputSystem::IsMouseButPressed(int, int) const { return false; }

// Always exactly one joystick: the client's first pad (reads as released when there is none).
int CRemoteInputSystem::GetNumJoysticks() const
{
  return 1;
}

const JoyDetails *CRemoteInputSystem::GetJoyDetails(int joyNum)
{
  return joyNum == 0 ? &m_remoteJoy : nullptr;
}

int CRemoteInputSystem::GetJoyAxisValue(int joyNum, int axisNum) const
{
  if (joyNum != 0 || axisNum < 0 || axisNum >= 6 || !m_current.padConnected)
    return 0;
  return m_current.axes[axisNum];
}

bool CRemoteInputSystem::IsJoyPOVInDir(int joyNum, int povNum, int povDir) const
{
  if (joyNum != 0 || povNum != 0 || !m_current.padConnected)
    return false;
  switch (povDir)
  {
    case POV_UP:    return (m_current.hat & 0x01) != 0;
    case POV_RIGHT: return (m_current.hat & 0x02) != 0;
    case POV_DOWN:  return (m_current.hat & 0x04) != 0;
    case POV_LEFT:  return (m_current.hat & 0x08) != 0;
    default:        return false;
  }
}

bool CRemoteInputSystem::IsJoyButPressed(int joyNum, int butNum) const
{
  if (joyNum != 0 || butNum < 0 || butNum >= 32 || !m_current.padConnected)
    return false;
  return (m_current.buttons >> butNum) & 1;
}

bool CRemoteInputSystem::ProcessForceFeedbackCmd(int, int, ForceFeedbackCmd)
{
  return false;   // no force feedback to the client
}
