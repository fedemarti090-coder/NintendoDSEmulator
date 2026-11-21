#pragma once

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
#include <cstdint>

constexpr size_t NDS_MAIN_RAM_SIZE = 4u * 1024u * 1024u;
constexpr uint32_t NDS_MAIN_RAM_BASE = 0x02000000u;
constexpr uint32_t NDS_MAIN_RAM_MASK = 0x003FFFFFu;
constexpr uint32_t NDS_IO_BASE = 0x04000000u;
constexpr size_t NDS_IO_SIZE = 0x1000u;
constexpr uint32_t NDS_ITCM_BASE = 0x01FF8000u;
constexpr size_t NDS_ITCM_SIZE = 0x8000u;
constexpr uint32_t NDS_ITCM_BACKING_OFFSET = 0x003F8000u;
constexpr uint32_t NDS_SHARED_WRAM_BASE = 0x03000000u;
constexpr size_t NDS_SHARED_WRAM_SIZE = 32u * 1024u;
constexpr uint32_t NDS_ARM7_WRAM_BASE = 0x03800000u;
constexpr size_t NDS_ARM7_WRAM_SIZE = 64u * 1024u;
constexpr uint32_t NDS_SCANLINE_CYCLES = 2130u;
constexpr uint32_t NDS_SCANLINE_COUNT = 263u;
constexpr uint32_t NDS_VBLANK_LINE = 192u;
constexpr size_t NDS_MEMORY_TRACE_CAPACITY = 512u;
constexpr size_t NDS_IO_TRACE_CAPACITY = 256u;

