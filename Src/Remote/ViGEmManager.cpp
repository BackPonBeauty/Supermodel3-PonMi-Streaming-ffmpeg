/**
 * ViGEmManager.cpp
 *
 * 仮想 Xbox 360 コントローラー管理の実装
 */

#ifdef SUPERMODEL_WIN32

#include "ViGEmManager.h"
#include <cstdio>

ViGEmManager::ViGEmManager()
{
    m_targets.fill(nullptr);
}

ViGEmManager::~ViGEmManager()
{
    Shutdown();
}

bool ViGEmManager::Initialize()
{
    if (m_client != nullptr)
        return true; // すでに初期化済み

    m_client = vigem_alloc();
    if (m_client == nullptr)
    {
        m_statusMessage = "ViGEm: memory allocation failed";
        return false;
    }

    VIGEM_ERROR err = vigem_connect(m_client);
    if (!VIGEM_SUCCESS(err))
    {
        vigem_free(m_client);
        m_client = nullptr;
        char buf[128];
        snprintf(buf, sizeof(buf), "ViGEm: connection failed (0x%08X) - Please check if ViGEmBus driver is installed", (unsigned)err);
        m_statusMessage = buf;
        return false;
    }

    m_statusMessage = "ViGEm: ready";
    return true;
}

void ViGEmManager::Shutdown()
{
    for (int slot = 1; slot <= 4; slot++)
        RemoveController(slot);

    if (m_client != nullptr)
    {
        vigem_disconnect(m_client);
        vigem_free(m_client);
        m_client = nullptr;
    }
}

bool ViGEmManager::AddController(int slot)
{
    if (!IsValidSlot(slot) || m_client == nullptr)
        return false;

    // すでに存在する場合は先に削除
    if (m_targets[slot] != nullptr)
        RemoveController(slot);

    PVIGEM_TARGET target = vigem_target_x360_alloc();
    if (target == nullptr)
    {
        m_statusMessage = "ViGEm: target allocation failed";
        return false;
    }

    VIGEM_ERROR err = vigem_target_add(m_client, target);
    if (!VIGEM_SUCCESS(err))
    {
        vigem_target_free(target);
        char buf[128];
        snprintf(buf, sizeof(buf), "ViGEm: slot %d addition failed (0x%08X)", slot, (unsigned)err);
        m_statusMessage = buf;
        return false;
    }

    m_targets[slot] = target;
    char buf[64];
    snprintf(buf, sizeof(buf), "ViGEm: slot %d virtual controller added", slot);
    m_statusMessage = buf;
    return true;
}

void ViGEmManager::RemoveController(int slot)
{
    if (!IsValidSlot(slot) || m_targets[slot] == nullptr)
        return;

    if (m_client != nullptr)
        vigem_target_remove(m_client, m_targets[slot]);

    vigem_target_free(m_targets[slot]);
    m_targets[slot] = nullptr;
}

bool ViGEmManager::UpdateController(int slot, const XUSB_REPORT& report)
{
    if (!IsValidSlot(slot) || m_client == nullptr || m_targets[slot] == nullptr)
        return false;

    VIGEM_ERROR err = vigem_target_x360_update(m_client, m_targets[slot], report);
    return VIGEM_SUCCESS(err);
}

bool ViGEmManager::IsSlotActive(int slot) const
{
    if (!IsValidSlot(slot))
        return false;
    return m_targets[slot] != nullptr;
}

#endif // SUPERMODEL_WIN32
