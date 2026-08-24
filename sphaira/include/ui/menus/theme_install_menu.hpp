#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "fs.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::theme_install {

struct Entry {
    fs::FsPath path{};
    std::string name{};
};

struct Menu final : grid::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Themes"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void Scan();
    void Install();
    void Dump();

    std::vector<Entry> m_entries{};
    s64 m_index{};
    std::unique_ptr<List> m_list{};
};

} // namespace sphaira::ui::menu::theme_install
