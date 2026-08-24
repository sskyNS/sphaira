#include "theme/bntx.hpp"
#include "image.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

// DXT 压缩（公有领域 stb_dxt，来自 nothings/stb）。
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include <stb_dxt.h>
#pragma GCC diagnostic pop

// 图片解码（实现位于 sphaira/source/image.cpp）。
#include <stb_image.h>

namespace sphaira::theme {

using namespace Bntxx;

// ============================ BRTI ============================

BRTI::BRTI(Buffer& Reader) {
    auto startPos = Reader.Position;
    if (Reader.readStr(4) != "BRTI")
        throw std::runtime_error("Wrong magic");

    BRTILength0 = Reader.readInt32();
    BRTILength1 = Reader.readInt64();
    Flags = Reader.readUInt8();
    Dimensions = Reader.readUInt8();
    TileMode = Reader.readUInt16();
    SwizzleSize = Reader.readUInt16();
    MipmapCount = Reader.readUInt16();
    MultiSampleCount = Reader.readUInt16();
    Reversed1A = Reader.readUInt16();
    Format = Reader.readUInt32();
    AccessFlags = Reader.readUInt32();
    Width = Reader.readInt32();
    Height = Reader.readInt32();
    Depth = Reader.readInt32();
    ArrayCount = Reader.readInt32();
    BlockHeightLog2 = Reader.readInt32();
    Reserved38 = Reader.readInt32();
    Reserved3C = Reader.readInt32();
    Reserved40 = Reader.readInt32();
    Reserved44 = Reader.readInt32();
    Reserved48 = Reader.readInt32();
    Reserved4C = Reader.readInt32();
    DataLength = Reader.readInt32();
    Alignment = Reader.readInt32();
    ChannelTypes = Reader.readInt32();
    TextureType = Reader.readInt32();
    NameAddress = Reader.readInt64();
    ParentAddress = Reader.readInt64();
    PtrsAddress = Reader.readInt64();

    ExtraBrtiData = Reader.readBytes((int)(BRTILength1 - (Reader.Position - startPos)));

    Reader.Position = (size_t)NameAddress;
    _readonly_name = Reader.readStr_U16Prefix();

    Reader.Position = (size_t)PtrsAddress;
    u64 BaseOffset = Reader.readInt64();

    if (MipmapCount > 1)
        throw std::runtime_error("mipmaps are not supported");

    Reader.Position = (size_t)BaseOffset;
    Data = Reader.readBytes(DataLength);
}

std::vector<u8> BRTI::Write() {
    Buffer bin;
    bin.ByteOrder = Endianness::LittleEndian;
    bin.Write("BRTI");
    bin.Write(BRTILength0);
    bin.Write(BRTILength1);
    bin.Write(Flags);
    bin.Write(Dimensions);
    bin.Write(TileMode);
    bin.Write(SwizzleSize);
    bin.Write(MipmapCount);
    bin.Write(MultiSampleCount);
    bin.Write(Reversed1A);
    bin.Write(Format);
    bin.Write(AccessFlags);
    bin.Write(Width);
    bin.Write(Height);
    bin.Write(Depth);
    bin.Write(ArrayCount);
    bin.Write(BlockHeightLog2);
    bin.Write(Reserved38);
    bin.Write(Reserved3C);
    bin.Write(Reserved40);
    bin.Write(Reserved44);
    bin.Write(Reserved48);
    bin.Write(Reserved4C);
    bin.Write((s32)Data.size());
    bin.Write(Alignment);
    bin.Write(ChannelTypes);
    bin.Write(TextureType);
    bin.Write(NameAddress);
    bin.Write((long long int)ParentAddress);
    bin.Write((long long int)PtrsAddress);
    bin.Write(ExtraBrtiData);
    return bin.getBuffer();
}

const std::string& BRTI::Name() const { return _readonly_name; }

ChannelType BRTI::Channel0Type() const { return (ChannelType)((ChannelTypes >> 0) & 0xff); }
ChannelType BRTI::Channel3Type() const { return (ChannelType)((ChannelTypes >> 8) & 0xff); }
ChannelType BRTI::Channel1Type() const { return (ChannelType)((ChannelTypes >> 16) & 0xff); }
ChannelType BRTI::Channel2Type() const { return (ChannelType)((ChannelTypes >> 24) & 0xff); }

Bntxx::TextureType BRTI::Type() const { return (Bntxx::TextureType)TextureType; }
TextureFormatType BRTI::FormatType() const { return (TextureFormatType)((Format >> 8) & 0xff); }
TextureFormatVar BRTI::FormatVariant() const { return (TextureFormatVar)((Format >> 0) & 0xff); }

// ============================ DDS ============================

namespace {

s32 DIV_ROUND_UP(s32 n, s32 d) { return (n + d - 1) / d; }
s32 round_up(s32 x, s32 y) { return ((x - 1) | (y - 1)) + 1; }

s32 pow2_round_up(s32 x) {
    x -= 1;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

s32 Log2(s32 v) {
    s32 r = ((0xFFFF - v) >> 31) & 0x10;
    v >>= r;
    s32 shift = ((0xFF - v) >> 31) & 0x8;
    v >>= shift;
    r |= shift;
    shift = ((0xF - v) >> 31) & 0x4;
    v >>= shift;
    r |= shift;
    shift = ((0x3 - v) >> 31) & 0x2;
    v >>= shift;
    r |= shift;
    r |= (v >> 1);
    return r;
}

s32 getBlockHeight(s32 height) {
    s32 blockHeight = pow2_round_up(height / 8);
    if (blockHeight > 16) blockHeight = 16;
    return blockHeight;
}

s32 getAddrBlockLinear(s32 x, s32 y, s32 image_width, s32 bytes_per_pixel, s32 base_address, s32 blockHeight) {
    auto image_width_in_gobs = DIV_ROUND_UP(image_width * bytes_per_pixel, 64);
    auto GOB_address = (base_address
        + (y / (8 * blockHeight)) * 512 * blockHeight * image_width_in_gobs
        + (x * bytes_per_pixel / 64) * 512 * blockHeight
        + (y % (8 * blockHeight) / 8) * 512);
    x *= bytes_per_pixel;
    return (GOB_address + ((x % 64) / 32) * 256 + ((y % 8) / 2) * 64 + ((x % 32) / 16) * 32 + (y % 2) * 16 + (x % 16));
}

std::vector<u8> swizzle(s32 width, s32 height, s32 blkWidth, s32 blkHeight, bool roundPitch, s32 bpp, s32 tileMode, s32 blockHeightLog2, const std::vector<u8>& data, s32 toSwizzle) {
    auto blockHeight = 1 << blockHeightLog2;
    width = DIV_ROUND_UP(width, blkWidth);
    height = DIV_ROUND_UP(height, blkHeight);

    auto pitch = -1;
    auto surfSize = -1;
    if (tileMode == 1) {
        pitch = width * bpp;
        if (roundPitch) pitch = round_up(pitch, 32);
        surfSize = pitch * height;
    } else {
        pitch = round_up(width * bpp, 64);
        surfSize = pitch * round_up(height, blockHeight * 8);
    }

    std::vector<u8> res(surfSize);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            auto pos = -1;
            if (tileMode == 1)
                pos = y * pitch + x * bpp;
            else
                pos = getAddrBlockLinear(x, y, width, bpp, 0, blockHeight);
            auto pos_ = (y * width + x) * bpp;
            if (pos + bpp <= surfSize) {
                if (toSwizzle)
                    std::memcpy(res.data() + pos, data.data() + pos_, bpp);
                else
                    std::memcpy(res.data() + pos_, data.data() + pos, bpp);
            }
        }
    return res;
}

std::tuple<s32, s32> getCurrentMipOffset_Size(s32 width, s32 height, s32 blkWidth, s32 blkHeight, s32 bpp, s32 currLevel) {
    s32 offset = 0;
    s32 width_ = 0;
    s32 height_ = 0;
    for (int mipLevel = 0; mipLevel < currLevel; mipLevel++) {
        width_ = DIV_ROUND_UP(std::max(1, width >> mipLevel), blkWidth);
        height_ = DIV_ROUND_UP(std::max(1, height >> mipLevel), blkHeight);
        offset += width_ * height_ * bpp;
    }
    width_ = DIV_ROUND_UP(std::max(1, width >> currLevel), blkWidth);
    height_ = DIV_ROUND_UP(std::max(1, height >> currLevel), blkHeight);
    auto size = width_ * height_ * bpp;
    return std::tuple<s32, s32>(offset, size);
}

s32 ToInt32(const u8* ptr) {
    union { s32 val; u8 chars[4]; } p;
    p.chars[0] = ptr[0];
    p.chars[1] = ptr[1];
    p.chars[2] = ptr[2];
    p.chars[3] = ptr[3];
    return p.val;
}

} // namespace

DDSEncoder::DDSLoadResult DDSEncoder::LoadDDS(const std::vector<u8>& inb) {
    std::string Format = "ZZZZ";
    for (int i = 0; i < 4; i++)
        Format[i] = inb[i + 0x54];

    if (!EncoderTable.count(Format))
        throw std::runtime_error("Unsupported format : only DXT1 and DXT4 encodings are supported for DDS");

    s32 bpp = EncoderTable.at(Format).bpp;

    auto width = ToInt32(inb.data() + 0x10);
    auto height = ToInt32(inb.data() + 0xC);
    auto size = ((width + 3) >> 2) * ((height + 3) >> 2) * bpp;
    auto numMips = 0;
    auto mipSize = 0;
    std::vector<u8> res(size + mipSize);
    std::memcpy(res.data(), inb.data() + 0x80, size + mipSize);
    return DDSLoadResult{ width, height, Format, size, numMips, res };
}

DDSEncoder::DDSEncoderResult DDSEncoder::EncodeTex(const DDSEncoder::DDSLoadResult& img) {
    auto numMips = 1;
    auto alignment = 512;

    auto fmt = EncoderTable.at(img.Format);

    auto blockHeight = getBlockHeight(DIV_ROUND_UP(img.height, fmt.blkHeight));
    auto blockHeightLog2 = Log2(blockHeight);
    auto linesPerBlockHeight = blockHeight * 8;

    auto surfSize = 0;
    auto blockHeightShift = 0;

    std::vector<s32> mipOfsets;
    std::vector<u8> res;

    for (s32 mipLevel = 0; mipLevel < numMips; mipLevel++) {
        auto offSize = getCurrentMipOffset_Size(img.width, img.height, fmt.blkWidth, fmt.blkHeight, fmt.bpp, mipLevel);
        std::vector<u8> data(std::get<1>(offSize));
        std::memcpy(data.data(), img.data.data() + std::get<0>(offSize), std::get<1>(offSize));
        auto width_ = std::max(1, img.width >> mipLevel);
        auto height_ = std::max(1, img.height >> mipLevel);
        auto width__ = DIV_ROUND_UP(width_, fmt.blkWidth);
        auto height__ = DIV_ROUND_UP(height_, fmt.blkHeight);
        int dataAlignBytes = round_up(surfSize, alignment) - surfSize;
        surfSize += dataAlignBytes;
        mipOfsets.push_back(surfSize);
        if (pow2_round_up(height__) < linesPerBlockHeight)
            blockHeightShift += 1;
        auto pitch = round_up(width__ * fmt.bpp, 64);
        surfSize += pitch * round_up(height__, std::max(1, blockHeight >> blockHeightShift) * 8);

        if (dataAlignBytes != 0) {
            for (int i = 0; i < dataAlignBytes; i++)
                res.push_back(0);
        }
        auto tmpVec = swizzle(width_, height_, fmt.blkWidth, fmt.blkHeight, true, fmt.bpp, 0, std::max(0, blockHeightLog2 - blockHeightShift), data, true);
        res.insert(res.end(), tmpVec.begin(), tmpVec.end());
    }

    return DDSEncoderResult{ res, fmt, blockHeightLog2 };
}

// ============================ DDS 转换 ============================

namespace {

int imin(int x, int y) { return (x < y) ? x : y; }

void extractBlock(const unsigned char* src, int x, int y, int w, int h, unsigned char* block) {
    int i, j;
    if ((w - x >= 4) && (h - y >= 4)) {
        src += x * 4;
        src += y * w * 4;
        for (i = 0; i < 4; ++i) {
            *(unsigned int*)block = *(unsigned int*)src; block += 4; src += 4;
            *(unsigned int*)block = *(unsigned int*)src; block += 4; src += 4;
            *(unsigned int*)block = *(unsigned int*)src; block += 4; src += 4;
            *(unsigned int*)block = *(unsigned int*)src; block += 4;
            src += (w * 4) - 12;
        }
        return;
    }

    int bw = imin(w - x, 4);
    int bh = imin(h - y, 4);
    int bx, by;

    const int rem[] = {
        0, 0, 0, 0,
        0, 1, 0, 1,
        0, 1, 2, 0,
        0, 1, 2, 3
    };

    for (i = 0; i < 4; ++i) {
        by = rem[(bh - 1) * 4 + i] + y;
        for (j = 0; j < 4; ++j) {
            bx = rem[(bw - 1) * 4 + j] + x;
            block[(i * 4 * 4) + (j * 4) + 0] = src[(by * (w * 4)) + (bx * 4) + 0];
            block[(i * 4 * 4) + (j * 4) + 1] = src[(by * (w * 4)) + (bx * 4) + 1];
            block[(i * 4 * 4) + (j * 4) + 2] = src[(by * (w * 4)) + (bx * 4) + 2];
            block[(i * 4 * 4) + (j * 4) + 3] = src[(by * (w * 4)) + (bx * 4) + 3];
        }
    }
}

} // namespace

DDSConv::ConversionResult DDSConv::ConvertImage(const std::vector<u8>& imgData, bool DXT5, int Width, int Height, bool ResizeIfNeeded) {
    if ((Width % 4) || (Height % 4))
        return DDSConv::ConversionResult::Fail("Width and height must be multiples of 4");

    int image_w = 0, image_h = 0;
    auto image_data = stbi_load_from_memory(imgData.data(), (int)imgData.size(), &image_w, &image_h, nullptr, 4);
    if (!image_data)
        return DDSConv::ConversionResult::Fail("Failed to load the source image");

    bool imageResized = false;
    std::vector<u8> resized;
    const u8* pixels = image_data;
    bool owns_image_data = true;

    if (image_w != Width || image_h != Height) {
        if (!ResizeIfNeeded) {
            stbi_image_free(image_data);
            return DDSConv::ConversionResult::Fail("Image dimensions don't match the required ones.");
        }

        auto r = sphaira::ImageResize(std::span<const u8>(image_data, (size_t)image_w * image_h * 4), image_w, image_h, Width, Height);
        stbi_image_free(image_data);
        owns_image_data = false;
        if (r.data.empty())
            return DDSConv::ConversionResult::Fail("Failed to resize the source image");
        resized = std::move(r.data);
        pixels = resized.data();
        imageResized = true;
    }

    const int BytePerBlock = DXT5 ? 16 : 8;

    Buffer bin;
    bin.ByteOrder = Endianness::LittleEndian;
    bin.Write("DDS ");
    bin.Write((u32)0x7c);
    bin.Write((u32)0xA1007);
    bin.Write((u32)Height);
    bin.Write((u32)Width);
    bin.Write((u32)((Width * Height / 16) * BytePerBlock));
    bin.Write((u32)0);
    bin.Write((u32)0);
    for (int i = 0; i < 11; i++)
        bin.Write((u32)0);
    bin.Write((u32)0x20);
    bin.Write((u32)0x4);
    bin.Write(DXT5 ? "DXT5" : "DXT1");
    for (int i = 0; i < 5; i++)
        bin.Write((u32)0);
    bin.Write((u32)0x401008);
    for (int i = 0; i < 4; i++)
        bin.Write((u32)0);

    unsigned char block[64];
    std::vector<u8> dst(BytePerBlock);
    for (int y = 0; y < Height; y += 4) {
        for (int x = 0; x < Width; x += 4) {
            extractBlock(pixels, x, y, Width, Height, block);
            stb_compress_dxt_block(dst.data(), block, DXT5, STB_DXT_DITHER | STB_DXT_HIGHQUAL);
            bin.Write(dst);
        }
    }

    if (owns_image_data)
        stbi_image_free(image_data);

    return DDSConv::ConversionResult::Success(bin.getBuffer(), imageResized);
}

// ============================ QuickBntx ============================

QuickBntx::QuickBntx(Buffer& Reader) {
    if (Reader.readStr(4) != "BNTX")
        throw std::runtime_error("Wrong magic");

    Reader.readInt32();
    Reader.readInt32();
    Reader.readUInt16();
    Reader.readUInt16();
    Reader.readInt32();
    Reader.readInt32();
    s32 RelocAddress = Reader.readInt32();
    Reader.readInt32();

    if (Reader.readStr(4) != "NX  ")
        throw std::runtime_error("Wrong magic");

    u32 TexturesCount = Reader.readUInt32();
    s64 InfoPtrsAddress = Reader.readInt64();
    Reader.readInt64();
    Reader.readInt64();
    Reader.readUInt32();

    Reader.Position = (size_t)InfoPtrsAddress;
    auto FirstBrti = (int)Reader.readInt64();
    Reader.Position = 0;
    Head = Reader.readBytes(FirstBrti);

    for (u32 Index = 0; Index < TexturesCount; Index++) {
        Reader.Position = (size_t)InfoPtrsAddress + Index * 8;
        Reader.Position = (size_t)Reader.readInt64();
        Textures.push_back(Bntxx::BRTI(Reader));
    }

    Reader.Position = RelocAddress;
    Rlt = Reader.readBytes((u32)(Reader.Length() - Reader.Position));
}

std::vector<u8> QuickBntx::Write() {
    Buffer bin;
    bin.ByteOrder = Endianness::LittleEndian;
    bin.Write(Head);
    std::vector<s64> TexPositions;
    for (auto t : Textures) {
        TexPositions.push_back(bin.Position);
        bin.Write(t.Write());
    }

    std::vector<s64> TexDataPositions;
    bin.WriteAlign(0x10);
    bin.Write("BRTD");
    bin.Write((s32)0);
    bin.Write((s32)0);
    bin.Write((s32)0);
    for (auto t : Textures) {
        TexDataPositions.push_back(bin.Position);
        bin.Write(t.Data);
        bin.WriteAlign(0x10);
    }
    bin.WriteAlign(0x1000);
    u32 rltPos = (u32)bin.Position;
    bin.Write(Rlt);

    bin.Position = 0x18;
    bin.Write((u32)rltPos);
    bin.Write((u32)bin.Length());
    bin.Position = (size_t)TexDataPositions[0] - 8;
    bin.Write((long long int)(rltPos - (TexDataPositions[0] - 0x10)));
    for (u32 i = 0; i < TexPositions.size(); i++) {
        bin.Position = (size_t)TexPositions[i] + 0x2A0;
        bin.Write((long long int)TexDataPositions[i]);
    }
    bin.Position = rltPos + 4;
    bin.Write(rltPos);
    return bin.getBuffer();
}

Bntxx::BRTI* QuickBntx::FindTex(const std::string& name) {
    for (u32 i = 0; i < Textures.size(); i++)
        if (Textures[i].Name() == name) return &Textures[i];
    return nullptr;
}

void QuickBntx::ReplaceTex(const std::string& name, const DDSEncoder::DDSLoadResult& tex) {
    auto target = FindTex(name);
    if (!target)
        throw std::runtime_error("Couldn't find texture");
    auto encoded = DDSEncoder::EncodeTex(tex);
    target->Data = encoded.Data;
    target->TextureType = (s32)Bntxx::TextureType::Image2D;
    target->Format = (u32)encoded.format.formatCode;
    target->ChannelTypes = 0x05040302;
    target->Width = tex.width;
    target->Height = tex.height;
    target->TileMode = 0;
    target->SwizzleSize = 0;
    target->Reversed1A = 0;
    target->Reserved4C = 0;
    target->Reserved48 = 0;
    target->Reserved44 = 0;
    target->Reserved40 = 0;
    target->Reserved3C = 0;
    target->Reserved38 = 0x00010007;
    target->MipmapCount = 1;
    target->Flags = 0x01;
    target->Depth = 1;
    target->BlockHeightLog2 = encoded.blockHeightLog2;
    target->Alignment = 0x200;
    target->AccessFlags = 0x20;
}

} // namespace sphaira::theme
