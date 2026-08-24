#pragma once

// 移植自 SwitchThemeInjector 的 SarcLib/Yaz0。
// Yaz0 压缩/解压，用于 .szs 主题文件。

#include <vector>
#include <span>
#include <cstdint>
#include <switch.h>

namespace sphaira::theme::yaz0 {

bool IsYaz0(std::span<const u8> Data);
std::vector<u8> Decompress(const std::vector<u8>& Data);
std::vector<u8> Compress(const std::vector<u8>& Data, int level = 3, int reserved1 = 0, int reserved2 = 0);

} // namespace sphaira::theme::yaz0
