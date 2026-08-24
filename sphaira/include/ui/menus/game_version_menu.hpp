#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"

#include "title_info.hpp"

#include <memory>
#include <vector>

namespace sphaira::ui::menu::game_version {

struct Entry {
    u64 app_id{};
    NacpLanguageEntry lang{};
    u32 version{};
    bool version_loaded{};
    title::NacpLoadStatus status{title::NacpLoadStatus::None};

    auto GetName() const -> const char* {
        return lang.name;
    }
};

struct Menu final : grid::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Versions"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void Scan();

    std::vector<Entry> m_entries{};
    s64 m_index{};
    std::unique_ptr<List> m_list{};
};

} // namespace sphaira::ui::menu::game_version
