#pragma once

// 移植自 SwitchThemeInjector 的 SarcLib/Sarc。
// SARC 容器解包/打包，配合 Yaz0 处理 .szs 主题文件。

#include "theme/buffer.hpp"

#include <unordered_map>
#include <vector>
#include <span>
#include <string>
#include <cstdint>
#include <switch.h>

namespace sphaira::theme {

class SARC {
public:
    struct PackedSarc {
        std::vector<u8> data;
        u32 align;
    };

    struct SarcData {
        std::unordered_map<std::string, std::vector<u8>> files;
        Endianness endianness;
        bool HashOnly;
    };

    static PackedSarc Pack(SarcData& data, s32 _align = -1);
    static SarcData Unpack(std::span<const u8> data);

private:
    static u32 NameHash(const std::string& name);
    static u32 StringHashToUint(const std::string& name);
    static std::string GuessFileExtension(const std::vector<u8>& file);
    static u32 GuessAlignment(const std::unordered_map<std::string, std::vector<u8>>& files);
    static u32 GuessFileAlignment(const std::vector<u8>& file);
};

} // namespace sphaira::theme
