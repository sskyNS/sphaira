#pragma once

// 移植自 SwitchThemeInjector 的 Layouts/Bflyt/（BFLYT 布局解析与补丁）。

#include <vector>
#include <string>
#include <stdexcept>
#include <memory>
#include <queue>
#include <functional>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include "theme/buffer.hpp"
#include "theme/patches.hpp"

namespace sphaira::theme {

class RGBAColor {
public:
    u8 R = 0, G = 0, B = 0, A = 0;

    RGBAColor() {}
    RGBAColor(u8 r, u8 g, u8 b, u8 a = 255) : R(r), G(g), B(b), A(a) {}
    RGBAColor(const std::string& LeByteString) {
        u32 Col = (u32)std::stoull(LeByteString, nullptr, 16);
        R = (Col & 0xFF);
        G = ((Col >> 8) & 0xFF);
        B = ((Col >> 16) & 0xFF);
        A = ((Col >> 24) & 0xFF);
    }

    std::string AsString() {
        std::stringstream str;
        str << std::hex << std::setw(8) << ((u32)(R | G << 8 | B << 16 | A << 24));
        return str.str();
    }

    static RGBAColor Read(Buffer& buf) {
        RGBAColor col;
        col.R = buf.readUInt8();
        col.G = buf.readUInt8();
        col.B = buf.readUInt8();
        col.A = buf.readUInt8();
        return col;
    }

    void Write(Buffer& buf) {
        buf.Write(R);
        buf.Write(G);
        buf.Write(B);
        buf.Write(A);
    }
};

namespace Panes {

    static inline Vector3 ReadVec3(Buffer& buf) {
        Vector3 res;
        res.X = buf.readFloat();
        res.Y = buf.readFloat();
        res.Z = buf.readFloat();
        return res;
    }

    static inline Vector2 ReadVec2(Buffer& buf) {
        Vector2 res;
        res.X = buf.readFloat();
        res.Y = buf.readFloat();
        return res;
    }

    static inline void WriteVec3(Buffer& bin, Vector3 _x) {
        bin.Write(_x.X); bin.Write(_x.Y); bin.Write(_x.Z);
    }

    static inline void WriteVec2(Buffer& bin, Vector2 _x) {
        bin.Write(_x.X); bin.Write(_x.Y);
    }

    class BasePane {
    public:
        std::unique_ptr<BasePane> UserData;
        std::weak_ptr<BasePane> Parent;
        std::vector<std::shared_ptr<BasePane>> Children;

        const std::string name;
        std::vector<u8> data;

        std::string PaneName = "";

        BasePane(const std::string& _name, u32 len) : name(_name), data(len - 8) {}

        BasePane(const std::string& _name, Buffer& reader) : name(_name) {
            auto length = reader.readUInt32();
            data = reader.readBytes(length - 8);
        }

        virtual ~BasePane() {}
        virtual void ApplyChanges(Buffer& writer) {}

        void WritePane(Buffer& writer) {
            Buffer bin;
            bin.ByteOrder = writer.ByteOrder;
            ApplyChanges(bin);
            if (bin.Length() != 0) {
                data = bin.getBuffer();
            }

            writer.WriteFixedLengthString(name, 4);
            writer.Write(u32(data.size() + 8));
            writer.Write(data);
            if (UserData)
                UserData->WritePane(writer);
        }
    };

    class BflytMaterial {
    public:
        struct TextureTransform {
            float X;
            float Y;
            float Rotation;
            float ScaleX;
            float ScaleY;
        };

        struct TextureReference {
            u16 TextureId;
            u8 WrapS;
            u8 WrapT;
        };

        std::string Name;
        u32 ForegroundColor;
        u32 BackgroundColor;

        std::vector<TextureReference> Textures;
        std::vector<TextureTransform> TextureTransformations;

        BflytMaterial(const std::vector<u8>& data, u32 Version, Endianness bo) {
            Data = data;
            Buffer buf{ Data };
            buf.ByteOrder = bo;
            Name = buf.readStr_Fixed(28);
            if (Version >= 0x08000000) {
                flags = buf.readUInt32();
                buf.readUInt32();
                ForegroundColor = buf.readUInt32_LE();
                BackgroundColor = buf.readUInt32_LE();
            } else {
                ForegroundColor = buf.readUInt32_LE();
                BackgroundColor = buf.readUInt32_LE();
                flags = buf.readUInt32();
            }

            Textures.resize(flags & 3);
            for (auto& t : Textures)
                t = { buf.readUInt16(), buf.readUInt8(), buf.readUInt8() };

            TextureTransformations.resize((flags & 0xC) >> 2);
            for (auto& t : TextureTransformations)
                t = { buf.readFloat(), buf.readFloat(), buf.readFloat(), buf.readFloat(), buf.readFloat() };
        }

