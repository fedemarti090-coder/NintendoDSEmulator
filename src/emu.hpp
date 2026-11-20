#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct NdsHeader
{
    std::string title;
    std::string game_code;

    uint32_t arm9_rom_offset = 0;
    uint32_t arm9_entry_address = 0;
    uint32_t arm9_ram_address = 0;
    uint32_t arm9_size = 0;

    uint32_t arm7_rom_offset = 0;
    uint32_t arm7_entry_address = 0;
    uint32_t arm7_ram_address = 0;
    uint32_t arm7_size = 0;
};

inline uint32_t read_u32_le(const std::vector<uint8_t> &buf, size_t offset)
{
    uint32_t val = 0;
    std::memcpy(&val, buf.data() + offset, sizeof(uint32_t)); // Leggo a chunk di 32 bit
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    val = ((val & 0x000000FFu) << 24) |
          ((val & 0x0000FF00u) << 8) |
          ((val & 0x00FF0000u) >> 8) |
          ((val & 0xFF000000u) >> 24);
#endif
    return val;
}

inline std::string read_ascii_fixed(const std::vector<uint8_t> &buf, size_t offset, size_t max_len)
{
    std::string out;
    out.reserve(max_len);
    for (size_t i = 0; i < max_len && (offset + i) < buf.size(); ++i)
    {
        char c = static_cast<char>(buf[offset + i]);
        if (c == '\0')
            break;
        if (static_cast<unsigned char>(c) >= 32 && static_cast<unsigned char>(c) <= 126)
        {
            out.push_back(c);
        }
    }
    return out;
}

inline bool parse_nds_header(const std::vector<uint8_t> &rom, NdsHeader &out)
{
    if (rom.size() < 0x40)
    {
        return false;
    }

    out.title = read_ascii_fixed(rom, 0x000, 12);
    out.game_code = read_ascii_fixed(rom, 0x00C, 4);

    out.arm9_rom_offset = read_u32_le(rom, 0x020);
    out.arm9_entry_address = read_u32_le(rom, 0x024);
    out.arm9_ram_address = read_u32_le(rom, 0x028);
    out.arm9_size = read_u32_le(rom, 0x02C);

    out.arm7_rom_offset = read_u32_le(rom, 0x030);
    out.arm7_entry_address = read_u32_le(rom, 0x034);
    out.arm7_ram_address = read_u32_le(rom, 0x038);
    out.arm7_size = read_u32_le(rom, 0x03C);

    return true;
}