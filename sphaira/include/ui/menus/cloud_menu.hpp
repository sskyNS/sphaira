#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"

#include <memory>
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
    void RefreshStatus();

    std::vector<Provider> m_entries{};
    std::vector<bool> m_authed{};
    s64 m_index{};
    std::unique_ptr<List> m_list{};
};

} // namespace sphaira::ui::menu::cloud
