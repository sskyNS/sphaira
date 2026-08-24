#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

namespace sphaira::ui::menu::cloud {

struct Provider {
    const char* name;      // 显示名
    const char* ini_path;  // mount ini 路径（小写）
    const char* section;   // 登录写入的 section 名（大写）
    const char* auth_key;  // 主鉴权键（判断是否已配置）
    const char* guide;     // 登录提示
};

struct Menu final : grid::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Cloud"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void Login();
    void StartDeviceCodeLogin();
    void PollDeviceCodeLogin();
    void RefreshStatus();
    void ClearQr();

    std::vector<Provider> m_entries{};
    std::vector<bool> m_authed{};
    s64 m_index{};
    std::unique_ptr<List> m_list{};

    // 设备码登录状态（光鸭）
    bool m_login_active{};
    u64 m_login_next_poll_ms{};
    std::string m_login_url{};
    std::string m_login_code{};

    // 二维码显示状态
    std::vector<std::uint8_t> m_qr_rgba{};
    int m_qr_size{};
    int m_qr_image{};
};

} // namespace sphaira::ui::menu::cloud