        std::vector<u8> Write(u32 version, Endianness bo) {
            if (Textures.size() > 3) throw std::runtime_error("[" + Name + "] A material can have no more than 3 texture references.");
            if (TextureTransformations.size() > 3) throw std::runtime_error("[" + Name + "] A material can have no more than 3 texture transformations.");

            flags &= ~3;
            flags |= Textures.size();
            flags &= ~0xC;
            flags |= TextureTransformations.size() << 2;

            Buffer buf{ Data };
            buf.ByteOrder = bo;
            buf.Position = 0;

            buf.WriteFixedLengthString(Name, 28);
            if (version >= 0x08000000) {
                buf.writeUInt32_LE(flags);
                buf.Position += 4;
                buf.writeUInt32_LE(ForegroundColor);
                buf.writeUInt32_LE(BackgroundColor);
            } else {
                buf.writeUInt32_LE(ForegroundColor);
                buf.writeUInt32_LE(BackgroundColor);
                buf.writeUInt32_LE(flags);
            }

            for (const auto& t : Textures) {
                buf.writeUInt16_LE(t.TextureId);
                buf.Write(t.WrapS);
                buf.Write(t.WrapT);
            }

            for (const auto& t : TextureTransformations) {
                buf.Write(t.X);
                buf.Write(t.Y);
                buf.Write(t.Rotation);
                buf.Write(t.ScaleX);
                buf.Write(t.ScaleY);
            }

            return buf.getBuffer();
        }

    private:
        std::vector<u8> Data;
        u32 flags;
    };

    class Grp1Pane : public BasePane {
    public:
        u32 Version;
        std::string GroupName;
        std::vector<std::string> Panes;

        Grp1Pane(Buffer& buf, u32 version) : BasePane("grp1", buf), Version{ version } {
            LoadProperties();
        }

        Grp1Pane(u32 version) : BasePane("grp1", 8), Version{ version } {}

    private:
        void LoadProperties() {
            Buffer bin{ data };
            bin.ByteOrder = Endianness::LittleEndian;
            if (Version > 0x05020000)
                GroupName = bin.readStr_Fixed(34);
            else
                GroupName = bin.readStr_Fixed(24);
            auto NodeCount = bin.readUInt16();
            if (Version <= 0x05020000)
                bin.readUInt16();
            auto pos = bin.Position;
            for (size_t i = 0; i < NodeCount; i++) {
                bin.Position = pos + i * 24;
                Panes.push_back(bin.readStr_Fixed(24));
            }
        }

        void ApplyChanges(Buffer& bin) override {
            if (Version > 0x05020000)
                bin.WriteFixedLengthString(GroupName, 34);
            else
                bin.WriteFixedLengthString(GroupName, 24);
            bin.Write((u16)Panes.size());
            if (Version <= 0x05020000)
                bin.Write((u16)0);
            for (const auto& s : Panes)
                bin.WriteFixedLengthString(s, 24);
            data = bin.getBuffer();
        }
    };

    enum class OriginX : u8 {
        Center = 0,
        Left = 1,
        Right = 2
    };

    enum class OriginY : u8 {
        Center = 0,
        Top = 1,
        Bottom = 2
    };

    class Pan1Pane : public BasePane {
    public:
        Vector3 Position, Rotation;
        Vector2 Scale, Size;

        bool GetVisible() { return (_flag1 & 0x1) == 0x1; }
        void SetVisible(bool value) {
            if (value) _flag1 |= 0x1;
            else _flag1 &= 0xFE;
        }

        OriginX GetOriginX() { return (OriginX)((_flag2 & 0xC0) >> 6); }
        void SetOriginX(OriginX val) {
            _flag2 &= ((u8)(~0xC0));
            _flag2 |= (u8)((u8)val << 6);
        }

        OriginY GetOriginY() { return (OriginY)((_flag2 & 0x30) >> 4); }
        void SetOriginY(OriginY val) {
            _flag2 &= ((u8)(~0x30));
            _flag2 |= (u8)((u8)val << 4);
        }

        OriginX GetParentOriginX() { return (OriginX)((_flag2 & 0xC) >> 2); }
        void SetParentOriginX(OriginX val) {
            _flag2 &= ((u8)(~0xC));
            _flag2 |= (u8)((u8)val << 2);
        }

