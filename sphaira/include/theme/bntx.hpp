#pragma once

// 移植自 SwitchThemeInjector 的 Bntx/（BRTI/DDS/DDS_conversion/QuickBntx）。
// BNTX 纹理容器解析与替换 + 图片→DDS 转换。

#include <vector>
#include <string>
#include <unordered_map>
#include <switch.h>
#include "theme/buffer.hpp"

namespace sphaira::theme {

namespace Bntxx {

    enum class TextureFormatType : u32 {
        R5G6B5 = 0x07, R8G8 = 0x09, R16 = 0x0a, R8G8B8A8 = 0x0b,
        R11G11B10 = 0x0f, R32 = 0x14, BC1 = 0x1a, BC2 = 0x1b, BC3 = 0x1c,
        BC4 = 0x1d, BC5 = 0x1e, ASTC4x4 = 0x2d, ASTC5x4 = 0x2e, ASTC5x5 = 0x2f,
        ASTC6x5 = 0x30, ASTC6x6 = 0x31, ASTC8x5 = 0x32, ASTC8x6 = 0x33, ASTC8x8 = 0x34,
        ASTC10x5 = 0x35, ASTC10x6 = 0x36, ASTC10x8 = 0x37, ASTC10x10 = 0x38,
        ASTC12x10 = 0x39, ASTC12x12 = 0x3a
    };

    enum class TextureFormatVar : u32 {
        UNorm = 1, SNorm = 2, UInt = 3, SInt = 4, Single = 5, SRGB = 6, UHalf = 10
    };

    enum class TextureType : u32 {
        Image1D = 0, Image2D = 1, Image3D = 2, Cube = 3, CubeFar = 8
    };

    enum class ChannelType {
        Zero, One, Red, Green, Blue, Alpha
    };

    class BRTI {
    public:
        s32 BRTILength0;
        long long int BRTILength1;
        u8 Flags;
        u8 Dimensions;
        u16 TileMode;
        u16 SwizzleSize;
        u16 MipmapCount;
        u16 MultiSampleCount;
        u16 Reversed1A;
        u32 Format;
        u32 AccessFlags;
        s32 Width;
        s32 Height;
        s32 Depth;
        s32 ArrayCount;
        s32 BlockHeightLog2;
        s32 Reserved38;
        s32 Reserved3C;
        s32 Reserved40;
        s32 Reserved44;
        s32 Reserved48;
        s32 Reserved4C;
        s32 DataLength;
        s32 Alignment;
        s32 ChannelTypes;
        s32 TextureType;
        long long int NameAddress;
        long long int ParentAddress;
        long long int PtrsAddress;

        std::vector<u8> Data;
        std::vector<u8> ExtraBrtiData;

        const std::string& Name() const;

        ChannelType Channel0Type() const;
        ChannelType Channel3Type() const;
        ChannelType Channel1Type() const;
        ChannelType Channel2Type() const;

        Bntxx::TextureType Type() const;
        TextureFormatType FormatType() const;
        TextureFormatVar FormatVariant() const;

        std::vector<u8> Write();
        BRTI(Buffer& Reader);

    private:
        std::string _readonly_name;
    };
}

namespace DDSEncoder {

    struct DDSLoadResult {
        s32 width;
        s32 height;
        std::string Format;
        s32 size;
        s32 numMips;
        std::vector<u8> data;
    };

    struct EncoderInfo {
        s32 blkHeight;
        s32 blkWidth;
        s32 bpp;
        s32 formatCode;
    };

    struct DDSEncoderResult {
        std::vector<u8> Data;
        EncoderInfo format;
        s32 blockHeightLog2;
    };

    const std::unordered_map<std::string, EncoderInfo> EncoderTable = {
        { "DXT1", { 4, 4, 8, 0x1a01 } },
        { "DXT3", { 4, 4, 16, 0x1b01 } },
        { "DXT4", { 4, 4, 16, 0x1c01 } },
        { "DXT5", { 4, 4, 16, 0x1c01 } },
    };

    DDSEncoderResult EncodeTex(const DDSLoadResult& img);
    DDSLoadResult LoadDDS(const std::vector<u8>& inb);
}

namespace DDSConv {

    struct ConversionResult {
        std::vector<u8> Data;
        std::string ErrorMessage;
        bool resized;

        bool IsSuccess() const { return ErrorMessage.empty(); }

        static ConversionResult Success(std::vector<u8> data, bool resized) {
            return { data, "", resized };
        }

        static ConversionResult Fail(std::string error) {
            return { {}, error, false };
        }
    };

    ConversionResult ConvertImage(const std::vector<u8>& imgData, bool DXT5 = false, int Width = 1280, int Height = 720, bool ResizeIfNeeded = false);
}

class QuickBntx {
public:
    std::vector<Bntxx::BRTI> Textures;
    std::vector<u8> Rlt;

    QuickBntx(Buffer& Reader);

    std::vector<u8> Write();
    void ReplaceTex(const std::string& name, const DDSEncoder::DDSLoadResult& tex);
    Bntxx::BRTI* FindTex(const std::string& name);

private:
    std::vector<u8> Head;
};

} // namespace sphaira::theme
