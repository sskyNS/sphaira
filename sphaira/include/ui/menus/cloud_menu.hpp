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
    void OpenFileBrowser();
    void StartDeviceCodeLogin();
    void PollDeviceCodeLogin();
    void PollAliyunLogin();
    void RefreshStatus();
    void ClearQr();

    std::vector<Provider> m_entries{};
    std::vector<bool> m_authed{};
    s64 m_index{};
    std::unique_ptr<List> m_list{};

    // 扫码登录状态
    bool m_login_active{};
    u64 m_login_next_poll_ms{};
    std::string m_login_type{};     // "guangya" / "aliyun"
    std::string m_login_url{};
    std::string m_login_code{};      // device_code（轮询用）
    std::string m_login_user_code{}; // user_code（展示用，谷歌·百度）
    std::string m_qr_cookie_file{}; // 阿里云 session cookie 文件
    std::string m_qr_data_raw{};    // 阿里云 generate.do 原始响应
    std::string m_oauth_client_id{};     // 谷歌·百度 client_id
    std::string m_oauth_client_secret{}; // 谷歌·百度 client_secret

    // 二维码显示状态
    std::vector<std::uint8_t> m_qr_rgba{};
    int m_qr_size{};
    int m_qr_image{};

    // 每个网盘的 logo（nanovg 图像句柄，按 PROVIDERS 顺序）。
    std::vector<int> m_logo_images{};
};

} // namespace sphaira::ui::menu::cloud