        OriginY GetParentOriginY() { return (OriginY)((_flag2 & 0x3)); }
        void SetParentOriginY(OriginY val) {
            _flag2 &= ((u8)(~0x3));
            _flag2 |= (u8)val;
        }

        Pan1Pane(Buffer& b, Endianness e, const std::string& name = "pan1") : BasePane(name, b) {
            LoadProperties(e);
        }

        void ApplyChanges(Buffer& bin) override {
            bin.Write(data);
            bin.Position = 0;
            bin.Write(_flag1);
            bin.Write(_flag2);
            bin.Position = 0x2C - 8;
            WriteVec3(bin, Position);
            WriteVec3(bin, Rotation);
            WriteVec2(bin, Scale);
            WriteVec2(bin, Size);
        }

    private:
        void LoadProperties(Endianness e) {
            Buffer buf(data);
            buf.ByteOrder = e;
            _flag1 = buf.readUInt8();
            _flag2 = buf.readUInt8();
            buf.Position += 2;
            PaneName = buf.readStr_NullTerm(0x18);
            buf.Position = 0x2c - 8;
            Position = ReadVec3(buf);
            Rotation = ReadVec3(buf);
            Scale = ReadVec2(buf);
            Size = ReadVec2(buf);
        }

        u8 _flag1;
        u8 _flag2;
    };

    class Pic1Pane : public Pan1Pane {
    public:
        RGBAColor ColorTopRight;
        RGBAColor ColorTopLeft;
        RGBAColor ColorBottomRight;
        RGBAColor ColorBottomLeft;

        u16 MaterialIndex;

        struct UVCoord {
            Vector2 TopLeft;
            Vector2 TopRight;
            Vector2 BottomLeft;
            Vector2 BottomRight;
        };
        std::vector<UVCoord> UvCoords;

        Pic1Pane(Buffer& reader, Endianness e) : Pan1Pane(reader, e, "pic1") {
            LoadProperties(e);
        }

        void ApplyChanges(Buffer& bin) override {
            Pan1Pane::ApplyChanges(bin);
            bin.Position = 0x54 - 8;
            ColorTopLeft.Write(bin);
            ColorTopRight.Write(bin);
            ColorBottomLeft.Write(bin);
            ColorBottomRight.Write(bin);
            bin.Write(MaterialIndex);
            bin.Write((u8)UvCoords.size());
            bin.Write((u8)0);
            for (const auto& uv : UvCoords) {
                WriteVec2(bin, uv.TopLeft);
                WriteVec2(bin, uv.TopRight);
                WriteVec2(bin, uv.BottomLeft);
                WriteVec2(bin, uv.BottomRight);
            }
        }

    private:
        void LoadProperties(Endianness e) {
            Buffer buf(data);
            buf.ByteOrder = e;
            buf.Position = 0x54 - 8;
            ColorTopLeft = RGBAColor::Read(buf);
            ColorTopRight = RGBAColor::Read(buf);
            ColorBottomLeft = RGBAColor::Read(buf);
            ColorBottomRight = RGBAColor::Read(buf);
            MaterialIndex = buf.readUInt16();
            int uvCount = buf.readUInt8();
            buf.readUInt8();
            for (int i = 0; i < uvCount; i++)
                UvCoords.push_back(UVCoord{ ReadVec2(buf), ReadVec2(buf), ReadVec2(buf), ReadVec2(buf) });
        }
    };

    class Txt1Pane : public Pan1Pane {
    public:
        RGBAColor FontTopColor;
        RGBAColor ShadowTopColor;
        RGBAColor FontBottomColor;
        RGBAColor ShadowBottomColor;

        Txt1Pane(Buffer& reader, Endianness e) : Pan1Pane(reader, e, "txt1") {
            LoadProperties(e);
        }

        void ApplyChanges(Buffer& bin) override {
            Pan1Pane::ApplyChanges(bin);
            bin.Position = 0x54 - 8 + 20;
            FontTopColor.Write(bin);
            FontBottomColor.Write(bin);
            bin.Position = 0x54 - 8 + 64;
            ShadowTopColor.Write(bin);
            ShadowBottomColor.Write(bin);
        }

    private:
        void LoadProperties(Endianness e) {
            Buffer buf(data);
            buf.ByteOrder = e;
            buf.Position = 0x54 - 8 + 20;
            FontTopColor = RGBAColor::Read(buf);
            FontBottomColor = RGBAColor::Read(buf);
            buf.Position = 0x54 - 8 + 64;
            ShadowTopColor = RGBAColor::Read(buf);
            ShadowBottomColor = RGBAColor::Read(buf);
        }
    };

