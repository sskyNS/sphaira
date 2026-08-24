#include "theme/install.hpp"
#include "theme/patcher.hpp"
#include "theme/sarc.hpp"
#include "theme/yaz0.hpp"

#include <exception>

namespace sphaira::theme {

bool ApplyTheme(NxTheme& theme, const std::vector<u8>& base_szs_data, std::vector<u8>& out_szs, std::string& error) {
    try {
        // 1. 解压并解包基础 szs。
        auto extracted = szs::Extract(base_szs_data);
        if (std::holds_alternative<std::string>(extracted)) {
            error = std::get<std::string>(extracted);
            return false;
        }

        auto sarc = std::get<FileContainer>(extracted);
        SARC::SarcData sarc_data;
        sarc_data.endianness = Endianness::LittleEndian;
        sarc_data.HashOnly = false;
        sarc_data.files = std::move(sarc);

        SzsPatcher patcher(sarc_data);

        // 2. 主背景图。
        if (theme.HasMainImage()) {
            auto img = theme.GetMainImage();
            if (std::holds_alternative<std::string>(img)) {
                error = std::get<std::string>(img);
                return false;
            }
            if (!patcher.PatchMainBG(std::get<FileData>(img))) {
                error = "Failed to patch the main background";
                return false;
            }
        }

        // 3. 布局补丁。
        if (theme.HasMainLayout()) {
            auto patch = Patches::LoadLayout(theme.GetMainLayout());
            if (!patcher.PatchLayouts(patch)) {
                error = "Failed to patch the layout";
                return false;
            }
        }

        if (theme.HasCommonLayout()) {
            auto patch = Patches::LoadLayout(theme.GetCommonLayout());
            if (!patcher.PatchLayouts(patch)) {
                error = "Failed to patch the common layout";
                return false;
            }
        }

        // 4. 应用图标替换（home/lock 等）。
        const std::string partName = theme.manifest.has_value() ? theme.manifest->Target : "";
        if (Patches::textureReplacement::NxNameToList.count(partName)) {
            for (const auto& replacement : Patches::textureReplacement::NxNameToList.at(partName)) {
                if (!theme.HasImagePart(replacement.NxThemeName))
                    continue;

                auto img = theme.GetImagePart(replacement.NxThemeName, replacement.W, replacement.H);
                if (std::holds_alternative<std::string>(img))
                    continue;

                patcher.PatchAppletIcon(std::get<FileData>(img), replacement.NxThemeName);
            }
        }

        // 5. 重新打包 + 压缩。
        auto& final_sarc = patcher.GetFinalSarc();
        auto packed = SARC::Pack(final_sarc);
        out_szs = yaz0::Compress(packed.data);

        return true;
    } catch (const std::exception& e) {
        error = std::string("Error while installing theme: ") + e.what();
        return false;
    }
}

} // namespace sphaira::theme
