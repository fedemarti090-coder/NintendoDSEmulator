#pragma once

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include <cstdint>

constexpr size_t NDS_MAIN_RAM_SIZE = 4u * 1024u * 1024u;
constexpr uint32_t NDS_MAIN_RAM_BASE = 0x02000000u;
constexpr uint32_t NDS_MAIN_RAM_MASK = 0x003FFFFFu;

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

struct NdsMemory
{
    std::vector<uint8_t> main_ram;
    mutable uint8_t ipcsync_peer_val = 0u;

    NdsMemory() : main_ram(NDS_MAIN_RAM_SIZE, 0) {}

    uint8_t read_main_ram_u8(uint32_t addr) const
    {
        if (addr == 0x04000180u)
        {
            return ipcsync_peer_val;
        }
        return main_ram[addr & NDS_MAIN_RAM_MASK];
    }

    void write_main_ram_u8(uint32_t addr, uint8_t val)
    {
        if (addr == 0x04000181u)
        {
            ipcsync_peer_val = val;
        }
        main_ram[addr & NDS_MAIN_RAM_MASK] = val;
    }

    uint32_t read_main_ram_u32_le(uint32_t addr) const
    {
        uint32_t offset = addr & NDS_MAIN_RAM_MASK;
        uint32_t val = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            val |= static_cast<uint32_t>(main_ram[(offset + i) & NDS_MAIN_RAM_MASK]) << (i * 8);
        }
        return val;
    }

    void write_main_ram_u32_le(uint32_t addr, uint32_t value)
    {
        write_main_ram_u8(addr + 0u, static_cast<uint8_t>(value & 0xFFu));
        write_main_ram_u8(addr + 1u, static_cast<uint8_t>((value >> 8) & 0xFFu));
        write_main_ram_u8(addr + 2u, static_cast<uint8_t>((value >> 16) & 0xFFu));
        write_main_ram_u8(addr + 3u, static_cast<uint8_t>((value >> 24) & 0xFFu));
    }
};

struct CpuState
{
    uint32_t r[16]{};
    uint32_t cpsr = 0x1F;
};