    class Usd1Pane : public BasePane {
    public:
        enum class ValueType : u8 {
            data = 0,
            int32 = 1,
            single = 2,
            other = 3
        };

        struct EditableProperty {
            std::string Name;
            size_t ValueOffset;
            u16 ValueCount;
            ValueType type;
            std::vector<std::string> value;
        };

        std::vector<EditableProperty> Properties;

        EditableProperty* FindName(const std::string& name) {
            for (auto& p : Properties)
                if (p.Name == name) return &p;
            return nullptr;
        }

        void AddProperty(const std::string& name, const std::vector<std::string>& values, ValueType type) {
            AddedProperties.push_back({ name, 0, (u16)values.size(), type, values });
        }

        Usd1Pane(Buffer& reader) : BasePane("usd1", reader) {
            LoadProperties();
        }

        void ApplyChanges(Buffer& bin) override {
            bin.Write((u16)(Properties.size() + AddedProperties.size() + UnknownPropertiesCount));
            bin.Write((u16)0);
            for (size_t i = 0; i < 3 * AddedProperties.size(); i++) bin.Write((u32)0);
            bin.Write(data, 4, data.size() - 4);
            for (auto& m : Properties) {
                if (m.type != ValueType::int32 && m.type != ValueType::single) continue;
                bin.Position = m.ValueOffset + 0xC * AddedProperties.size();
                for (size_t i = 0; i < m.ValueCount; i++) {
                    if (m.type == ValueType::int32)
                        bin.Write(std::stoi(m.value[i]));
                    else
                        bin.Write(ParseFloat(m.value[i]));
                }
            }
            for (size_t i = 0; i < AddedProperties.size(); i++) {
                bin.Position = bin.Length();
                u32 DataOffset = (u32)bin.Position;
                for (int j = 0; j < AddedProperties[i].ValueCount; j++)
                    if (AddedProperties[i].type == ValueType::int32)
                        bin.Write(std::stoi(AddedProperties[i].value[j]));
                    else
                        bin.Write(ParseFloat(AddedProperties[i].value[j]));
                u32 NameOffest = (u32)bin.Position;
                bin.Write(AddedProperties[i].Name, Buffer::BinaryString::NullTerminated);
                bin.WriteAlign(4);
                u32 entryStart = (u32)(4 + i * 0xC);
                bin.Position = entryStart;
                bin.Write(NameOffest - entryStart);
                bin.Write(DataOffset - entryStart);
                bin.Write(AddedProperties[i].ValueCount);
                bin.Write((u8)AddedProperties[i].type);
                bin.Write((u8)0);
            }
            data = std::move(bin.getBuffer());
        }

    private:
        std::vector<EditableProperty> AddedProperties;
        size_t UnknownPropertiesCount = 0;

        void LoadProperties() {
            Buffer dataReader(data);
            dataReader.ByteOrder = Endianness::LittleEndian;
            dataReader.Position = 0;
            u16 Count = dataReader.readUInt16();
            dataReader.readUInt16();
            for (int i = 0; i < Count; i++) {
                auto EntryOffset = dataReader.Position;
                u32 NameOffset = dataReader.readUInt32();
                u32 DataOffset = dataReader.readUInt32();
                u16 ValueLen = dataReader.readUInt16();
                u8 dataType = dataReader.readUInt8();
                dataReader.readUInt8();

                if (!(dataType == 1 || dataType == 2)) {
                    UnknownPropertiesCount++;
                    continue;
                }

                auto pos = dataReader.Position;
                dataReader.Position = EntryOffset + NameOffset;
                std::string propName = dataReader.readStr_NullTerm();
                auto type = (ValueType)dataType;

                dataReader.Position = EntryOffset + DataOffset;
                std::vector<std::string> values;

                for (int j = 0; j < ValueLen; j++) {
                    if (type == ValueType::int32)
                        values.push_back(std::to_string(dataReader.readInt32()));
                    else
                        values.push_back(to_string_with_precision<18>(dataReader.readFloat()));
                }

                Properties.push_back(EditableProperty{ propName, EntryOffset + DataOffset, ValueLen, type, std::move(values) });
                dataReader.Position = pos;
            }
        }

