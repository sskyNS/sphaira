#include "app.hpp"
#include "log.hpp"

#include "defines.hpp"
#include "i18n.hpp"
#include "image.hpp"
#include "title_info.hpp"

#include "ui/menus/game_version_menu.hpp"
#include "ui/nvg_util.hpp"

#include "yati/nx/ns.hpp"

#include <algorithm>
#include <utility>

namespace sphaira::ui::menu::game_version {
namespace {

// 将 NCM 标题版本号 (u32) 格式化为 "major.minor.micro"。
auto FormatVersion(u32 v) -> std::string {
    const u32 major = (v >> 26) & 0x3F;
    const u32 minor = (v >> 20) & 0x3F;
    const u32 micro = (v >> 16) & 0xF;
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(micro);
}

} // namespace

Menu::Menu(u32 flags) : grid::Menu{"Versions"_i18n, flags} {
    this->SetActions(
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Refresh"_i18n, [this](){
            m_entries.clear();
            m_index = 0;
            Scan();
        }})
    );

    ns::Initialize();
    title::Init();
    Scan();

    grid::Menu::OnLayoutChange(m_list, grid::LayoutType_GridDetail);
}

Menu::~Menu() {
    for (auto& e : m_entries) {
        if (e.image) {
            nvgDeleteImage(App::GetVg(), e.image);
        }
    }

    title::Exit();
    ns::Exit();
}

void Menu::Scan() {
    App::SetBoostMode(true);
    ON_SCOPE_EXIT(App::SetBoostMode(false));

    m_entries.clear();

    constexpr auto CHUNK = 1000;
    std::vector<NsApplicationRecord> records(CHUNK);
    s32 offset{};
    while (true) {
        s32 count{};
        if (R_FAILED(nsListApplicationRecord(records.data(), records.size(), offset, &count))) {
            log_write_feature("[game_version] failed to list application records at offset %d\n", offset);
        }

        if (!count) {
            break;
        }

        for (s32 i = 0; i < count; i++) {
            const auto& r = records[i];

            // 跳过 forwarder。
            if ((r.application_id & 0x0500000000000000) == 0x0500000000000000) {
                continue;
            }

            auto& e = m_entries.emplace_back();
            e.app_id = r.application_id;
        }

        offset += count;
    }

    log_write_feature("[game_version] found %zu titles\n", m_entries.size());
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
        gfx::drawTextArgs(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO), "Empty..."_i18n.c_str());
        return;
    }

    int image_load_count = 0;
    int version_load_count = 0;

    m_list->Draw(vg, theme, m_entries.size(), [this, &image_load_count, &version_load_count](auto* vg, auto* theme, auto v, auto pos) {
        auto& e = m_entries[pos];

        if (e.status == title::NacpLoadStatus::None) {
            title::PushAsync(e.app_id);
            e.status = title::NacpLoadStatus::Progress;
        } else if (e.status == title::NacpLoadStatus::Progress) {
            if (auto* result = title::GetAsync(e.app_id)) {
                e.status = result->status;
                if (e.status == title::NacpLoadStatus::Loaded) {
                    e.lang = result->lang;

                    // 懒加载图标，每帧最多 2 个，避免 IO/GPU 卡顿。
                    if (image_load_count < 2 && !e.image && !result->icon.empty()) {
                        const auto image = ImageLoadFromMemory(result->icon, ImageFlag_JPEG);
                        if (!image.data.empty()) {
                            e.image = nvgCreateImageRGBA(vg, image.w, image.h, 0, image.data.data());
                            image_load_count++;
                        }
                    }
                }
            }
        }

        // 每帧最多读取 8 个标题版本，避免首次打开时卡顿。
        if (!e.version_loaded && version_load_count < 8) {
            version_load_count++;

            title::MetaEntries meta;
            if (R_SUCCEEDED(title::GetMetaEntries(e.app_id, meta, title::ContentFlag_Nacp))) {
                for (auto& m : meta) {
                    e.version = std::max(e.version, m.version);
                }
            }
            e.version_loaded = true;
        }

        const std::string version_str = e.version_loaded ? FormatVersion(e.version) : "...";
        const auto selected = pos == m_index;

        DrawEntry(vg, theme, grid::LayoutType_GridDetail, v, selected, e.image, e.GetName(), e.GetAuthor(), version_str.c_str());
    });
}

} // namespace sphaira::ui::menu::game_version
