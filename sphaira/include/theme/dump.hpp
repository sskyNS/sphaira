#pragma once

// 系统主题 dump：从固件内置 title（qlaunch/user page/psl）的 NCA romfs 中
// 提取基础 .szs 文件，写到 /themes/sphaira/dump/{TitleId}/lyt/*.szs。

#include <string>

namespace sphaira::theme {

// 成功返回 true；失败返回 false 并填充 error。
bool DumpSystemThemes(std::string& error);

} // namespace sphaira::theme
