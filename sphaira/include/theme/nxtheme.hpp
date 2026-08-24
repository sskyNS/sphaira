#pragma once

// 移植自 SwitchThemeInjector 的 NXTheme.hpp。
// nxtheme(zip/szs) 解析 + info.json 清单 + 图片/布局访问。

#include <vector>
#include <string>
#include <optional>
#include <variant>
#include <span>
#include <string_view>
#include "theme/common.hpp"

namespace sphaira::theme {

using FileResult = std::variant<FileData, std::string>;
using ContainerResult = std::variant<FileContainer, std::string>;

namespace zip {
    bool IsZip(std::span<const u8> data);
    ContainerResult Extract(std::span<const u8> data);
}

namespace szs {
    ContainerResult Extract(const std::vector<u8>& data);
}

struct ThemeFileManifest {
    int Version;
    std::string Author;
    std::string ThemeName;
    std::string LayoutInfo;
    std::string Target;

    static ThemeFileManifest FromJson(std::string_view json);
};

class NxTheme {
private:
    void initialize();

public:
    FileContainer files;
    std::optional<std::string> error;
    std::optional<ThemeFileManifest> manifest;

    bool IsValid() const { return !error.has_value() && manifest.has_value(); }

    NxTheme(FileContainer&& f) : files(std::move(f)) { initialize(); }
    NxTheme(const FileContainer& f) : files(f) { initialize(); }

    // 所有图片始终以 DDS 格式返回。
    static FileResult ConvertToDDS(const FileData& image, bool transparent, int width, int height);

    bool HasMainImage() const { return files.count("image.dds") || files.count("image.jpg"); }
    FileResult GetMainImage() const;

    bool HasMainLayout() const { return files.count("layout.json"); }
    std::string_view GetMainLayout() const;

    bool HasCommonLayout() const { return files.count("common.json"); }
    std::string_view GetCommonLayout() const;

    bool HasImagePart(std::string_view partName) const;
    FileResult GetImagePart(std::string_view partName, int width, int height) const;

    static NxTheme FromError(std::string message);
    static NxTheme TryLoad(const std::vector<u8>& data);
};

} // namespace sphaira::theme
