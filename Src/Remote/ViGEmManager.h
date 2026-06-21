/**
 * ViGEmManager.h
 *
 * 仮想 Xbox 360 コントローラー管理（ViGEmClient C SDK 使用）
 *
 * 必要なもの:
 *   ViGEmClient SDK: https://github.com/nefarius/ViGEmClient
 *   NuGet: Install-Package Nefarius.ViGEm.Client
 *   または VS2008/ViGEmClient/ フォルダに手動配置
 */
#pragma once

#ifdef SUPERMODEL_WIN32

#include <windows.h>
#include <array>
#include <string>

// ViGEmClient SDK ヘッダー
// NuGet でインストールした場合は自動的にインクルードパスが設定される
// 手動配置の場合: VS プロジェクトのインクルードパスに追加してください
#include <ViGEm/Client.h>

class ViGEmManager
{
public:
    ViGEmManager();
    ~ViGEmManager();

    // ViGEmドライバーに接続して初期化
    bool Initialize();
    // 全コントローラーを解放してシャットダウン
    void Shutdown();

    // 指定スロット（1〜4）に仮想 Xbox360 コントローラーを追加
    bool AddController(int slot);
    // 指定スロットのコントローラーを削除
    void RemoveController(int slot);

    // 指定スロットのコントローラー状態を更新
    bool UpdateController(int slot, const XUSB_REPORT& report);

    bool IsInitialized() const { return m_client != nullptr; }
    bool IsSlotActive(int slot) const;

    std::string GetStatusMessage() const { return m_statusMessage; }

private:
    PVIGEM_CLIENT            m_client = nullptr;
    std::array<PVIGEM_TARGET, 5> m_targets = {};   // インデックス 1〜4 を使用
    std::string              m_statusMessage;

    bool IsValidSlot(int slot) const { return slot >= 1 && slot <= 4; }
};

#endif // SUPERMODEL_WIN32
