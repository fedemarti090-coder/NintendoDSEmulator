#include "src/emuStruct.hpp"
#include "src/emu.hpp"
#include "src/debug.hpp"
#include "src/instructionSet.hpp"

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

    // stampa_header(header);

    NdsMemory mem;
    if (!load_binaries_into_main_ram(rom, header, mem))
    {
        std::cerr << "Errore: caricamento ARM9/ARM7 in Main RAM fallito.\n";
        return 1;
    }

    // stampa_ram_bin(mem, header);

    CpuState cpu{};
    cpu.r[15] = header.arm9_entry_address;

    constexpr uint64_t max_steps = 100000;
    uint64_t steps = 0;

    while (steps < max_steps)
    {
        ++steps;
        if (!step_once(cpu, mem))
        {
            break;
        };
    }

    std::cout << std::dec
              << "Passi eseguiti: " << steps << "\n"
              << std::hex << std::uppercase << std::setfill('0')
              << "PC finale:   0x" << std::setw(8) << cpu.r[15] << "\n"
              << "LR (R14):    0x" << std::setw(8) << cpu.r[14] << "\n"
              << "CPSR finale: 0x" << std::setw(8) << cpu.cpsr << "\n"
              << std::dec;

    return 0;
}