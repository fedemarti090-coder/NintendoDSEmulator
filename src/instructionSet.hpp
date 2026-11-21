#pragma once

#include "emuStruct.hpp"

#include <bit>
#include <cstdint>
#include <iomanip>
#include <iostream>

inline bool cond_passed(uint32_t cond, uint32_t cpsr)
{
    bool n = ((cpsr >> 31) & 1u) != 0u;
    bool z = ((cpsr >> 30) & 1u) != 0u;
    bool c = ((cpsr >> 29) & 1u) != 0u;
    bool v = ((cpsr >> 28) & 1u) != 0u;

    switch (cond & 0xFu)
    {
    case 0x0u:
        return z; // EQ
    case 0x1u:
        return !z; // NE
    case 0x2u:
        return c; // CS / HS
    case 0x3u:
        return !c; // CC / LO
    case 0x4u:
        return n; // MI
    case 0x5u:
        return !n; // PL
    case 0x6u:
        return v; // VS
    case 0x7u:
        return !v; // VC
    case 0x8u:
        return c && !z; // HI
    case 0x9u:
        return !c || z; // LS
    case 0xAu:
        return n == v; // GE
    case 0xBu:
        return n != v; // LT
    case 0xCu:
        return !z && (n == v); // GT
    case 0xDu:
        return z || (n != v); // LE
    case 0xEu:
        return true; // AL
    default:
        return false; // 0xF (riservato)
    }
}

inline uint32_t *saved_status_register(CpuState &s)
{
    switch (s.cpsr & 0x1Fu)
    {
    case 0x11u:
        return &s.spsr[0]; // FIQ
    case 0x12u:
        return &s.spsr[1]; // IRQ
    case 0x13u:
        return &s.spsr[2]; // Supervisor
    case 0x17u:
        return &s.spsr[3]; // Abort
    case 0x1Bu:
        return &s.spsr[4]; // Undefined
    default:
        return nullptr;
    }
}

inline int register_bank_index(uint32_t mode)
{
    switch (mode & 0x1Fu)
    {
    case 0x10u:
    case 0x1Fu:
        return 0; // User / System
    case 0x11u:
        return 1; // FIQ
    case 0x12u:
        return 2; // IRQ
    case 0x13u:
        return 3; // Supervisor
    case 0x17u:
        return 4; // Abort
    case 0x1Bu:
        return 5; // Undefined
    default:
        return -1;
    }
}

inline void write_cpsr(CpuState &s, uint32_t value)
{
    int current_bank = register_bank_index(s.cpsr);
    int next_bank = register_bank_index(value);
    if (current_bank >= 0 && next_bank >= 0 && current_bank != next_bank)
    {
        s.banked_r13[static_cast<size_t>(current_bank)] = s.r[13];
        s.banked_r14[static_cast<size_t>(current_bank)] = s.r[14];
        s.r[13] = s.banked_r13[static_cast<size_t>(next_bank)];
        s.r[14] = s.banked_r14[static_cast<size_t>(next_bank)];
    }
    s.cpsr = value;
}

inline uint32_t read_user_mode_register(const CpuState &s, uint32_t reg)
{
    int current_bank = register_bank_index(s.cpsr);
    if (reg == 13u && current_bank != 0)
        return s.banked_r13[0];
    if (reg == 14u && current_bank != 0)
        return s.banked_r14[0];
    return s.r[reg];
}

inline void write_user_mode_register(CpuState &s, uint32_t reg, uint32_t value)
{
    int current_bank = register_bank_index(s.cpsr);
    if (reg == 13u && current_bank != 0)
    {
        s.banked_r13[0] = value;
        return;
    }
    if (reg == 14u && current_bank != 0)
    {
        s.banked_r14[0] = value;
        return;
    }
    s.r[reg] = value;
}

inline void enter_irq(CpuState &s)
{
    uint32_t return_address = s.r[15] + 4u;
    s.spsr[1] = s.cpsr;
    write_cpsr(s, (s.cpsr & ~0x3Fu) | 0x12u | (1u << 7));
    s.r[14] = return_address;
    s.r[15] = 0x00000018u;
    s.waiting_for_interrupt = false;
}

struct InstructionTraceScope
{
    NdsMemory &memory;

    InstructionTraceScope(NdsMemory &mem, uint32_t instruction_pc) : memory(mem)
    {
        memory.begin_instruction_trace(instruction_pc);
    }

    ~InstructionTraceScope()
    {
        memory.end_instruction_trace();
    }
};

