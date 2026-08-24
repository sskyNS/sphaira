#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "fs.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui { struct ProgressBox; }

namespace sphaira::ui::menu::film_warehouse {

// 单条入库的媒体（电影 / 剧集 / 未识别视频）。
struct MediaEntry {
    std::string title{};    // 识别出的标题（失败时回退为文件名）
    std::string year{};     // 年份（TMDB 可覆盖）
    std::string kind{};     // "movie" | "tv" | "unknown"
    std::string season{};   // 剧集季号（可为空）
    std::string episode{};  // 剧集集号（可为空）
    std::string quality{};  // 画质标签（1080p 等）
    std::string source{};   // 来源：网盘名 或 "SD 卡"
    std::string mount{};    // 挂载点（stdio 为 "[SECTION] name:/"，SD 为空）
    std::string path{};     // 挂载点内完整路径
    std::string filename{}; // 原始文件名
    u64 size{};
    std::string poster{};   // 本地海报缓存路径（可为空）
};

struct Menu final : grid::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "影视仓"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void LoadLibrary();
    void SaveLibrary() const;
    void StartScan();
    void ClearLibrary();
    void OpenEntry();
    void Play();
    void Search();
    auto DoScan(sphaira::ui::ProgressBox* pbox) -> std::vector<MediaEntry>;

    std::vector<MediaEntry> m_entries{};
    std::vector<int> m_posters{}; // nanovg 海报句柄（与 m_entries 对齐）
    s64 m_index{};
    std::unique_ptr<List> m_list{};
};

} // namespace sphaira::ui::menu::film_warehouse
