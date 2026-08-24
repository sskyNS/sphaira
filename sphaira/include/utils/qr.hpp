#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace sphaira::qr {

// 把文本编码为二维码，输出 RGBA 位图（黑模块 + 白底，含静区）。
// scale 为每个模块放大到的像素数。out_size 为位图边长（像素）。
// 返回 false 表示内容过长无法编码。
bool Generate(const std::string& text, std::vector<uint8_t>& out_rgba, int& out_size, int scale = 4);

} // namespace sphaira::qr
