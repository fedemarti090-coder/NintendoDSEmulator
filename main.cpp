#include "src/emuStruct.hpp"
#include "src/emu.hpp"
#include "src/debug.hpp"
#include "src/instructionSet.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <utility>
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
    CpuState arm7{};
    arm7.cpu_id = 1u;
    arm7.r[15] = header.arm7_entry_address;
    bool arm7_running = true;

    constexpr uint64_t max_steps = 10000000;
    uint64_t steps = 0;
    std::array<uint32_t, 32> pc_history{};
    std::array<uint32_t, 32> cpsr_history{};
    size_t history_next = 0;
    size_t history_count = 0;
    std::array<uint32_t, 32> first_wfi_pc_history{};
    size_t first_wfi_history_count = 0;
    bool first_wfi_captured = false;
    uint32_t first_wfi_pc = 0u;
    uint32_t first_wfi_lr = 0u;
    uint32_t first_wfi_sp = 0u;
    uint32_t first_wfi_cpsr = 0u;

    while (steps < max_steps)
    {
        ++steps;
        pc_history[history_next] = cpu.r[15];
        cpsr_history[history_next] = cpu.cpsr;
        history_next = (history_next + 1u) % pc_history.size();
        history_count = std::min(history_count + 1u, pc_history.size());
        uint64_t previous_wfi_count = cpu.wfi_count;
        if (!step_once(cpu, mem))
        {
            break;
        }

        if (arm7_running && !step_once(arm7, mem, false))
            arm7_running = false;

        if (!first_wfi_captured && cpu.wfi_count != previous_wfi_count)
        {
            first_wfi_captured = true;
            first_wfi_pc = cpu.last_wfi_pc;
            first_wfi_lr = cpu.last_wfi_lr;
            first_wfi_sp = cpu.last_wfi_sp;
            first_wfi_cpsr = cpu.last_wfi_cpsr;
            first_wfi_history_count = history_count;
            for (size_t i = 0; i < history_count; ++i)
            {
                size_t index = (history_next + pc_history.size() - history_count + i) % pc_history.size();
                first_wfi_pc_history[i] = pc_history[index];
            }
        }
    }

    mem.active_cpu = 0u;

    if (steps == max_steps)
    {
        std::cout << "Snapshot al limite:\n"
                  << std::hex << std::uppercase << std::setfill('0')
                  << "PC=0x" << std::setw(8) << cpu.r[15]
                  << " Instr=0x" << std::setw(8) << mem.read_main_ram_u32_le(cpu.r[15])
                  << " CPSR=0x" << std::setw(8) << cpu.cpsr << "\n";

        for (uint32_t reg = 0; reg < 14u; ++reg)
        {
            std::cout << "R" << std::dec << reg << "=0x" << std::hex
                      << std::setw(8) << cpu.r[reg]
                      << ((reg % 4u == 3u) ? "\n" : "  ");
        }

        uint32_t first_addr = cpu.r[15] - 32u;
        std::cout << "Memoria attorno al PC:\n";
        for (uint32_t addr = first_addr; addr <= cpu.r[15] + 32u; addr += 4u)
        {
            std::cout << "0x" << std::setw(8) << addr << ": 0x"
                      << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
        }

        std::cout << "Ultimi PC eseguiti:\n";
        for (size_t i = 0; i < history_count; ++i)
        {
            size_t index = (history_next + pc_history.size() - history_count + i) % pc_history.size();
            std::cout << "0x" << std::setw(8) << pc_history[index]
                      << ((i % 4u == 3u) ? "\n" : "  ");
        }

        std::cout << "Registri I/O:\n";
        for (uint32_t addr : {0x04000000u, 0x04000004u, 0x04000100u, 0x04000180u})
        {
            std::cout << "0x" << std::setw(8) << addr << ": 0x"
                      << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
        }
        std::cout << std::dec;
    }
    else if (!cpu.waiting_for_interrupt)
    {
        std::cout << "Stato all'arresto: PC=0x"
                  << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << cpu.r[15]
                  << " CPSR=0x" << std::setw(8) << cpu.cpsr;
        if ((cpu.cpsr & (1u << 5)) != 0u)
        {
            uint16_t instr = static_cast<uint16_t>(mem.read_main_ram_u8(cpu.r[15])) |
                             (static_cast<uint16_t>(mem.read_main_ram_u8(cpu.r[15] + 1u)) << 8);
            std::cout << " InstrThumb=0x" << std::setw(4) << instr << "\n";
        }
        else
        {
            std::cout << " InstrArm=0x" << std::setw(8) << mem.read_main_ram_u32_le(cpu.r[15]) << "\n";
        }
        for (uint32_t reg = 0u; reg < 15u; ++reg)
        {
            std::cout << "R" << std::dec << reg << "=0x" << std::hex
                      << std::setw(8) << cpu.r[reg] << ((reg % 4u == 3u) ? "\n" : "  ");
        }
        std::cout << "ITCM Thumb:";
        for (uint32_t addr = 0x01FF8000u; addr < 0x01FF8010u; addr += 2u)
        {
            uint16_t instr = static_cast<uint16_t>(mem.read_main_ram_u8(addr)) |
                             (static_cast<uint16_t>(mem.read_main_ram_u8(addr + 1u)) << 8);
            std::cout << " 0x" << std::setw(4) << instr;
        }
        std::cout << "\nStack:";
        for (uint32_t offset = 0u; offset < 16u; offset += 4u)
            std::cout << " 0x" << std::setw(8) << mem.read_main_ram_u32_le(cpu.r[13] + offset);
        std::cout << "\n";
        std::cout << "Ultime istruzioni prima dell'arresto:\n";
        for (size_t i = 0; i < history_count; ++i)
        {
            size_t index = (history_next + pc_history.size() - history_count + i) % pc_history.size();
            uint32_t historical_pc = pc_history[index];
            bool was_thumb = (cpsr_history[index] & (1u << 5)) != 0u;
            std::cout << "0x" << std::setw(8) << historical_pc
                      << (was_thumb ? "[T] " : "[A] ");
            if (was_thumb)
            {
                uint16_t instr = static_cast<uint16_t>(mem.read_main_ram_u8(historical_pc)) |
                                 (static_cast<uint16_t>(mem.read_main_ram_u8(historical_pc + 1u)) << 8);
                std::cout << "0x" << std::setw(4) << instr;
            }
            else
            {
                std::cout << "0x" << std::setw(8) << mem.read_main_ram_u32_le(historical_pc);
            }
            std::cout << ((i % 2u == 1u) ? "\n" : "  ");
        }
        std::cout << "IE=0x" << std::setw(8) << mem.read_main_ram_u32_le(0x04000210u)
                  << " IF=0x" << std::setw(8) << mem.read_main_ram_u32_le(0x04000214u) << "\n"
                  << std::dec;
        if (cpu.r[15] == 0x02BB407Au)
        {
            std::cout << "Istruzioni BIOS e target BLX:\n"
                      << std::hex << std::uppercase << std::setfill('0');
            for (uint32_t addr = 0x01FF804Cu; addr <= 0x01FF8060u; addr += 4u)
            {
                std::cout << "0x" << std::setw(8) << addr << ": 0x"
                          << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
            }
            for (uint32_t addr = 0x02BB4078u; addr <= 0x02BB4084u; addr += 4u)
            {
                std::cout << "0x" << std::setw(8) << addr << ": 0x"
                          << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
            }
            std::cout << std::dec;
        }
    }

    std::cout << std::dec
              << "Passi eseguiti: " << steps << "\n"
              << std::hex << std::uppercase << std::setfill('0')
              << "PC finale:   0x" << std::setw(8) << cpu.r[15] << "\n"
              << "LR (R14):    0x" << std::setw(8) << cpu.r[14] << "\n"
              << "CPSR finale: 0x" << std::setw(8) << cpu.cpsr << "\n"
              << std::dec;

    std::cout << std::hex << std::uppercase << std::setfill('0')
              << "ARM7 PC=0x" << std::setw(8) << arm7.r[15]
              << " CPSR=0x" << std::setw(8) << arm7.cpsr
              << (arm7_running ? " [running]\n" : " [stopped]\n")
              << std::dec;

    if (!arm7_running)
    {
        std::cout << "ARM7 arrestato/non gestito: PC=0x" << std::hex << std::uppercase
                  << std::setfill('0') << std::setw(8) << arm7.r[15]
                  << " CPSR=0x" << std::setw(8) << arm7.cpsr << std::dec << "\n";
    }

    if (cpu.waiting_for_interrupt)
    {
        std::cout << "CPU in attesa di interrupt; registri I/O:\n"
                  << std::hex << std::uppercase << std::setfill('0');
        for (uint32_t reg = 0; reg < 16u; ++reg)
        {
            std::cout << "R" << std::dec << reg << "=0x" << std::hex
                      << std::setw(8) << cpu.r[reg]
                      << ((reg % 4u == 3u) ? "\n" : "  ");
        }

        for (const auto &[name, addr] : {
                 std::pair{"DISPCNT", 0x04000000u},
                 std::pair{"DISPSTAT/VCOUNT", 0x04000004u},
                 std::pair{"TIMER0", 0x04000100u},
                 std::pair{"IPCSYNC", 0x04000180u},
                 std::pair{"IME", 0x04000208u},
                 std::pair{"IE", 0x04000210u},
                 std::pair{"IF", 0x04000214u}})
        {
            std::cout << name << " [0x" << std::setw(8) << addr << "]: 0x"
                      << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
        }
        std::cout << std::dec;

        if (cpu.last_wfi_pc != 0u)
        {
            std::cout << "Ultimo WFI: PC=0x" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(8) << cpu.last_wfi_pc
                      << " LR=0x" << std::setw(8) << cpu.last_wfi_lr
                      << " SP=0x" << std::setw(8) << cpu.last_wfi_sp
                      << " CPSR=0x" << std::setw(8) << cpu.last_wfi_cpsr
                      << " conteggio=" << std::dec << cpu.wfi_count << "\n";

            std::cout << "Istruzioni attorno al WFI:\n"
                      << std::hex << std::uppercase << std::setfill('0');
            for (uint32_t addr = cpu.last_wfi_pc - 8u; addr <= cpu.last_wfi_pc + 8u; addr += 4u)
            {
                std::cout << "0x" << std::setw(8) << addr << ": 0x"
                          << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
            }

            if (cpu.last_wfi_sp != 0u)
            {
                std::cout << "Stack attorno a SP:\n";
                for (uint32_t offset = 0u; offset <= 16u; offset += 4u)
                {
                    uint32_t addr = cpu.last_wfi_sp - 8u + offset;
                    std::cout << "0x" << std::setw(8) << addr << ": 0x"
                              << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
                }
            }
            else
            {
                std::cout << "Stack non ispezionato: SP nullo al WFI.\n";
            }

            size_t trace_size = mem.memory_trace_size();
            size_t trace_start = trace_size > 128u ? trace_size - 128u : 0u;
            std::cout << "Accessi durante l'ultimo handler IRQ ("
                      << std::dec << trace_size << " eventi, buffer max " << NDS_MEMORY_TRACE_CAPACITY << "):\n"
                      << std::hex << std::uppercase << std::setfill('0');
            for (size_t i = trace_start; i < trace_size; ++i)
            {
                const MemoryAccessTrace &event = mem.memory_trace_at(i);
                std::cout << "PC=0x" << std::setw(8) << event.pc
                          << (event.is_write ? " W" : " R") << std::dec
                          << static_cast<unsigned int>(event.width * 8u) << std::hex
                          << " [0x" << std::setw(8) << event.address << "] = 0x"
                          << std::setw(event.width * 2u) << event.value << "\n";
            }

            std::cout << "Callback V-Blank BIOS [0x02FF0000]: 0x"
                      << std::setw(8) << mem.read_main_ram_u32_le(0x02FF0000u) << "\n";
            std::cout << std::dec;
        }

        size_t startup_trace_size = mem.startup_io_trace_size();
        auto is_relevant_startup_io = [](uint32_t addr)
        {
            return (addr >= 0x04000180u && addr < 0x04000190u) ||
                   (addr >= 0x04000208u && addr < 0x04000218u) ||
                   (addr >= 0x04000004u && addr < 0x04000008u) ||
                   addr == 0x04100000u;
        };
        size_t relevant_startup_trace_size = 0u;
        for (size_t i = 0; i < startup_trace_size; ++i)
        {
            if (is_relevant_startup_io(mem.startup_io_trace_at(i).address))
                ++relevant_startup_trace_size;
        }
        std::cout << "Accessi IPC/IRQ/video durante l'avvio ARM9 ("
                  << std::dec << relevant_startup_trace_size << " eventi):\n"
                  << std::hex << std::uppercase << std::setfill('0');
        for (size_t i = 0; i < startup_trace_size; ++i)
        {
            const MemoryAccessTrace &event = mem.startup_io_trace_at(i);
            if (!is_relevant_startup_io(event.address))
                continue;
            std::cout << "PC=0x" << std::setw(8) << event.pc
                      << (event.is_write ? " W" : " R") << std::dec
                      << static_cast<unsigned int>(event.width * 8u) << std::hex
                      << " [0x" << std::setw(8) << event.address << "] = 0x"
                      << std::setw(event.width * 2u) << event.value << "\n";
        }

        if (first_wfi_captured)
        {
            std::cout << "Primo WFI: PC=0x" << std::hex << std::uppercase << std::setfill('0')
                      << std::setw(8) << first_wfi_pc
                      << " LR=0x" << std::setw(8) << first_wfi_lr
                      << " SP=0x" << std::setw(8) << first_wfi_sp
                      << " CPSR=0x" << std::setw(8) << first_wfi_cpsr << "\n"
                      << "Percorso PC precedente al primo WFI:\n";
            for (size_t i = 0; i < first_wfi_history_count; ++i)
            {
                std::cout << "0x" << std::setw(8) << first_wfi_pc_history[i]
                          << ((i % 4u == 3u) ? "\n" : "  ");
            }
            std::cout << std::dec;
        }
    }

    return 0;
}