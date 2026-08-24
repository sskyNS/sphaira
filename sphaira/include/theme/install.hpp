#pragma once

// 主题安装入口：用 nxtheme 修补基础 szs 并输出最终 szs 字节。

#include <string>
#include <vector>
#include "theme/nxtheme.hpp"

namespace sphaira::theme {

// base_szs_data：基础 .szs 的原始字节（Yaz0 压缩，通常来自系统主题 dump）。
// 成功返回 true，out_szs 为修补后 Yaz0 压缩的 .szs 字节；失败返回 false 并填充 error。
bool ApplyTheme(NxTheme& theme, const std::vector<u8>& base_szs_data, std::vector<u8>& out_szs, std::string& error);

} // namespace sphaira::theme
