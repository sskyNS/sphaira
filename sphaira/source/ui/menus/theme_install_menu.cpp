#include "app.hpp"
#include "log.hpp"

#include "defines.hpp"
#include "i18n.hpp"

#include "theme/install.hpp"
#include "theme/nxtheme.hpp"
#include "theme/common.hpp"
#include "theme/dump.hpp"

#include "ui/menus/theme_install_menu.hpp"
#include "ui/nvg_util.hpp"

#include <cstring>

namespace sphaira::ui::menu::theme_install {

namespace {

constexpr const char* THEMES_DIR = "/themes/sphaira";
constexpr const char* DUMP_DIR = "/themes/sphaira/dump";

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"Theme Install"_i18n, flags} {
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){ SetPop(); }}),
        std::make_pair(Button::A, Action{"Install"_i18n, [this](){ Install(); }}),
        std::make_pair(Button::Y, Action{"Dump System"_i18n, [this](){ Dump(); }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){ m_entries.clear(); m_index = 0; Scan(); }})
    );

    Scan();
    grid::Menu::OnLayoutChange(m_list, grid::LayoutType_List);
}

Menu::~Menu() {
}

void Menu::Scan() {
    m_entries.clear();

    fs::FsNativeSd fs;

    // 递归扫描 /themes/sphaira 及其子目录，找到所有 .nxtheme 文件。
    // 这样用户把主题放在子文件夹里也能被识别。
    auto walk = [&](auto&& self, const std::string& dir_path) -> void {
        fs::Dir dir;
        if (R_FAILED(fs.OpenDirectory(dir_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) {
            return;
        }

        std::vector<FsDirectoryEntry> entries;
        if (R_FAILED(dir.ReadAll(entries))) {
            return;
        }

        for (const auto& e : entries) {
            if (e.name[0] == '.') {
                continue;
            }

            const std::string child = fs::AppendPath(dir_path, e.name);

            if (e.type == FsDirEntryType_Dir) {
                self(self, child);
            } else if (e.type == FsDirEntryType_File) {
                const char* ext = std::strrchr(e.name, '.');
                if (!ext || strncasecmp(ext, ".nxtheme", 8)) {
                    continue;
                }

                Entry entry;
                entry.path = child;

                // 显示相对于 /themes/sphaira 的路径，避免重名混淆。
                std::string rel = child;
                if (rel.starts_with(THEMES_DIR)) {
                    rel.erase(0, std::strlen(THEMES_DIR));
                    while (!rel.empty() && rel.front() == '/') {
                        rel.erase(rel.begin());
                    }
                }
                entry.name = rel.empty() ? e.name : rel;

                m_entries.emplace_back(std::move(entry));
            }
        }
    };

    walk(walk, THEMES_DIR);

    log_write("[theme_install] found %zu nxtheme files\n", m_entries.size());
}

void Menu::Install() {
    if (m_index >= m_entries.size()) {
        return;
    }

    const auto& e = m_entries[m_index];

    std::vector<u8> nxtheme_data;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(e.path, nxtheme_data))) {
        App::Notify("Failed to read the theme file");
        return;
    }

    theme::hos::InitializeVersion();

    auto theme = theme::NxTheme::TryLoad(nxtheme_data);
    if (!theme.IsValid()) {
        App::Notify(theme.error.value_or("Invalid theme"));
        return;
    }

    const auto* target = theme::ThemeTargetInfo::Find(theme.manifest->Target);
    if (!target) {
        App::Notify("Unknown theme target: " + theme.manifest->Target);
        return;
    }

    const std::string title_id = theme::ThemeTargetInfo::TitleIdToString(target->TitleId);

    // 基础 szs 优先从 NXThemes Installer 的 /themes/systemData 读取（无需 prod.keys），
    // 其次回退到本工具自己的 dump 目录 /themes/sphaira/dump。
    const char* szs_name = std::strrchr(target->SzsFile.c_str(), '/');
    szs_name = szs_name ? szs_name + 1 : target->SzsFile.c_str();

    // 优先直接读 /themes/systemData/{szs_name}（不嵌套 TitleId 子目录）。
    fs::FsPath base_path = std::string("/themes/systemData/") + szs_name;
    if (!fs::FileExists(base_path)) {
        // 回退：带 TitleId 子目录的标准写法。
        base_path = std::string("/themes/systemData/") + title_id + "/" + szs_name;
    }
    if (!fs::FileExists(base_path)) {
        // 最后回退到本工具自己的 dump 目录。
        base_path = std::string(DUMP_DIR) + "/" + title_id + target->SzsFile;
    }

    std::vector<u8> base_szs;
    if (R_FAILED(fs::FsNativeSd().read_entire_file(base_path, base_szs))) {
        App::Notify("基础主题缺失：请先用 NXThemes Installer 生成 /themes/systemData/" + title_id + "/" + szs_name + "（或按 Y dump）");
        return;
    }

    std::vector<u8> out_szs;
    std::string error;
    if (!theme::ApplyTheme(theme, base_szs, out_szs, error)) {
        App::Notify(error);
        return;
    }

    fs::FsPath out_path = std::string("/atmosphere/contents/") + title_id + "/romfs" + target->SzsFile;

    fs::FsNativeSd fs;
    fs.CreateDirectoryRecursivelyWithPath(out_path);
    if (R_FAILED(fs.write_entire_file(out_path, out_szs))) {
        App::Notify("Failed to write the patched theme");
        return;
    }

    App::Notify("Theme installed. Reboot to apply.");
}

void Menu::Dump() {
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    std::string error;
    if (theme::DumpSystemThemes(error)) {
        App::Notify("System themes dumped to /themes/sphaira/dump");
    } else {
        App::Notify("Dump failed: " + error);
    }
}

void Menu::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);

    m_list->OnUpdate(controller, touch, m_index, m_entries.size(), [this](bool touch, auto i) {
        if (!(touch && m_index == i)) {
            m_index = i;
        }
    });

    this->SetSubHeading(std::to_string(m_entries.empty() ? 0 : m_index + 1) + " / " + std::to_string(m_entries.size()));
}

void Menu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (m_entries.empty()) {
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No .nxtheme files in /themes/sphaira");
        return;
    }

    m_list->Draw(vg, theme, m_entries.size(), [this](auto* vg, auto* theme, auto v, auto pos) {
        const auto& e = m_entries[pos];
        const bool selected = pos == m_index;
        DrawEntryNoImage(vg, theme, grid::LayoutType_List, v, selected, e.name.c_str(), "", "");
    });
}

} // namespace sphaira::ui::menu::theme_install