struct MemoryAccessTrace
{
    uint32_t pc = 0u;
    uint32_t address = 0u;
    uint32_t value = 0u;
    uint8_t width = 0u;
    bool is_write = false;
};

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
    std::array<uint8_t, NDS_SHARED_WRAM_SIZE> shared_wram{};
    std::array<uint8_t, NDS_ARM7_WRAM_SIZE> arm7_wram{};
    std::array<uint8_t, NDS_IO_SIZE> io_regs{};
    std::array<uint8_t, NDS_IO_SIZE> arm7_io_regs{};
    std::array<uint8_t, 2> ipcsync_data{};
    mutable std::array<std::deque<uint32_t>, 2> ipc_fifo_received{};
    bool dma3_active = false;
    uint64_t video_cycles = 0u;
    mutable std::array<MemoryAccessTrace, NDS_MEMORY_TRACE_CAPACITY> memory_trace{};
    mutable size_t memory_trace_next = 0u;
    mutable size_t memory_trace_count = 0u;
    mutable std::array<MemoryAccessTrace, NDS_IO_TRACE_CAPACITY> startup_io_trace{};
    mutable size_t startup_io_trace_next = 0u;
    mutable size_t startup_io_trace_count = 0u;
    mutable uint8_t memory_trace_suppression = 0u;
    uint32_t trace_pc = 0u;
    bool trace_instruction_active = false;
    bool trace_irq_handler_active = false;
    bool startup_io_trace_active = true;
    uint8_t active_cpu = 0u;

    NdsMemory() : main_ram(NDS_MAIN_RAM_SIZE, 0) {}

    std::array<uint8_t, NDS_IO_SIZE> &io_bank()
    {
        return active_cpu == 0u ? io_regs : arm7_io_regs;
    }

    const std::array<uint8_t, NDS_IO_SIZE> &io_bank() const
    {
        return active_cpu == 0u ? io_regs : arm7_io_regs;
    }

    std::array<uint8_t, NDS_IO_SIZE> &io_bank(uint8_t cpu)
    {
        return cpu == 0u ? io_regs : arm7_io_regs;
    }

    uint32_t ipc_fifo_control_value() const
    {
        uint32_t value = 0u;
        const auto &bank = io_bank();
        for (size_t i = 0u; i < 4u; ++i)
            value |= static_cast<uint32_t>(bank[0x184u + i]) << (i * 8u);

        value &= ~((1u << 0) | (1u << 1) | (1u << 8) | (1u << 9));
        if (ipc_fifo_received[active_cpu].empty())
            value |= 1u << 8;
        if (ipc_fifo_received[active_cpu].size() >= 16u)
            value |= 1u << 9;
        if (ipc_fifo_received[1u - active_cpu].empty())
            value |= 1u;
        if (ipc_fifo_received[1u - active_cpu].size() >= 16u)
            value |= 1u << 1;
        return value;
    }

    void begin_irq_handler_trace()
    {
        memory_trace_next = 0u;
        memory_trace_count = 0u;
        trace_irq_handler_active = true;
    }

    void end_irq_handler_trace()
    {
        trace_irq_handler_active = false;
    }

    void end_startup_io_trace()
    {
        startup_io_trace_active = false;
    }

    void begin_instruction_trace(uint32_t pc)
    {
        trace_pc = pc;
        trace_instruction_active = true;
    }

    void end_instruction_trace()
    {
        trace_instruction_active = false;
    }

    size_t memory_trace_size() const
    {
        return memory_trace_count;
    }

    const MemoryAccessTrace &memory_trace_at(size_t index) const
    {
        size_t first = (memory_trace_next + memory_trace.size() - memory_trace_count) % memory_trace.size();
        return memory_trace[(first + index) % memory_trace.size()];
    }

    size_t startup_io_trace_size() const
    {
        return startup_io_trace_count;
    }

    const MemoryAccessTrace &startup_io_trace_at(size_t index) const
    {
        size_t first = (startup_io_trace_next + startup_io_trace.size() - startup_io_trace_count) % startup_io_trace.size();
        return startup_io_trace[(first + index) % startup_io_trace.size()];
    }

    void record_memory_access(uint32_t addr, uint32_t value, uint8_t width, bool is_write) const
    {
        if (!trace_instruction_active || memory_trace_suppression != 0u)
            return;

        bool is_main_ram = addr >= NDS_MAIN_RAM_BASE && addr < 0x03000000u;
        bool is_wram = (addr >= NDS_SHARED_WRAM_BASE && addr < NDS_SHARED_WRAM_BASE + NDS_SHARED_WRAM_SIZE) ||
                       (addr >= NDS_ARM7_WRAM_BASE && addr < NDS_ARM7_WRAM_BASE + NDS_ARM7_WRAM_SIZE);
        bool is_io = addr >= NDS_IO_BASE && static_cast<size_t>(addr - NDS_IO_BASE) < NDS_IO_SIZE;
        bool is_ipc_fifo = addr == 0x04100000u;
        bool is_itcm = is_itcm_address(addr);
        if (!is_main_ram && !is_wram && !is_io && !is_ipc_fifo && !is_itcm)
            return;

        MemoryAccessTrace event{trace_pc, addr, value, width, is_write};
        if ((is_io || is_ipc_fifo) && startup_io_trace_active && active_cpu == 0u)
        {
            startup_io_trace[startup_io_trace_next] = event;
            startup_io_trace_next = (startup_io_trace_next + 1u) % startup_io_trace.size();
            startup_io_trace_count = std::min(startup_io_trace_count + 1u, startup_io_trace.size());
        }
        if (trace_irq_handler_active && active_cpu == 0u)
        {
            memory_trace[memory_trace_next] = event;
            memory_trace_next = (memory_trace_next + 1u) % memory_trace.size();
            memory_trace_count = std::min(memory_trace_count + 1u, memory_trace.size());
        }
    }

    static bool is_itcm_address(uint32_t addr)
    {
        return addr < NDS_ITCM_SIZE ||
               (addr >= NDS_ITCM_BASE && addr < NDS_ITCM_BASE + NDS_ITCM_SIZE) ||
               (addr >= 0xFFFF0000u && addr < 0xFFFF0000u + NDS_ITCM_SIZE);
    }

    static uint32_t itcm_offset(uint32_t addr)
    {
        if (addr < NDS_ITCM_SIZE)
        {
            return addr;
        }
        if (addr >= 0xFFFF0000u)
        {
            return addr - 0xFFFF0000u;
        }
        return addr - NDS_ITCM_BASE;
    }

    uint8_t read_main_ram_u8(uint32_t addr) const
    {
        uint8_t value = 0u;
        if (is_itcm_address(addr))
        {
            uint32_t offset = NDS_ITCM_BACKING_OFFSET + itcm_offset(addr);
            value = main_ram[offset & NDS_MAIN_RAM_MASK];
        }
        else if (addr >= NDS_SHARED_WRAM_BASE && addr < NDS_SHARED_WRAM_BASE + NDS_SHARED_WRAM_SIZE)
        {
            value = shared_wram[addr - NDS_SHARED_WRAM_BASE];
        }
        else if (addr >= NDS_ARM7_WRAM_BASE && addr < NDS_ARM7_WRAM_BASE + NDS_ARM7_WRAM_SIZE)
        {
            value = arm7_wram[addr - NDS_ARM7_WRAM_BASE];
        }
        else if (addr == 0x04000180u)
        {
            value = ipcsync_data[1u - active_cpu];
        }
        else if (addr >= 0x04000184u && addr <= 0x04000187u)
        {
            value = static_cast<uint8_t>(ipc_fifo_control_value() >> ((addr - 0x04000184u) * 8u));
        }
        else if (addr >= NDS_IO_BASE && static_cast<size_t>(addr - NDS_IO_BASE) < io_regs.size())
        {
            value = io_bank()[addr - NDS_IO_BASE];
        }
        else if (addr == 0x04100000u && !ipc_fifo_received[active_cpu].empty())
        {
            value = static_cast<uint8_t>(ipc_fifo_received[active_cpu].front() & 0xFFu);
        }
        else
        {
            value = main_ram[addr & NDS_MAIN_RAM_MASK];
        }
        record_memory_access(addr, value, 1u, false);
        return value;
    }

    void write_main_ram_u8(uint32_t addr, uint8_t val)
    {
        if (is_itcm_address(addr))
        {
            uint32_t offset = NDS_ITCM_BACKING_OFFSET + itcm_offset(addr);
            main_ram[offset & NDS_MAIN_RAM_MASK] = val;
            record_memory_access(addr, val, 1u, true);
            return;
        }
        if (addr >= NDS_SHARED_WRAM_BASE && addr < NDS_SHARED_WRAM_BASE + NDS_SHARED_WRAM_SIZE)
        {
            shared_wram[addr - NDS_SHARED_WRAM_BASE] = val;
            record_memory_access(addr, val, 1u, true);
            return;
        }
        if (addr >= NDS_ARM7_WRAM_BASE && addr < NDS_ARM7_WRAM_BASE + NDS_ARM7_WRAM_SIZE)
        {
            arm7_wram[addr - NDS_ARM7_WRAM_BASE] = val;
            record_memory_access(addr, val, 1u, true);
            return;
        }
        if (addr >= NDS_IO_BASE && static_cast<size_t>(addr - NDS_IO_BASE) < io_regs.size())
        {
            auto &bank = io_bank();
            if (addr == 0x04000181u)
            {
                ipcsync_data[active_cpu] = val;
                bank[addr - NDS_IO_BASE] = val;
                uint8_t peer = static_cast<uint8_t>(1u - active_cpu);
                if ((val & 0x40u) != 0u && (io_bank(peer)[0x181u] & 0x20u) != 0u)
                    set_interrupt_flags_for(peer, 1u << 16);
                record_memory_access(addr, val, 1u, true);
                return;
            }
            if (addr == 0x04000180u)
            {
                record_memory_access(addr, val, 1u, true);
                return;
            }
            if (addr == 0x04000184u)
            {
                if ((val & 0x08u) != 0u)
                    ipc_fifo_received[1u - active_cpu].clear();
                bank[0x184u] = static_cast<uint8_t>((bank[0x184u] & 0x03u) | (val & 0x04u));
                record_memory_access(addr, val, 1u, true);
                return;
            }
            if (addr == 0x04000185u)
            {
                uint8_t error = (bank[0x185u] & 0x40u) != 0u && (val & 0x40u) == 0u ? 0x40u : 0u;
                bank[0x185u] = static_cast<uint8_t>((bank[0x185u] & 0x03u) |
                                                     (val & 0x84u) | error);
                record_memory_access(addr, val, 1u, true);
                return;
            }
            if (addr >= 0x04000214u && addr <= 0x04000217u)
            {
                bank[addr - NDS_IO_BASE] &= static_cast<uint8_t>(~val);
                record_memory_access(addr, val, 1u, true);
                return;
            }
            if (addr == 0x04000184u && (val & 0x08u) != 0u)
                ipc_fifo_received[1u - active_cpu].clear();
            if (addr == 0x04000006u || addr == 0x04000007u)
            {
                record_memory_access(addr, val, 1u, true);
                return;
            }
            if (addr == 0x04000004u)
            {
                bank[addr - NDS_IO_BASE] =
                    static_cast<uint8_t>((bank[addr - NDS_IO_BASE] & 0x07u) | (val & 0xF8u));
                record_memory_access(addr, val, 1u, true);
                return;
            }

            bank[addr - NDS_IO_BASE] = val;
            if (addr == 0x040000DFu && !dma3_active)
            {
                execute_dma3();
            }
            record_memory_access(addr, val, 1u, true);
            return;
        }
        main_ram[addr & NDS_MAIN_RAM_MASK] = val;
        record_memory_access(addr, val, 1u, true);
    }

    uint32_t read_main_ram_u32_le(uint32_t addr) const
    {
        if (addr == 0x04100000u)
        {
            uint32_t value = 0u;
            auto &queue = ipc_fifo_received[active_cpu];
            if (!queue.empty())
            {
                value = queue.front();
                queue.pop_front();
            }
            record_memory_access(addr, value, 4u, false);
            return value;
        }

        ++memory_trace_suppression;
        uint32_t val = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            val |= static_cast<uint32_t>(read_main_ram_u8(addr + static_cast<uint32_t>(i))) << (i * 8);
        }
        --memory_trace_suppression;
        record_memory_access(addr, val, 4u, false);
        return val;
    }

    void write_main_ram_u32_le(uint32_t addr, uint32_t value)
    {
        if (addr == 0x04000188u)
        {
            uint8_t peer = static_cast<uint8_t>(1u - active_cpu);
            bool queued = false;
            if ((io_bank()[0x185u] & 0x80u) != 0u)
            {
                auto &queue = ipc_fifo_received[peer];
                if (queue.size() < 16u)
                {
                    queue.push_back(value);
                    queued = true;
                }
                else
                {
                    io_bank()[0x185u] |= 0x40u;
                }
            }
            if (queued && (io_bank(peer)[0x185u] & 0x04u) != 0u)
                set_interrupt_flags_for(peer, 1u << 17);
            record_memory_access(addr, value, 4u, true);
            return;
        }

        ++memory_trace_suppression;
        write_main_ram_u8(addr + 0u, static_cast<uint8_t>(value & 0xFFu));
        write_main_ram_u8(addr + 1u, static_cast<uint8_t>((value >> 8) & 0xFFu));
        write_main_ram_u8(addr + 2u, static_cast<uint8_t>((value >> 16) & 0xFFu));
        write_main_ram_u8(addr + 3u, static_cast<uint8_t>((value >> 24) & 0xFFu));
        --memory_trace_suppression;
        record_memory_access(addr, value, 4u, true);
    }

    void execute_dma3()
    {
        constexpr uint32_t source_reg = 0x040000D4u;
        constexpr uint32_t destination_reg = 0x040000D8u;
        constexpr uint32_t control_reg = 0x040000DCu;

        uint32_t control = read_main_ram_u32_le(control_reg);
        if ((control & 0x80000000u) == 0u || ((control >> 28) & 0x3u) != 0u)
        {
            return;
        }

        uint32_t count = control & 0x001FFFFFu;
        if (count == 0u)
        {
            count = 0x00200000u;
        }

        uint32_t source = read_main_ram_u32_le(source_reg);
        uint32_t destination = read_main_ram_u32_le(destination_reg);
        uint32_t transfer_size = (control & (1u << 26)) ? 4u : 2u;
        uint32_t source_mode = (control >> 23) & 0x3u;
        uint32_t destination_mode = (control >> 21) & 0x3u;

        dma3_active = true;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t value = transfer_size == 4u
                                 ? read_main_ram_u32_le(source)
                                 : static_cast<uint32_t>(read_main_ram_u8(source)) |
                                       (static_cast<uint32_t>(read_main_ram_u8(source + 1u)) << 8);

            if (transfer_size == 4u)
            {
                write_main_ram_u32_le(destination, value);
            }
            else
            {
                write_main_ram_u8(destination, static_cast<uint8_t>(value & 0xFFu));
                write_main_ram_u8(destination + 1u, static_cast<uint8_t>((value >> 8) & 0xFFu));
            }

            if (source_mode == 0u)
            {
                source += transfer_size;
            }
            else if (source_mode == 1u)
            {
                source -= transfer_size;
            }

            if (destination_mode == 0u || destination_mode == 3u)
            {
                destination += transfer_size;
            }
            else if (destination_mode == 1u)
            {
                destination -= transfer_size;
            }
        }
        dma3_active = false;

        auto store_io_u32 = [this](uint32_t addr, uint32_t value)
        {
            size_t offset = static_cast<size_t>(addr - NDS_IO_BASE);
            auto &bank = io_bank();
            for (size_t i = 0; i < 4; ++i)
            {
                bank[offset + i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFFu);
            }
        };
        store_io_u32(source_reg, source);
        store_io_u32(destination_reg, destination);
        store_io_u32(control_reg, control & ~0x80000000u);
    }

    void set_interrupt_flags(uint32_t flags)
    {
        set_interrupt_flags_for(active_cpu, flags);
    }

    void set_interrupt_flags_for(uint8_t cpu, uint32_t flags)
    {
        size_t offset = 0x214u;
        auto &bank = io_bank(cpu);
        for (size_t i = 0; i < 4; ++i)
        {
            bank[offset + i] |= static_cast<uint8_t>((flags >> (i * 8)) & 0xFFu);
        }
    }

    bool interrupt_pending() const
    {
        return (read_main_ram_u32_le(0x04000208u) & 1u) != 0u &&
               (read_main_ram_u32_le(0x04000210u) & read_main_ram_u32_le(0x04000214u)) != 0u;
    }

    void update_video_status()
    {
        uint32_t frame_cycles = NDS_SCANLINE_CYCLES * NDS_SCANLINE_COUNT;
        uint32_t frame_position = static_cast<uint32_t>(video_cycles % frame_cycles);
        uint32_t line = frame_position / NDS_SCANLINE_CYCLES;
        uint32_t line_cycle = frame_position % NDS_SCANLINE_CYCLES;
        uint16_t status = 0u;

        if (line >= NDS_VBLANK_LINE)
            status |= 1u;
        if (line_cycle >= 1600u)
            status |= 2u;

        for (uint8_t cpu = 0u; cpu < 2u; ++cpu)
        {
            auto &bank = io_bank(cpu);
            uint16_t dispstat = static_cast<uint16_t>(bank[4] | (static_cast<uint16_t>(bank[5]) << 8));
            uint32_t compare = (dispstat >> 8) & 0xFFu;
            uint16_t cpu_status = status;
            if (line == compare)
                cpu_status |= 4u;
            dispstat = static_cast<uint16_t>((dispstat & ~0x7u) | cpu_status);
            bank[4] = static_cast<uint8_t>(dispstat & 0xFFu);
            bank[5] = static_cast<uint8_t>(dispstat >> 8);
            bank[6] = static_cast<uint8_t>(line & 0xFFu);
            bank[7] = static_cast<uint8_t>((line >> 8) & 0xFFu);
        }
    }

    void advance_cpu_cycles(uint32_t cycles)
    {
        uint32_t frame_cycles = NDS_SCANLINE_CYCLES * NDS_SCANLINE_COUNT;
        for (uint32_t i = 0; i < cycles; ++i)
        {
            uint32_t previous_position = static_cast<uint32_t>(video_cycles % frame_cycles);
            uint32_t previous_line = previous_position / NDS_SCANLINE_CYCLES;
            uint32_t previous_line_cycle = previous_position % NDS_SCANLINE_CYCLES;
            ++video_cycles;

            uint32_t current_position = static_cast<uint32_t>(video_cycles % frame_cycles);
            uint32_t current_line = current_position / NDS_SCANLINE_CYCLES;
            uint32_t current_line_cycle = current_position % NDS_SCANLINE_CYCLES;
            for (uint8_t cpu = 0u; cpu < 2u; ++cpu)
            {
                const auto &bank = io_bank(cpu);
                uint16_t dispstat = static_cast<uint16_t>(bank[4] | (static_cast<uint16_t>(bank[5]) << 8));
                if (previous_line_cycle < 1600u && current_line_cycle >= 1600u && (dispstat & (1u << 4)))
                    set_interrupt_flags_for(cpu, 1u << 1);
                if (current_line != previous_line)
                {
                    uint32_t compare = (dispstat >> 8) & 0xFFu;
                    if (current_line == NDS_VBLANK_LINE && (dispstat & (1u << 3)))
                        set_interrupt_flags_for(cpu, 1u);
                    if (current_line == compare && (dispstat & (1u << 5)))
                        set_interrupt_flags_for(cpu, 1u << 2);
                }
            }
            update_video_status();
        }
    }
};

struct CpuState
{
    uint32_t r[16]{};
    std::array<uint32_t, 6> banked_r13{};
    std::array<uint32_t, 6> banked_r14{};
    std::array<uint32_t, 5> spsr{};
    uint32_t cpsr = 0x1F;
    bool waiting_for_interrupt = false;
    uint32_t last_wfi_pc = 0u;
    uint32_t last_wfi_lr = 0u;
    uint32_t last_wfi_sp = 0u;
    uint32_t last_wfi_cpsr = 0u;
    uint64_t wfi_count = 0u;
    uint8_t cpu_id = 0u;
};