inline bool execute_thumb_instruction(CpuState &s, NdsMemory &mem, uint32_t pc, uint16_t instr)
{
    if ((instr & 0xE000u) == 0u && ((instr >> 11) & 0x3u) != 0x3u)
    {
        uint32_t opcode = (instr >> 11) & 0x3u;
        uint32_t amount = (instr >> 6) & 0x1Fu;
        uint32_t rs = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t value = s.r[rs];
        uint32_t result = value;
        uint32_t carry = (s.cpsr >> 29) & 1u;

        if (opcode == 0u)
        {
            if (amount != 0u)
            {
                carry = (value >> (32u - amount)) & 1u;
                result = value << amount;
            }
        }
        else if (opcode == 1u)
        {
            amount = amount == 0u ? 32u : amount;
            carry = (value >> (amount - 1u)) & 1u;
            result = amount == 32u ? 0u : value >> amount;
        }
        else
        {
            amount = amount == 0u ? 32u : amount;
            carry = (value >> (amount - 1u)) & 1u;
            result = amount == 32u
                         ? ((value & 0x80000000u) != 0u ? 0xFFFFFFFFu : 0u)
                         : static_cast<uint32_t>(static_cast<int32_t>(value) >> amount);
        }
        s.r[rd] = result;
        s.cpsr = (s.cpsr & ~0xE0000000u) |
                 ((result & 0x80000000u) ? (1u << 31) : 0u) |
                 (result == 0u ? (1u << 30) : 0u) |
                 (carry << 29);
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xF800u) == 0x1800u)
    {
        bool immediate = (instr & 0x0400u) != 0u;
        bool subtract = (instr & 0x0200u) != 0u;
        uint32_t operand = (instr >> 6) & 0x7u;
        uint32_t rs = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t left = s.r[rs];
        uint32_t right = immediate ? operand : s.r[operand];
        uint32_t result = subtract ? left - right : left + right;
        uint32_t carry = subtract ? (left >= right ? 1u : 0u)
                                  : (result < left ? 0u : 1u);
        uint32_t overflow = subtract
                                ? (((left ^ right) & (left ^ result)) >> 31) & 1u
                                : ((~(left ^ right) & (left ^ result)) >> 31) & 1u;
        s.r[rd] = result;
        s.cpsr = (s.cpsr & ~0xF0000000u) |
                 ((result & 0x80000000u) ? (1u << 31) : 0u) |
                 (result == 0u ? (1u << 30) : 0u) |
                 (carry << 29) | (overflow << 28);
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xFE00u) == 0xB400u)
    {
        uint32_t reg_list = instr & 0xFFu;
        bool include_lr = (instr & 0x0100u) != 0u;
        uint32_t count = std::popcount(reg_list) + (include_lr ? 1u : 0u);
        uint32_t address = s.r[13] - count * 4u;
        uint32_t write_address = address;
        for (uint32_t reg = 0u; reg < 8u; ++reg)
        {
            if ((reg_list & (1u << reg)) != 0u)
            {
                mem.write_main_ram_u32_le(write_address, s.r[reg]);
                write_address += 4u;
            }
        }
        if (include_lr)
            mem.write_main_ram_u32_le(write_address, s.r[14]);
        s.r[13] = address;
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xFE00u) == 0xBC00u)
    {
        uint32_t reg_list = instr & 0xFFu;
        bool include_pc = (instr & 0x0100u) != 0u;
        uint32_t address = s.r[13];
        for (uint32_t reg = 0u; reg < 8u; ++reg)
        {
            if ((reg_list & (1u << reg)) != 0u)
            {
                s.r[reg] = mem.read_main_ram_u32_le(address);
                address += 4u;
            }
        }
        if (include_pc)
        {
            uint32_t target = mem.read_main_ram_u32_le(address);
            address += 4u;
            s.r[15] = target & ~1u;
        }
        else
        {
            s.r[15] = pc + 2u;
        }
        s.r[13] = address;
        return true;
    }

    if ((instr & 0xFC00u) == 0x4000u)
    {
        uint32_t opcode = (instr >> 6) & 0xFu;
        uint32_t rs = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        if (opcode == 0xAu)
        {
            uint32_t left = s.r[rd];
            uint32_t right = s.r[rs];
            uint32_t result = left - right;
            uint32_t carry = left >= right ? 1u : 0u;
            uint32_t overflow = (((left ^ right) & (left ^ result)) >> 31) & 1u;
            s.cpsr = (s.cpsr & ~0xF0000000u) |
                     ((result & 0x80000000u) ? (1u << 31) : 0u) |
                     (result == 0u ? (1u << 30) : 0u) |
                     (carry << 29) | (overflow << 28);
            s.r[15] = pc + 2u;
            return true;
        }
    }

    if ((instr & 0xE000u) == 0x2000u)
    {
        uint32_t opcode = (instr >> 11) & 0x3u;
        uint32_t rd = (instr >> 8) & 0x7u;
        uint32_t immediate = instr & 0xFFu;
        uint32_t left = s.r[rd];
        uint32_t result = immediate;

        if (opcode == 0u)
        {
            s.r[rd] = result;
            s.cpsr = (s.cpsr & ~0xC0000000u) |
                     ((result & 0x80000000u) ? (1u << 31) : 0u) |
                     (result == 0u ? (1u << 30) : 0u);
        }
        else if (opcode == 1u)
        {
            result = left - immediate;
            uint32_t carry = left >= immediate ? 1u : 0u;
            uint32_t overflow = (((left ^ immediate) & (left ^ result)) >> 31) & 1u;
            s.cpsr = (s.cpsr & ~0xF0000000u) |
                     ((result & 0x80000000u) ? (1u << 31) : 0u) |
                     (result == 0u ? (1u << 30) : 0u) |
                     (carry << 29) | (overflow << 28);
        }
        else
        {
            bool subtract = opcode == 3u;
            result = subtract ? left - immediate : left + immediate;
            uint32_t carry = subtract ? (left >= immediate ? 1u : 0u)
                                      : (result < left ? 0u : 1u);
            uint32_t overflow = subtract
                                    ? (((left ^ immediate) & (left ^ result)) >> 31) & 1u
                                    : ((~(left ^ immediate) & (left ^ result)) >> 31) & 1u;
            s.r[rd] = result;
            s.cpsr = (s.cpsr & ~0xF0000000u) |
                     ((result & 0x80000000u) ? (1u << 31) : 0u) |
                     (result == 0u ? (1u << 30) : 0u) |
                     (carry << 29) | (overflow << 28);
        }
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xF800u) == 0x4800u)
    {
        uint32_t rd = (instr >> 8) & 0x7u;
        uint32_t address = ((pc + 4u) & ~3u) + ((instr & 0xFFu) << 2);
        s.r[rd] = mem.read_main_ram_u32_le(address);
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xF000u) == 0x8000u)
    {
        bool is_load = (instr & 0x0800u) != 0u;
        uint32_t offset = ((instr >> 6) & 0x1Fu) << 1;
        uint32_t rb = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t address = s.r[rb] + offset;
        if (is_load)
        {
            s.r[rd] = static_cast<uint32_t>(mem.read_main_ram_u8(address)) |
                      (static_cast<uint32_t>(mem.read_main_ram_u8(address + 1u)) << 8);
        }
        else
        {
            mem.write_main_ram_u8(address, static_cast<uint8_t>(s.r[rd] & 0xFFu));
            mem.write_main_ram_u8(address + 1u, static_cast<uint8_t>((s.r[rd] >> 8) & 0xFFu));
        }
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xE000u) == 0x6000u)
    {
        uint32_t opcode = (instr >> 11) & 0x3u;
        bool is_byte = (opcode & 0x2u) != 0u;
        bool is_load = (opcode & 0x1u) != 0u;
        uint32_t offset = (instr >> 6) & 0x1Fu;
        uint32_t rb = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t address = s.r[rb] + (is_byte ? offset : offset * 4u);
        if (is_load)
        {
            s.r[rd] = is_byte ? static_cast<uint32_t>(mem.read_main_ram_u8(address))
                              : mem.read_main_ram_u32_le(address);
        }
        else if (is_byte)
        {
            mem.write_main_ram_u8(address, static_cast<uint8_t>(s.r[rd] & 0xFFu));
        }
        else
        {
            mem.write_main_ram_u32_le(address, s.r[rd]);
        }
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xF000u) == 0x5000u)
    {
        uint32_t opcode = (instr >> 9) & 0x7u;
        uint32_t rm = (instr >> 6) & 0x7u;
        uint32_t rb = (instr >> 3) & 0x7u;
        uint32_t rd = instr & 0x7u;
        uint32_t address = s.r[rb] + s.r[rm];
        uint32_t value = 0u;

        switch (opcode)
        {
        case 0u:
            mem.write_main_ram_u32_le(address, s.r[rd]);
            break;
        case 1u:
            mem.write_main_ram_u8(address, static_cast<uint8_t>(s.r[rd] & 0xFFu));
            mem.write_main_ram_u8(address + 1u, static_cast<uint8_t>((s.r[rd] >> 8) & 0xFFu));
            break;
        case 2u:
            mem.write_main_ram_u8(address, static_cast<uint8_t>(s.r[rd] & 0xFFu));
            break;
        case 3u:
            s.r[rd] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(mem.read_main_ram_u8(address))));
            break;
        case 4u:
            s.r[rd] = mem.read_main_ram_u32_le(address);
            break;
        case 5u:
            value = static_cast<uint32_t>(mem.read_main_ram_u8(address)) |
                    (static_cast<uint32_t>(mem.read_main_ram_u8(address + 1u)) << 8);
            s.r[rd] = value;
            break;
        case 6u:
            s.r[rd] = static_cast<uint32_t>(mem.read_main_ram_u8(address));
            break;
        case 7u:
            value = static_cast<uint32_t>(mem.read_main_ram_u8(address)) |
                    (static_cast<uint32_t>(mem.read_main_ram_u8(address + 1u)) << 8);
            s.r[rd] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(value)));
            break;
        }
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xF000u) == 0x9000u)
    {
        bool is_load = (instr & 0x0800u) != 0u;
        uint32_t rd = (instr >> 8) & 0x7u;
        uint32_t address = s.r[13] + ((instr & 0xFFu) << 2);
        if (is_load)
            s.r[rd] = mem.read_main_ram_u32_le(address);
        else
            mem.write_main_ram_u32_le(address, s.r[rd]);
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xF000u) == 0xD000u)
    {
        uint32_t condition = (instr >> 8) & 0xFu;
        if (condition < 0xEu && cond_passed(condition, s.cpsr))
        {
            int32_t offset = static_cast<int8_t>(instr & 0xFFu);
            s.r[15] = static_cast<uint32_t>(static_cast<int64_t>(pc) + 4ll + offset * 2ll);
        }
        else
        {
            s.r[15] = pc + 2u;
        }
        return true;
    }

    if ((instr & 0xFC00u) == 0x4400u)
    {
        uint32_t opcode = (instr >> 8) & 0x3u;
        uint32_t rd = (instr & 0x7u) | ((instr >> 4) & 0x8u);
        uint32_t rs = (instr >> 3) & 0xFu;
        uint32_t left = rd == 15u ? ((pc + 4u) & ~3u) : s.r[rd];
        uint32_t right = rs == 15u ? ((pc + 4u) & ~3u) : s.r[rs];

        if (opcode == 0u)
        {
            uint32_t result = left + right;
            if (rd == 15u)
                s.r[15] = result & ~1u;
            else
            {
                s.r[rd] = result;
                s.r[15] = pc + 2u;
            }
        }
        else if (opcode == 1u)
        {
            uint32_t result = left - right;
            uint32_t carry = left >= right ? 1u : 0u;
            uint32_t overflow = (((left ^ right) & (left ^ result)) >> 31) & 1u;
            s.cpsr = (s.cpsr & ~0xF0000000u) |
                     ((result & 0x80000000u) ? (1u << 31) : 0u) |
                     (result == 0u ? (1u << 30) : 0u) |
                     (carry << 29) | (overflow << 28);
            s.r[15] = pc + 2u;
        }
        else if (opcode == 2u)
        {
            if (rd == 15u)
                s.r[15] = right & ~1u;
            else
            {
                s.r[rd] = right;
                s.r[15] = pc + 2u;
            }
        }
        else
        {
            uint32_t target = right;
            bool is_blx = ((instr >> 7) & 1u) != 0u;
            if (is_blx)
            {
                s.r[14] = (pc + 2u) | 1u;
                s.cpsr &= ~(1u << 5);
                s.r[15] = target & ~3u;
            }
            else if ((target & 1u) != 0u)
            {
                s.cpsr |= 1u << 5;
                s.r[15] = target & ~1u;
            }
            else
            {
                s.cpsr &= ~(1u << 5);
                s.r[15] = target & ~3u;
            }
        }
        return true;
    }

    if ((instr & 0xF800u) == 0xE000u)
    {
        int32_t offset = static_cast<int32_t>(instr & 0x7FFu);
        if (offset & 0x400)
            offset |= ~0x7FF;
        s.r[15] = static_cast<uint32_t>(static_cast<int64_t>(pc) + 4ll + offset * 2ll);
        return true;
    }

    if ((instr & 0xF800u) == 0xF000u)
    {
        int32_t offset = static_cast<int32_t>(instr & 0x7FFu);
        if (offset & 0x400)
            offset |= ~0x7FF;
        s.r[14] = static_cast<uint32_t>(static_cast<int64_t>(pc + 4u) +
                                        (static_cast<int64_t>(offset) * 4096ll));
        s.r[15] = pc + 2u;
        return true;
    }

    if ((instr & 0xF800u) == 0xF800u)
    {
        uint32_t target = s.r[14] + ((instr & 0x7FFu) << 1);
        s.r[14] = (pc + 2u) | 1u;
        s.r[15] = target & ~1u;
        return true;
    }

    std::cout << std::hex << std::uppercase << std::setfill('0')
              << "Istruzione Thumb non gestita: PC=0x" << std::setw(8) << pc
              << " Instr=0x" << std::setw(4) << instr << std::dec << "\n";
    return false;
}

