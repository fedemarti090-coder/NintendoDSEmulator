#include "src/emu.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

static bool load_file(const std::string &path, std::vector<uint8_t> &out)
{
    // Leggo il file salvandolo in out ( usando & )
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return false;
    }
    std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    return static_cast<bool>(file.read(reinterpret_cast<char *>(out.data()), size));
}

int main(int argc, char **argv)
{
    std::string rom_path = (argc > 1) ? argv[1] : "../roms/pong.nds";

    std::vector<uint8_t> rom;
    if (!load_file(rom_path, rom))
    {
        std::cerr << "Errore: impossibile leggere il file ROM: " << rom_path << "\n";
        return 1;
    }

    NdsHeader header;
    if (!parse_nds_header(rom, header))
    {
        std::cerr << "Errore: header .nds non valido o file troppo piccolo.\n";
        return 1;
    }

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

    return 0;
}