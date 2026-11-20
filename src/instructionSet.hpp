#pragma once

#include "emuStruct.hpp"

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

inline bool step_once(CpuState &s, NdsMemory &mem)
{
    uint32_t pc = s.r[15];
    uint32_t instr = mem.read_main_ram_u32_le(pc);

    // Stop immediato su word nulla (zona vuota / non mappata)
    if (instr == 0x00000000u)
    {
        return false;
    }

    uint32_t cond = (instr >> 28) & 0xFu;
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

    // BX / BLX Rn: bit 27..8 == 0x12FFF e bit 7..5 == 001 e bit 4 == 1
    if (((instr >> 8) & 0xFFFFFu) == 0x12FFFu &&
        (((instr >> 5) & 0x7u) == 0b001u) &&
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

    // MSR immediate
    if (((instr >> 20) & 0xFFu) == 0x32u && (((instr >> 12) & 0xFu) == 0xFu))
    {
        uint32_t imm8 = instr & 0xFFu;
        uint32_t rot = ((instr >> 8) & 0xFu) * 2u;
        uint32_t val = rot ? ((imm8 >> rot) | (imm8 << ((32u - rot) & 31u))) : imm8;
        uint32_t field_mask = (instr >> 16) & 0xFu;

        uint32_t mask = 0u;
        if (field_mask & 0x1u)
            mask |= 0x000000FFu; // c
        if (field_mask & 0x2u)
            mask |= 0x0000FF00u; // x
        if (field_mask & 0x4u)
            mask |= 0x00FF0000u; // s
        if (field_mask & 0x8u)
            mask |= 0xFF000000u; // f

        s.cpsr = (s.cpsr & ~mask) | (val & mask);
        s.r[15] = pc + 4u;
        return true;
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
        uint32_t rm = instr & 0xFu;

        uint32_t rn_val = (rn == 15u) ? (pc + 8u) : s.r[rn];
        uint32_t rm_val = (rm == 15u) ? (pc + 8u) : s.r[rm];
        uint32_t op2 = rm_val;

        if (shift_type == 0u) // LSL
        {
            op2 = (shift_imm == 0u) ? rm_val : (rm_val << shift_imm);
        }
        else if (shift_type == 1u) // LSR
        {
            op2 = (shift_imm == 0u) ? 0u : (rm_val >> shift_imm);
        }
        else if (shift_type == 2u) // ASR
        {
            int32_t s_rm = static_cast<int32_t>(rm_val);
            op2 = (shift_imm == 0u)
                      ? static_cast<uint32_t>(s_rm >> 31)
                      : static_cast<uint32_t>(s_rm >> shift_imm);
        }
        else if (shift_type == 3u) // ROR / RRX
        {
            if (shift_imm == 0u)
            {
                uint32_t c_in = (s.cpsr >> 29) & 1u;
                op2 = (c_in << 31) | (rm_val >> 1);
            }
            else
            {
                op2 = (rm_val >> shift_imm) | (rm_val << (32u - shift_imm));
            }
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

        // MOV Rd, op2 (opcode == 0xD)
        if (opcode == 0xDu)
        {
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

        

        (void)rn_val;
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
        uint32_t rm = instr & 0xFu;

        uint32_t rn_val = (rn == 15u) ? (pc + 8u) : s.r[rn];
        uint32_t rm_val = (rm == 15u) ? (pc + 8u) : s.r[rm];
        uint32_t op2 = rm_val;

        if (shift_type == 0u) // LSL
        {
            op2 = (shift_imm == 0u) ? rm_val : (rm_val << shift_imm);
        }
        else if (shift_type == 1u) // LSR
        {
            op2 = (shift_imm == 0u) ? 0u : (rm_val >> shift_imm);
        }
        else if (shift_type == 2u) // ASR
        {
            int32_t s_rm = static_cast<int32_t>(rm_val);
            op2 = (shift_imm == 0u)
                      ? static_cast<uint32_t>(s_rm >> 31)
                      : static_cast<uint32_t>(s_rm >> shift_imm);
        }
        else if (shift_type == 3u) // ROR / RRX
        {
            if (shift_imm == 0u)
            {
                uint32_t c_in = (s.cpsr >> 29) & 1u;
                op2 = (c_in << 31) | (rm_val >> 1);
            }
            else
            {
                op2 = (rm_val >> shift_imm) | (rm_val << (32u - shift_imm));
            }
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

        

        (void)rd;
        (void)set_flags;
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

        std::cout << " -> " << (is_blx ? "BLX" : "BX") << " R" << std::dec << rm << std::hex
                  << " -> 0x" << std::setw(8) << s.r[15]
                  << (thumb_state ? " [THUMB]" : " [ARM]") << "\n";
        std::cout << std::dec;
        return true;
    }

    // Block Data Transfer (LDM / STM): bit 27..25 == 100
    if (group == 0b100u)
    {
        bool pre_index = (((instr >> 24) & 0x1u) == 1u);
        bool up = (((instr >> 23) & 0x1u) == 1u);
        bool write_back = (((instr >> 21) & 0x1u) == 1u);
        bool is_load = (((instr >> 20) & 0x1u) == 1u);

        uint32_t rn = (instr >> 16) & 0xFu;
        uint32_t reg_list = instr & 0xFFFFu;
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
            addr = pre_index ? (base - count * 4u - 4u) : (base - count * 4u);
        }

        bool pc_loaded = false;
        for (uint32_t r = 0u; r < 16u; ++r)
        {
            if ((reg_list >> r) & 1u)
            {
                if (is_load)
                {
                    uint32_t val = mem.read_main_ram_u32_le(addr);
                    s.r[r] = val;
                    if (r == 15u)
                    {
                        pc_loaded = true;
                    }
                }
                else
                {
                    uint32_t val = (r == 15u) ? (pc + 12u) : s.r[r];
                    mem.write_main_ram_u32_le(addr, val);
                }
                addr += 4u;
            }
        }

        if (write_back && rn != 15u)
        {
            s.r[rn] = up ? (base + count * 4u) : (base - count * 4u);
        }

        if (!pc_loaded)
        {
            s.r[15] = pc + 4u;
        }

        return true;
    }

    if (((instr >> 24) & 0xFu) == 0xFu)
    {
        s.r[15] = pc + 4u;
        return true;
    }

    std::cout << std::hex << std::uppercase << std::setfill('0');
    std::cout << "Istruzione non gestita: PC: 0x" << std::setw(8) << pc
              << " | Instr: 0x" << std::setw(8) << instr << "\n";
    s.r[15] = pc + 4u;
    std::cout << std::dec;
    return false;
}