        template <int Prec, typename T>
        std::string to_string_with_precision(const T a_value) {
            std::ostringstream out;
            out.precision(Prec);
            out << std::fixed << a_value;
            return out.str();
        }

        float ParseFloat(std::string& str) {
            auto p = str.find(',');
            if (p != std::string::npos) str[p] = '.';
            return std::stof(str);
        }
    };

    class TextureSection : public BasePane {
    public:
        std::vector<std::string> Textures;
        TextureSection(Buffer& reader);
        TextureSection();
        void ApplyChanges(Buffer& writer) override;
    };

    class MaterialsSection : public BasePane {
    public:
        u32 Version;
        std::vector<BflytMaterial> Materials;
        MaterialsSection(Buffer& reader, u32 version);
        MaterialsSection(u32 version);
        void ApplyChanges(Buffer& writer) override;
    };

    typedef std::shared_ptr<Panes::BasePane> PanePtr;
}

namespace Utils {
    template <typename T>
    static inline size_t IndexOf(const std::vector<T>& v, const T& s) {
        for (size_t i = 0; i < v.size(); i++)
            if (v[i] == s) return i;
        return SIZE_MAX;
    }
}

class BflytFile {
public:
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Panes::PanePtr;
        using difference_type = std::ptrdiff_t;
        using pointer = Panes::PanePtr*;
        using reference = Panes::PanePtr&;

        Iterator(BflytFile* file, std::vector<Panes::PanePtr>&& root) : _file(file) {
            for (auto& p : root) it.push(p);
            Step();
        }

        Iterator(BflytFile* file, std::vector<Panes::PanePtr>& root) : _file(file) {
            for (auto& p : root) it.push(p);
            Step();
        }

        const Panes::PanePtr& operator*() const {
            if (!Cur) throw std::runtime_error("The end of the sequence has been reached");
            return Cur;
        }

        Iterator& operator++() { Step(); return *this; }
        bool operator==(const Iterator& other) const { return this->_file == other._file && this->Cur == other.Cur; }
        bool operator!=(const Iterator& other) const { return this->_file != other._file || this->Cur != other.Cur; }

    private:
        void Step() {
            if (it.size() == 0)
                Cur = nullptr;
            else {
                Cur = it.front();
                it.pop();
                for (auto ptr : Cur->Children)
                    it.push(ptr);
            }
        }

        BflytFile* _file;
        std::queue<Panes::PanePtr> it;
        Panes::PanePtr Cur = nullptr;
    };

    Iterator PanesBegin() { return Iterator(this, RootPanes); }
    Iterator PanesEnd() { return Iterator(this, std::vector<Panes::PanePtr>{}); }

    BflytFile(const std::vector<u8>& file);
    ~BflytFile();
    std::vector<u8> SaveFile();

    u32 Version;

    std::vector<Panes::PanePtr> RootPanes;

    std::shared_ptr<Panes::TextureSection> GetTexSection();
    std::shared_ptr<Panes::MaterialsSection> GetMatSection();
    Panes::PanePtr GetRootElement();
    std::shared_ptr<Panes::Grp1Pane> GetRootGroup();

    Panes::PanePtr operator[](const std::string& name);

    void RemovePane(Panes::PanePtr& pane);
    void AddPane(size_t offset, Panes::PanePtr& Parent, Panes::PanePtr& pane);
    void MovePane(Panes::PanePtr& pane, Panes::PanePtr& NewParent, size_t offset);

    Panes::PanePtr FindPane(std::function<bool(const Panes::PanePtr&)> fun);
    Panes::PanePtr FindRoot(const std::string& type);

private:
    int FindRootIndex(const std::string& type);
    std::vector<Panes::PanePtr> WritePaneListForBinary();
};

class BflytPatcher {
public:
    BflytPatcher(BflytFile& layout) : lyt(layout) {}

    bool ClearUVData(const std::string& name);
    bool ApplyLayoutPatch(const std::vector<PanePatch>& Patches);
    bool ApplyMaterialsPatch(const std::vector<MaterialPatch>& Patches);
    bool AddGroupNames(const std::vector<ExtraGroup>& Groups);
    bool PatchTextureName(const std::string& original, const std::string& _new);
    u16 AddBgMat(const std::string& texName);
    bool AddBgPanel(Panes::PanePtr target, const std::string& TexName, const std::string& Pic1Name);
    bool PatchBgLayout(const PatchTemplate& patch);
    bool PanePullToFront(const std::string& paneName);
    bool PanePushBack(const std::string& paneName);

private:
    BflytFile& lyt;
};

} // namespace sphaira::theme
