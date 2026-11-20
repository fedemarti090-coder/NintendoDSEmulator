#include "emu.hpp"

#include <iomanip>
#include <iostream>

void stampa_header(const NdsHeader &header)
{
    std::cout << "Titolo:        " << header.title << "\n";
    std::cout << "Game Code:     " << header.game_code << "\n";
    std::cout << std::hex << std::uppercase << std::setfill('0');
    std::cout << "ARM9 Offset:   0x" << std::setw(8) << header.arm9_rom_offset << "\n";
    std::cout << "ARM9 RAM Addr: 0x" << std::setw(8) << header.arm9_ram_address << "\n";
    std::cout << "ARM9 Entry:    0x" << std::setw(8) << header.arm9_entry_address << "\n";
    std::cout << "ARM9 Size:     0x" << std::setw(8) << header.arm9_size << "\n";
    std::cout << "ARM7 Offset:   0x" << std::setw(8) << header.arm7_rom_offset << "\n";
    std::cout << "ARM7 RAM Addr: 0x" << std::setw(8) << header.arm7_ram_address << "\n";
    std::cout << "ARM7 Entry:    0x" << std::setw(8) << header.arm7_entry_address << "\n";
    std::cout << "ARM7 Size:     0x" << std::setw(8) << header.arm7_size << "\n";
    std::cout << std::dec;
}

static void dump_words(const NdsMemory &mem, uint32_t addr, size_t count)
{
    std::cout << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t cur = addr + static_cast<uint32_t>(i * 4);
        std::cout << "  [0x" << std::setw(8) << cur << "] = 0x" << std::setw(8) << mem.read_main_ram_u32_le(cur) << "\n";
    }
    std::cout << std::dec;
}

void stampa_ram_bin(NdsMemory &mem, NdsHeader &header)
{
    std::cout << "Binari caricati correttamente in Main RAM.\n";
    std::cout << "Primi 4 word ARM9 entry (0x" << std::hex << std::uppercase << header.arm9_entry_address << std::dec << "):\n";
    dump_words(mem, header.arm9_entry_address, 4);

    std::cout << "Primi 4 word ARM7 entry (0x" << std::hex << std::uppercase << header.arm7_entry_address << std::dec << "):\n";
    dump_words(mem, header.arm7_entry_address, 4);
}