inline bool step_once(CpuState &s, NdsMemory &mem, bool advance_cycles = true)
{
    // Approximate timing until instruction-specific cycle costs are modeled.
    mem.active_cpu = s.cpu_id;
    if (advance_cycles)
        mem.advance_cpu_cycles(1u);

    if (s.waiting_for_interrupt)
    {
        if (!mem.interrupt_pending())
        {
            return true;
        }
        s.waiting_for_interrupt = false;
    }

    if (mem.interrupt_pending() && ((s.cpsr & (1u << 7)) == 0u))
    {
        enter_irq(s);
        if (s.cpu_id == 0u)
            mem.begin_irq_handler_trace();
        return true;
    }

    uint32_t pc = s.r[15];
    if ((s.cpsr & (1u << 5)) != 0u)
    {
        uint16_t thumb_instr = static_cast<uint16_t>(mem.read_main_ram_u8(pc)) |
                               (static_cast<uint16_t>(mem.read_main_ram_u8(pc + 1u)) << 8);
        if (thumb_instr == 0u)
            return false;
        InstructionTraceScope trace_scope(mem, pc);
        return execute_thumb_instruction(s, mem, pc, thumb_instr);
    }

    uint32_t instr = mem.read_main_ram_u32_le(pc);

    // Stop immediato su word nulla (zona vuota / non mappata)
    if (instr == 0x00000000u)
    {
        return false;
    }

    InstructionTraceScope trace_scope(mem, pc);

    uint32_t cond = (instr >> 28) & 0xFu;
    if ((instr & 0xFE000000u) == 0xFA000000u)
    {
        uint32_t imm24 = instr & 0x00FFFFFFu;
        int32_t signed_imm24 = (imm24 & (1u << 23))
                                   ? static_cast<int32_t>(imm24 | 0xFF000000u)
                                   : static_cast<int32_t>(imm24);
        uint32_t high_offset = (instr >> 23) & 0x2u;
        s.r[14] = pc + 4u;
        s.cpsr |= 1u << 5;
        s.r[15] = static_cast<uint32_t>(
            static_cast<int64_t>(pc) + 8ll + static_cast<int64_t>(signed_imm24) * 4ll + high_offset);
        return true;
    }

    if (!cond_passed(cond, s.cpsr))
    {
        s.r[15] = pc + 4u;
        return true;
    }

    uint32_t group = (instr >> 25) & 0x7u;

    // Branch B / BL: bit 27..25 == 101 (bit 24: 0 = B, 1 = BL)
    if (group == 0b101u)
    {
        bool is_link = (((instr >> 24) & 0x1u) == 1u);
        uint32_t imm24 = instr & 0x00FFFFFFu;
        int32_t signed_imm24 = (imm24 & (1u << 23))
                                   ? static_cast<int32_t>(imm24 | 0xFF000000u)
                                   : static_cast<int32_t>(imm24);
        uint32_t next_pc = static_cast<uint32_t>(
            static_cast<int64_t>(pc) + 8ll + (static_cast<int64_t>(signed_imm24) * 4ll));

        if (is_link)
        {
            s.r[14] = pc + 4u;
        }

        s.r[15] = next_pc;
        return true;
    }

    // BX / BLX Rn: bit 27..8 == 0x12FFF, bit 7..6 == 00, bit 4 == 1 (bit 5: 0 = BX, 1 = BLX)
    if (((instr >> 8) & 0xFFFFFu) == 0x12FFFu &&
        (((instr >> 6) & 0x3u) == 0u) &&
        (((instr >> 4) & 0x1u) == 1u))
    {
        bool is_blx = (((instr >> 5) & 0x1u) == 1u);
        uint32_t rm = instr & 0xFu;
        uint32_t target = (rm == 15u) ? (pc + 8u) : s.r[rm];

        if (is_blx)
        {
            s.r[14] = (pc + 4u) & ~1u;
        }

        bool thumb_state = (target & 1u) != 0u;
        if (thumb_state)
        {
            s.cpsr |= (1u << 5);
            s.r[15] = target & ~1u;
        }
        else
        {
            s.cpsr &= ~(1u << 5);
            s.r[15] = target & ~3u;
        }

        return true;
    }

    // MRS Rd, CPSR / SPSR
    if ((instr & 0x0FBF0FFFu) == 0x010F0000u)
    {
        uint32_t rd = (instr >> 12) & 0xFu;
        bool use_spsr = (((instr >> 22) & 0x1u) != 0u);
        uint32_t *saved_status = use_spsr ? saved_status_register(s) : nullptr;

        if (!use_spsr || saved_status != nullptr)
        {
            s.r[rd] = use_spsr ? *saved_status : s.cpsr;
            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }
    }

    // MSR CPSR_<fields> / SPSR_<fields>, Rm
    if ((instr & 0x0FB0FFF0u) == 0x0120F000u)
    {
        uint32_t field_mask = (instr >> 16) & 0xFu;
        uint32_t rm = instr & 0xFu;
        uint32_t value = (rm == 15u) ? (pc + 8u) : s.r[rm];
        bool use_spsr = (((instr >> 22) & 0x1u) != 0u);
        uint32_t *saved_status = use_spsr ? saved_status_register(s) : nullptr;
        uint32_t mask = 0u;

        if (field_mask & 0x1u)
            mask |= 0x000000FFu;
        if (field_mask & 0x2u)
            mask |= 0x0000FF00u;
        if (field_mask & 0x4u)
            mask |= 0x00FF0000u;
        if (field_mask & 0x8u)
            mask |= 0xFF000000u;

        if (!use_spsr || saved_status != nullptr)
        {
            uint32_t status = use_spsr ? *saved_status : s.cpsr;
            status = (status & ~mask) | (value & mask);
            if (use_spsr)
                *saved_status = status;
            else
                write_cpsr(s, status);
            s.r[15] = pc + 4u;
            return true;
        }
    }

    // Count Leading Zeros (CLZ)
    if ((instr & 0x0FFF0FF0u) == 0x016F0F10u)
    {
        uint32_t rd = (instr >> 12) & 0xFu;
        uint32_t rm = instr & 0xFu;
        s.r[rd] = std::countl_zero(s.r[rm]);
        s.r[15] = pc + 4u;
        return true;
    }

    // MSR CPSR_<fields> / SPSR_<fields>, #imm
    if ((((instr >> 20) & 0xFBu) == 0x32u) && (((instr >> 12) & 0xFu) == 0xFu))
    {
        uint32_t imm8 = instr & 0xFFu;
        uint32_t rot = ((instr >> 8) & 0xFu) * 2u;
        uint32_t val = rot ? ((imm8 >> rot) | (imm8 << ((32u - rot) & 31u))) : imm8;
        uint32_t field_mask = (instr >> 16) & 0xFu;
        bool use_spsr = (((instr >> 22) & 0x1u) != 0u);
        uint32_t *saved_status = use_spsr ? saved_status_register(s) : nullptr;

        uint32_t mask = 0u;
        if (field_mask & 0x1u)
            mask |= 0x000000FFu; // c
        if (field_mask & 0x2u)
            mask |= 0x0000FF00u; // x
        if (field_mask & 0x4u)
            mask |= 0x00FF0000u; // s
        if (field_mask & 0x8u)
            mask |= 0xFF000000u; // f

        if (!use_spsr || saved_status != nullptr)
        {
            uint32_t status = use_spsr ? *saved_status : s.cpsr;
            status = (status & ~mask) | (val & mask);
            if (use_spsr)
                *saved_status = status;
            else
                write_cpsr(s, status);
            s.r[15] = pc + 4u;
            return true;
        }
    }

    // Single Data Transfer (LDR / STR / LDRB / STRB): bit 27..26 == 01
    if (((instr >> 26) & 0x3u) == 0b01u)
    {
        bool is_imm_offset = (((instr >> 25) & 0x1u) == 0u);
        bool pre_index = (((instr >> 24) & 0x1u) == 1u);
        bool add_offset = (((instr >> 23) & 0x1u) == 1u);
        bool is_byte = (((instr >> 22) & 0x1u) == 1u);
        bool write_back = (((instr >> 21) & 0x1u) == 1u);
        bool is_load = (((instr >> 20) & 0x1u) == 1u);

        uint32_t rn = (instr >> 16) & 0xFu;
        uint32_t rd = (instr >> 12) & 0xFu;

        uint32_t offset = 0u;
        if (is_imm_offset)
        {
            offset = instr & 0xFFFu;
        }
        else
        {
            uint32_t rm = instr & 0xFu;
            offset = (rm == 15u) ? (pc + 8u) : s.r[rm];
        }

        uint32_t base_val = (rn == 15u) ? (pc + 8u) : s.r[rn];
        uint32_t eff_addr = pre_index
                                ? (add_offset ? (base_val + offset) : (base_val - offset))
                                : base_val;

        if (is_load)
        {
            uint32_t loaded = 0u;
            if (is_byte)
            {
                loaded = static_cast<uint32_t>(mem.read_main_ram_u8(eff_addr));
                s.r[rd] = loaded;
            }
            else
            {
                loaded = mem.read_main_ram_u32_le(eff_addr);
                s.r[rd] = loaded;
            }

            if (!pre_index)
            {
                uint32_t post_addr = add_offset ? (base_val + offset) : (base_val - offset);
                if (rn != 15u)
                    s.r[rn] = post_addr;
            }
            else if (write_back && rn != 15u)
            {
                s.r[rn] = eff_addr;
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }
        else
        {
            uint32_t src_val = (rd == 15u) ? (pc + 12u) : s.r[rd];

            if (is_byte)
            {
                uint8_t byte_val = static_cast<uint8_t>(src_val & 0xFFu);
                mem.write_main_ram_u8(eff_addr, byte_val);
            }
            else
            {
                mem.write_main_ram_u32_le(eff_addr, src_val);
            }

            if (!pre_index)
            {
                uint32_t post_addr = add_offset ? (base_val + offset) : (base_val - offset);
                if (rn != 15u)
                    s.r[rn] = post_addr;
            }
            else if (write_back && rn != 15u)
            {
                s.r[rn] = eff_addr;
            }

            s.r[15] = pc + 4u;
            return true;
        }
    }

    // Data processing immediate: bit 27..25 == 001
    if (group == 0b001u)
    {
        uint32_t opcode = (instr >> 21) & 0xFu;
        bool set_flags = (((instr >> 20) & 0x1u) == 1u);
        uint32_t rn = (instr >> 16) & 0xFu;
        uint32_t rd = (instr >> 12) & 0xFu;
        uint32_t imm8 = instr & 0xFFu;
        uint32_t rot = ((instr >> 8) & 0xFu) * 2u;
        uint32_t imm32 = rot ? ((imm8 >> rot) | (imm8 << ((32u - rot) & 31u))) : imm8;
        uint32_t rn_val = (rn == 15u) ? (pc + 8u) : s.r[rn];

        // AND (opcode == 0x0)
        if (opcode == 0x0u)
        {
            s.r[rd] = rn_val & imm32;
            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // EOR / EORS Rd, Rn, #imm32 (opcode == 0x1)
        if (opcode == 0x1u)
        {
            uint32_t result = rn_val ^ imm32;
            s.r[rd] = result;

            if (set_flags)
            {
                uint32_t n_flag = (result >> 31) & 1u;
                uint32_t z_flag = (result == 0u) ? 1u : 0u;
                s.cpsr &= ~0xC0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30);

                if (rot != 0u)
                {
                    uint32_t c_flag = (imm32 >> 31) & 1u;
                    s.cpsr = (s.cpsr & ~(1u << 29)) | (c_flag << 29);
                }
            }

            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // SUB / SUBS (opcode == 0x2)
        if (opcode == 0x2u)
        {
            uint32_t diff = rn_val - imm32;
            s.r[rd] = diff;

            if (set_flags)
            {
                uint32_t n_flag = (diff >> 31) & 1u;
                uint32_t z_flag = (diff == 0u) ? 1u : 0u;
                uint32_t c_flag = (rn_val >= imm32) ? 1u : 0u;
                uint32_t v_flag = (((rn_val ^ imm32) & (rn_val ^ diff)) >> 31) & 1u;

                s.cpsr &= ~0xF0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30) | (c_flag << 29) | (v_flag << 28);
            }

            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // RSB / RSBS Rd, Rn, #imm32 (opcode == 0x3)
        if (opcode == 0x3u)
        {
            uint32_t diff = imm32 - rn_val;
            s.r[rd] = diff;

            if (set_flags)
            {
                uint32_t n_flag = (diff >> 31) & 1u;
                uint32_t z_flag = (diff == 0u) ? 1u : 0u;
                uint32_t c_flag = (imm32 >= rn_val) ? 1u : 0u;
                uint32_t v_flag = (((imm32 ^ rn_val) & (imm32 ^ diff)) >> 31) & 1u;

                s.cpsr &= ~0xF0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30) | (c_flag << 29) | (v_flag << 28);
            }

            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // ADD (opcode == 0x4)
        if (opcode == 0x4u)
        {
            s.r[rd] = rn_val + imm32;
            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // TST Rn, #imm32 (opcode == 0x8)
        if (opcode == 0x8u)
        {
            uint32_t res = rn_val & imm32;
            uint32_t n_flag = (res >> 31) & 1u;
            uint32_t z_flag = (res == 0u) ? 1u : 0u;

            s.cpsr &= ~(0xC0000000u);
            s.cpsr |= (n_flag << 31) | (z_flag << 30);
            s.r[15] = pc + 4u;
            return true;
        }

        // CMP (opcode == 0xA)
        if (opcode == 0xAu)
        {
            uint32_t diff = rn_val - imm32;
            uint32_t n_flag = (diff >> 31) & 1u;
            uint32_t z_flag = (diff == 0u) ? 1u : 0u;
            uint32_t c_flag = (rn_val >= imm32) ? 1u : 0u;
            uint32_t v_flag = (((rn_val ^ imm32) & (rn_val ^ diff)) >> 31) & 1u;

            s.cpsr &= ~0xF0000000u;
            s.cpsr |= (n_flag << 31) | (z_flag << 30) | (c_flag << 29) | (v_flag << 28);
            s.r[15] = pc + 4u;
            return true;
        }

        // ORR (opcode == 0xC)
        if (opcode == 0xCu)
        {
            s.r[rd] = rn_val | imm32;
            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // MOV (opcode == 0xD)
        if (opcode == 0xDu)
        {
            s.r[rd] = imm32;
            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // MVN Rd, #imm32 (opcode == 0xF)
        if (opcode == 0xFu)
        {
            uint32_t result = ~imm32;
            s.r[rd] = result;

            if (set_flags)
            {
                uint32_t n_flag = (result >> 31) & 1u;
                uint32_t z_flag = (result == 0u) ? 1u : 0u;
                s.cpsr &= ~0xC0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30);

                if (rot != 0u)
                {
                    uint32_t c_flag = (imm32 >> 31) & 1u;
                    s.cpsr = (s.cpsr & ~(1u << 29)) | (c_flag << 29);
                }
            }

            if (rd != 15u)
                s.r[15] = pc + 4u;
            return true;
        }

        // BIC Rd, Rn, #imm32 (opcode == 0xE)
        if (opcode == 0xEu)
        {
            uint32_t res = rn_val & ~imm32;
            s.r[rd] = res;

            if (set_flags)
            {
                uint32_t n_flag = (res >> 31) & 1u;
                uint32_t z_flag = (res == 0u) ? 1u : 0u;
                s.cpsr &= ~(0xC0000000u);
                s.cpsr |= (n_flag << 31) | (z_flag << 30);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }
    }

    // Wait For Interrupt: MCR p15, 0, R0, c7, c0, 4
    if ((instr & 0x0FFFFFFFu) == 0x0E070F90u)
    {
        if (s.cpu_id == 0u && s.wfi_count == 0u)
            mem.end_startup_io_trace();
        s.last_wfi_pc = pc;
        s.last_wfi_lr = s.r[14];
        s.last_wfi_sp = s.r[13];
        s.last_wfi_cpsr = s.cpsr;
        ++s.wfi_count;
        if (s.cpu_id == 0u)
            mem.end_irq_handler_trace();
        s.waiting_for_interrupt = true;
        s.r[15] = pc + 4u;
        return true;
    }

    // Coprocessor register transfer (MCR / MRC): bit 27..24 == 1110 e bit 4 == 1
    if (((instr >> 24) & 0xFu) == 0xEu && (((instr >> 4) & 0x1u) == 1u))
    {
        bool is_mrc = (((instr >> 20) & 0x1u) == 1u);
        uint32_t rd = (instr >> 12) & 0xFu;

        if (is_mrc)
        {
            if (rd != 15u)
            {
                s.r[rd] = 0u;
            }
        }

        s.r[15] = pc + 4u;
        return true;
    }

    // Data processing register (con shift immediato): bit 27..25 == 000, non (bit 7 == 1 && bit 4 == 1)
    if (group == 0b000u && !((((instr >> 7) & 0x1u) == 1u) && (((instr >> 4) & 0x1u) == 1u)))
    {
        uint32_t opcode = (instr >> 21) & 0xFu;
        bool set_flags = (((instr >> 20) & 0x1u) == 1u);
        uint32_t rn = (instr >> 16) & 0xFu;
        uint32_t rd = (instr >> 12) & 0xFu;
        uint32_t shift_imm = (instr >> 7) & 0x1Fu;
        uint32_t shift_type = (instr >> 5) & 0x3u;
        bool shift_by_register = (((instr >> 4) & 0x1u) != 0u);
        uint32_t rm = instr & 0xFu;

        uint32_t rn_val = (rn == 15u) ? (pc + 8u) : s.r[rn];
        uint32_t rm_val = (rm == 15u) ? (pc + 8u) : s.r[rm];
        uint32_t op2 = rm_val;
        uint32_t shifter_carry_out = (s.cpsr >> 29) & 1u;

        if (shift_by_register)
        {
            uint32_t rs = (instr >> 8) & 0xFu;
            uint32_t shift_amount = ((rs == 15u) ? (pc + 8u) : s.r[rs]) & 0xFFu;

            if (shift_type == 0u) // LSL
            {
                if (shift_amount != 0u)
                {
                    if (shift_amount < 32u)
                    {
                        op2 = rm_val << shift_amount;
                        shifter_carry_out = (rm_val >> (32u - shift_amount)) & 1u;
                    }
                    else
                    {
                        op2 = 0u;
                        shifter_carry_out = (shift_amount == 32u) ? (rm_val & 1u) : 0u;
                    }
                }
            }
            else if (shift_type == 1u) // LSR
            {
                if (shift_amount != 0u)
                {
                    if (shift_amount < 32u)
                    {
                        op2 = rm_val >> shift_amount;
                        shifter_carry_out = (rm_val >> (shift_amount - 1u)) & 1u;
                    }
                    else
                    {
                        op2 = 0u;
                        shifter_carry_out = (shift_amount == 32u) ? (rm_val >> 31) : 0u;
                    }
                }
            }
            else if (shift_type == 2u) // ASR
            {
                if (shift_amount != 0u)
                {
                    int32_t signed_rm = static_cast<int32_t>(rm_val);
                    if (shift_amount < 32u)
                    {
                        op2 = static_cast<uint32_t>(signed_rm >> shift_amount);
                        shifter_carry_out = (rm_val >> (shift_amount - 1u)) & 1u;
                    }
                    else
                    {
                        op2 = static_cast<uint32_t>(signed_rm >> 31);
                        shifter_carry_out = rm_val >> 31;
                    }
                }
            }
            else if (shift_type == 3u && shift_amount != 0u) // ROR
            {
                uint32_t rotate = shift_amount & 0x1Fu;
                op2 = (rotate == 0u)
                          ? rm_val
                          : ((rm_val >> rotate) | (rm_val << (32u - rotate)));
                shifter_carry_out = op2 >> 31;
            }
        }
        else if (shift_type == 0u) // LSL
        {
            if (shift_imm != 0u)
            {
                op2 = rm_val << shift_imm;
                shifter_carry_out = (rm_val >> (32u - shift_imm)) & 1u;
            }
        }
        else if (shift_type == 1u) // LSR
        {
            if (shift_imm == 0u)
            {
                op2 = 0u;
                shifter_carry_out = rm_val >> 31;
            }
            else
            {
                op2 = rm_val >> shift_imm;
                shifter_carry_out = (rm_val >> (shift_imm - 1u)) & 1u;
            }
        }
        else if (shift_type == 2u) // ASR
        {
            int32_t signed_rm = static_cast<int32_t>(rm_val);
            if (shift_imm == 0u)
            {
                op2 = static_cast<uint32_t>(signed_rm >> 31);
                shifter_carry_out = rm_val >> 31;
            }
            else
            {
                op2 = static_cast<uint32_t>(signed_rm >> shift_imm);
                shifter_carry_out = (rm_val >> (shift_imm - 1u)) & 1u;
            }
        }
        else if (shift_type == 3u) // ROR / RRX
        {
            if (shift_imm == 0u)
            {
                uint32_t c_in = (s.cpsr >> 29) & 1u;
                op2 = (c_in << 31) | (rm_val >> 1);
                shifter_carry_out = rm_val & 1u;
            }
            else
            {
                op2 = (rm_val >> shift_imm) | (rm_val << (32u - shift_imm));
                shifter_carry_out = op2 >> 31;
            }
        }

        // AND / ANDS Rd, Rn, op2 (opcode == 0x0)
        if (opcode == 0x0u)
        {
            uint32_t result = rn_val & op2;
            s.r[rd] = result;

            if (set_flags)
            {
                uint32_t n_flag = (result >> 31) & 1u;
                uint32_t z_flag = (result == 0u) ? 1u : 0u;
                s.cpsr &= ~0xE0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30) | (shifter_carry_out << 29);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }

        // EOR / EORS Rd, Rn, op2 (opcode == 0x1)
        if (opcode == 0x1u)
        {
            uint32_t result = rn_val ^ op2;
            s.r[rd] = result;

            if (set_flags)
            {
                uint32_t n_flag = (result >> 31) & 1u;
                uint32_t z_flag = (result == 0u) ? 1u : 0u;
                s.cpsr &= ~0xE0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30) | (shifter_carry_out << 29);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }

        // TST Rn, op2 (opcode == 0x8)
        if (opcode == 0x8u)
        {
            uint32_t result = rn_val & op2;
            uint32_t n_flag = (result >> 31) & 1u;
            uint32_t z_flag = (result == 0u) ? 1u : 0u;
            s.cpsr &= ~0xE0000000u;
            s.cpsr |= (n_flag << 31) | (z_flag << 30) | (shifter_carry_out << 29);
            s.r[15] = pc + 4u;
            return true;
        }

        // BIC / BICS Rd, Rn, op2 (opcode == 0xE)
        if (opcode == 0xEu)
        {
            uint32_t result = rn_val & ~op2;
            s.r[rd] = result;

            if (set_flags)
            {
                uint32_t n_flag = (result >> 31) & 1u;
                uint32_t z_flag = (result == 0u) ? 1u : 0u;
                s.cpsr &= ~0xE0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30) | (shifter_carry_out << 29);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }

        // SUB / SUBS Rd, Rn, op2 (opcode == 0x2)
        if (opcode == 0x2u)
        {
            uint32_t diff = rn_val - op2;
            s.r[rd] = diff;

            if (set_flags)
            {
                uint32_t n_flag = (diff >> 31) & 1u;
                uint32_t z_flag = (diff == 0u) ? 1u : 0u;
                uint32_t c_flag = (rn_val >= op2) ? 1u : 0u;
                uint32_t v_flag = (((rn_val ^ op2) & (rn_val ^ diff)) >> 31) & 1u;

                s.cpsr &= ~0xF0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30) | (c_flag << 29) | (v_flag << 28);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }

        // ADD / ADDS Rd, Rn, op2 (opcode == 0x4)
        if (opcode == 0x4u)
        {
            uint64_t sum = static_cast<uint64_t>(rn_val) + static_cast<uint64_t>(op2);
            uint32_t result = static_cast<uint32_t>(sum);
            s.r[rd] = result;

            if (set_flags)
            {
                uint32_t n_flag = (result >> 31) & 1u;
                uint32_t z_flag = (result == 0u) ? 1u : 0u;
                uint32_t c_flag = static_cast<uint32_t>(sum >> 32);
                uint32_t v_flag = ((~(rn_val ^ op2) & (rn_val ^ result)) >> 31) & 1u;

                s.cpsr &= ~0xF0000000u;
                s.cpsr |= (n_flag << 31) | (z_flag << 30) | (c_flag << 29) | (v_flag << 28);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }

        // CMP Rn, op2 (opcode == 0xA)
        if (opcode == 0xAu)
        {
            uint32_t diff = rn_val - op2;
            uint32_t n_flag = (diff >> 31) & 1u;
            uint32_t z_flag = (diff == 0u) ? 1u : 0u;
            uint32_t c_flag = (rn_val >= op2) ? 1u : 0u;
            uint32_t v_flag = (((rn_val ^ op2) & (rn_val ^ diff)) >> 31) & 1u;

            s.cpsr &= ~0xF0000000u;
            s.cpsr |= (n_flag << 31) | (z_flag << 30) | (c_flag << 29) | (v_flag << 28);
            s.r[15] = pc + 4u;
            return true;
        }

        // ORR Rd, Rn, op2 (opcode == 0xC)
        if (opcode == 0xCu)
        {
            uint32_t res = rn_val | op2;
            s.r[rd] = res;

            if (set_flags)
            {
                uint32_t n_flag = (res >> 31) & 1u;
                uint32_t z_flag = (res == 0u) ? 1u : 0u;
                s.cpsr &= ~(0xC0000000u);
                s.cpsr |= (n_flag << 31) | (z_flag << 30);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }

            return true;
        }

        // MOV Rd, op2 (opcode == 0xD)
        if (opcode == 0xDu)
        {
            if (rd == 15u && set_flags)
            {
                uint32_t *saved_status = saved_status_register(s);
                if (saved_status != nullptr)
                {
                    uint32_t restored_cpsr = *saved_status;
                    write_cpsr(s, restored_cpsr);
                    s.r[15] = op2 & (((restored_cpsr >> 5) & 1u) ? ~1u : ~3u);
                    return true;
                }
            }

            s.r[rd] = op2;

            if (set_flags)
            {
                uint32_t n_flag = (op2 >> 31) & 1u;
                uint32_t z_flag = (op2 == 0u) ? 1u : 0u;
                s.cpsr &= ~(0xC0000000u);
                s.cpsr |= (n_flag << 31) | (z_flag << 30);
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }

            return true;
        }
    }

    // Block Data Transfer (LDM / STM): bit 27..25 == 100
    if (group == 0b100u)
    {
        bool pre_index = (((instr >> 24) & 0x1u) == 1u);
        bool up = (((instr >> 23) & 0x1u) == 1u);
        bool write_back = (((instr >> 21) & 0x1u) == 1u);
        bool is_load = (((instr >> 20) & 0x1u) == 1u);
        bool user_registers = (((instr >> 22) & 0x1u) != 0u);

        uint32_t rn = (instr >> 16) & 0xFu;
        uint32_t reg_list = instr & 0xFFFFu;
        bool pc_in_list = (reg_list & (1u << 15)) != 0u;
        bool exception_return = is_load && user_registers && pc_in_list;
        bool transfer_user_registers = user_registers && !exception_return;
        uint32_t base = (rn == 15u) ? (pc + 8u) : s.r[rn];

        uint32_t count = 0u;
        for (uint32_t r = 0u; r < 16u; ++r)
        {
            if ((reg_list >> r) & 1u)
            {
                ++count;
            }
        }

        uint32_t addr = 0u;
        if (up)
        {
            addr = pre_index ? (base + 4u) : base;
        }
        else
        {
            addr = pre_index ? (base - count * 4u) : (base - count * 4u + 4u);
        }

        bool pc_loaded = false;
        uint32_t loaded_pc = 0u;
        for (uint32_t r = 0u; r < 16u; ++r)
        {
            if ((reg_list >> r) & 1u)
            {
                if (is_load)
                {
                    uint32_t val = mem.read_main_ram_u32_le(addr);
                    if (r == 15u)
                    {
                        pc_loaded = true;
                        loaded_pc = val;
                    }
                    else if (transfer_user_registers)
                    {
                        write_user_mode_register(s, r, val);
                    }
                    else
                    {
                        s.r[r] = val;
                    }
                }
                else
                {
                    uint32_t val = (r == 15u)
                                       ? (pc + 12u)
                                       : (transfer_user_registers ? read_user_mode_register(s, r) : s.r[r]);
                    mem.write_main_ram_u32_le(addr, val);
                }
                addr += 4u;
            }
        }

        if (write_back && rn != 15u)
        {
            s.r[rn] = up ? (base + count * 4u) : (base - count * 4u);
        }

        if (pc_loaded)
        {
            uint32_t *saved_status = exception_return ? saved_status_register(s) : nullptr;
            if (saved_status != nullptr)
            {
                uint32_t restored_cpsr = *saved_status;
                write_cpsr(s, restored_cpsr);
                s.r[15] = loaded_pc & (((restored_cpsr >> 5) & 1u) ? ~1u : ~3u);
            }
            else
            {
                s.r[15] = loaded_pc & ~3u;
            }
        }
        else
        {
            s.r[15] = pc + 4u;
        }

        return true;
    }

    // Software Interrupt (SWI / SVC): bit 27..24 == 1111
    if (((instr >> 24) & 0xFu) == 0xFu)
    {
        s.r[15] = pc + 4u;
        return true;
    }

    // Halfword and Signed Data Transfer: bit 27..25 == 000, bit 7 == 1, bit 4 == 1, bits 6..5 != 00
    if (group == 0b000u &&
        (((instr >> 7) & 0x1u) == 1u) &&
        (((instr >> 4) & 0x1u) == 1u) &&
        (((instr >> 5) & 0x3u) != 0u))
    {
        bool pre_index = (((instr >> 24) & 0x1u) == 1u);
        bool add_offset = (((instr >> 23) & 0x1u) == 1u);
        bool is_imm_off = (((instr >> 22) & 0x1u) == 1u);
        bool write_back = (((instr >> 21) & 0x1u) == 1u);
        bool is_load = (((instr >> 20) & 0x1u) == 1u);

        uint32_t rn = (instr >> 16) & 0xFu;
        uint32_t rd = (instr >> 12) & 0xFu;
        uint32_t sh = (instr >> 5) & 0x3u; // 01 = H, 10 = SB, 11 = SH

        uint32_t offset = 0u;
        if (is_imm_off)
        {
            offset = (((instr >> 8) & 0xFu) << 4) | (instr & 0xFu);
        }
        else
        {
            uint32_t rm = instr & 0xFu;
            offset = (rm == 15u) ? (pc + 8u) : s.r[rm];
        }

        uint32_t base_val = (rn == 15u) ? (pc + 8u) : s.r[rn];
        uint32_t eff_addr = pre_index
                                ? (add_offset ? (base_val + offset) : (base_val - offset))
                                : base_val;

        if (is_load)
        {
            uint16_t lo = static_cast<uint16_t>(mem.read_main_ram_u8(eff_addr));
            uint16_t hi = static_cast<uint16_t>(mem.read_main_ram_u8(eff_addr + 1u));
            uint16_t val16 = static_cast<uint16_t>(lo | (hi << 8));

            if (sh == 0b01u) // LDRH (unsigned 16-bit)
            {
                s.r[rd] = static_cast<uint32_t>(val16);
            }
            else if (sh == 0b10u) // LDRSB (signed 8-bit)
            {
                s.r[rd] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(lo)));
            }
            else if (sh == 0b11u) // LDRSH (signed 16-bit)
            {
                s.r[rd] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(val16)));
            }

            if (!pre_index)
            {
                uint32_t post_addr = add_offset ? (base_val + offset) : (base_val - offset);
                if (rn != 15u)
                    s.r[rn] = post_addr;
            }
            else if (write_back && rn != 15u)
            {
                s.r[rn] = eff_addr;
            }

            if (rd != 15u)
            {
                s.r[15] = pc + 4u;
            }
            return true;
        }
        else if (sh == 0b01u) // STRH
        {
            uint32_t src_val = (rd == 15u) ? (pc + 12u) : s.r[rd];
            mem.write_main_ram_u8(eff_addr, static_cast<uint8_t>(src_val & 0xFFu));
            mem.write_main_ram_u8(eff_addr + 1u, static_cast<uint8_t>((src_val >> 8) & 0xFFu));

            if (!pre_index)
            {
                uint32_t post_addr = add_offset ? (base_val + offset) : (base_val - offset);
                if (rn != 15u)
                    s.r[rn] = post_addr;
            }
            else if (write_back && rn != 15u)
            {
                s.r[rn] = eff_addr;
            }

            s.r[15] = pc + 4u;
            return true;
        }
    }

    if (s.r[15] == 0x00000000u)
    {
        std::cout << std::hex << std::uppercase << std::setfill('0');
        std::cout << "[DEBUG] PC diventato 0x00000000 da PC=0x" << std::setw(8) << pc
                  << " | Instr=0x" << std::setw(8) << instr << "\n";
        std::cout << std::dec;
    }

    std::cout << std::hex << std::uppercase << std::setfill('0');
    std::cout << "Istruzione non gestita: PC: 0x" << std::setw(8) << pc << " | Instr: 0x" << std::setw(8) << instr << "\n";
    for (uint32_t reg = 0; reg < 16u; ++reg)
    {
        std::cout << "R" << std::dec << reg << "=0x" << std::hex << std::setw(8) << s.r[reg]
                  << ((reg % 4u == 3u) ? "\n" : "  ");
    }
    for (uint32_t addr = pc - 8u; addr <= pc + 8u; addr += 4u)
    {
        std::cout << "0x" << std::setw(8) << addr << ": 0x"
                  << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
    }
    for (uint32_t addr : {0x01FF86B8u, 0x03000000u, 0x03800000u, 0x0380FFFCu})
    {
        std::cout << "0x" << std::setw(8) << addr << ": 0x"
                  << std::setw(8) << mem.read_main_ram_u32_le(addr) << "\n";
    }
    s.r[15] = pc + 4u;
    std::cout << std::dec;
    return false;
}