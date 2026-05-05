/* =========================================================
Copyright (C) 2026 - Peneaux Benjamin 
This program is free software; 
you may redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; 
either version 3 of the license or (at your discretion) any later version.
========================================================= */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "gpu.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================= DEBUG CONFIGURATION ================= */

// Enable/disable different debug levels
#define DEBUG_CPU_STEPS     1    // Display each instruction
#define DEBUG_MEMORY_ACCESS 1    // Display memory access (verbose - disabled by default)
#define DEBUG_STACK         1    // Display stack operations
#define DEBUG_GPU_OPS       1    // Display GPU operations (disabled by default)
#define DEBUG_JUMPS         1    // Display jumps/calls
#define DEBUG_ALIGNMENT     1    // Display alignment warnings
#define DEBUG_NMI			1
#define DEBUG_IRQ			1

/* ================= UNIFIED MEMORY ACCESS ================= */

/**
 * CORRECTION: Unified 8-bit memory access functions
 * All MCPY, DMA, MSET functions pass through here
 */
uint8_t mem_read8(PB010381_CPU *cpu, uint32_t addr) {
    // === VRAM ===
    if (addr >= VRAM_START && addr <= VRAM_END) {
        uint32_t offset = addr - VRAM_START;
        if (offset < VRAM_SIZE) {
            return cpu->gpu->vram[offset];
        }
        return 0xFF;
    }
    
    // === RAM ===
    if (addr >= RAM_START && addr <= RAM_END) {
        uint32_t offset = addr - RAM_START;
        if (offset < RAM_SIZE) {
            return cpu->memory[offset];
        }
        return 0xFF;
    }
    
    // === ROM ===
    if (addr >= ROM_START && addr <= ROM_END) {
        if (addr < MEM_SIZE) {
            return cpu->memory[addr];
        }
        return 0xFF;
    }
    
    // Invalid address
    return 0xFF;
}

void mem_write8(PB010381_CPU *cpu, uint32_t addr, uint8_t val) {
    // === ROM Protection ===
    if (addr >= ROM_START && addr <= ROM_END) {
        // Ignore writes to ROM
        return;
    }
    
    // === VRAM ===
    if (addr >= VRAM_START && addr <= VRAM_END) {
        uint32_t offset = addr - VRAM_START;
        if (offset < VRAM_SIZE) {
            cpu->gpu->vram[offset] = val;
        }
        return;
    }
    
    // === RAM ===
    if (addr >= RAM_START && addr <= RAM_END) {
        uint32_t offset = addr - RAM_START;
        if (offset < RAM_SIZE) {
            cpu->memory[offset] = val;
        }
        return;
    }
}

/* ================= 16-BIT MEMORY ACCESS ================= */

/**
 * Read 16 bits from memory
 * Uses mem_read8 for consistency
 * CORRECTION: Forced 2-byte alignment to avoid bugs
 */
uint16_t mem_read16(PB010381_CPU *cpu, uint32_t addr) {
    // CRITICAL: Word-aligned address to ensure consistency
    if (addr & 1) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Unaligned 16-bit read @ 0x%08X -> aligned to 0x%08X\n", 
               addr, addr & ~1);
        #endif
        addr &= ~1;
    }
    
    uint16_t val = 0;
 
    // === INTERRUPT CONTROLLER MMIO ===
    if (addr >= INT_MMIO_START && addr <= INT_MMIO_END) {
        uint32_t off = addr - INT_MMIO_START;
        if (off == 0x00) {
            // Read: INT_CTRL
            return (cpu->irq_enabled ? 0x0001 : 0x0000) |
                   (cpu->nmi_pending ? 0x0002 : 0x0000);
        }
        if (off == 0x02) {
            // Read: INT_STATUS
            return (cpu->irq_pending ? 0x0001 : 0x0000) |
                   (cpu->nmi_pending ? 0x0002 : 0x0000);
        }
        return 0;
    }
 
    // === CONTROLLERS MMIO ===
    if (addr >= CONTROLLER_MMIO_START && addr <= CONTROLLER_MMIO_END) {
        if (cpu->controllers) {
            val = controllers_read16(cpu->controllers, addr);
            #if DEBUG_MEMORY_ACCESS
            printf("    [CTRL_READ] 0x%08X => 0x%04X\n", addr, val);
            #endif
            return val;
        }
        return 0;
    }
	
	// === APU MMIO ===
    if (addr >= APU_MMIO_START && addr <= APU_MMIO_END) {
        if (cpu->apu) {
            val = apu_read16(cpu->apu, addr);
            #if DEBUG_MEMORY_ACCESS
            printf("    [APU_READ] 0x%08X => 0x%04X\n", addr, val);
            #endif
            return val;
        }
        return 0;
    } 	
    
    // === GPU MMIO ===
    if (addr >= GPU_MMIO_START && addr <= GPU_MMIO_END) {
        val = gpu_read16(cpu->gpu, addr);
        #if DEBUG_GPU_OPS
        printf("    [GPU_READ] 0x%08X => 0x%04X\n", addr, val);
        #endif
        return val;
    }
    
    // === Normal memory (via unified mem_read8) ===
    val = (mem_read8(cpu, addr) << 8) | mem_read8(cpu, addr + 1);
    
    #if DEBUG_MEMORY_ACCESS
    printf("    [MEM_READ16] 0x%08X => 0x%04X\n", addr, val);
    #endif
    
    return val;
}

/**
 * Write 16 bits to memory
 * Uses mem_write8 for consistency
 * CORRECTION: Forced 2-byte alignment
 */
void mem_write16(PB010381_CPU *cpu, uint32_t addr, uint16_t val) {
    // CRITICAL: Word-aligned address to ensure consistency
    if (addr & 1) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Unaligned 16-bit write @ 0x%08X -> aligned to 0x%08X\n", 
               addr, addr & ~1);
        #endif
        addr &= ~1;
    }

    // === INTERRUPT CONTROLLER MMIO ===
    if (addr >= INT_MMIO_START && addr <= INT_MMIO_END) {
        uint32_t off = addr - INT_MMIO_START;
        if (off == 0x00) {
            // INT_CTRL: bit0=IRQ enable, bit1=trigger IRQ manually
            cpu->irq_enabled = (val & 0x01) != 0;
            if (val & 0x02) cpu->irq_pending = true;
			#if DEBUG_IRQ
            printf("    [INT_CTRL] IRQ_EN=%d IRQ_TRIG=%d\n",
                   cpu->irq_enabled, (val >> 1) & 1);
			#endif
        }
        if (off == 0x02) {
            // INT_STATUS: write 0 to clear flags
            if (!(val & 0x01)) cpu->irq_pending = false;
            if (!(val & 0x02)) cpu->nmi_pending = false;
        }
        return;
    }
   
    // === CONTROLLERS MMIO ===
    if (addr >= CONTROLLER_MMIO_START && addr <= CONTROLLER_MMIO_END) {
        if (cpu->controllers) {
            controllers_write16(cpu->controllers, addr, val);
            #if DEBUG_MEMORY_ACCESS
            printf("    [CTRL_WRITE] 0x%08X <= 0x%04X\n", addr, val);
            #endif
        }
        return;
    }
    
    // === APU MMIO ===
    if (addr >= APU_MMIO_START && addr <= APU_MMIO_END) {
        if (cpu->apu) {
            apu_write16(cpu->apu, addr, val);
            #if DEBUG_MEMORY_ACCESS
            printf("    [APU_WRITE] 0x%08X <= 0x%04X\n", addr, val);
            #endif
        }
        return;
    }

    // === GPU MMIO ===
    if (addr >= GPU_MMIO_START && addr <= GPU_MMIO_END) {
        gpu_write16(cpu->gpu, addr, val);
        #if DEBUG_GPU_OPS
        printf("    [GPU_WRITE] 0x%08X <= 0x%04X\n", addr, val);
        #endif
        return;
    }
    
    // === Normal memory (via unified mem_write8) ===
    mem_write8(cpu, addr, (val >> 8) & 0xFF);
    mem_write8(cpu, addr + 1, val & 0xFF);
    
    #if DEBUG_MEMORY_ACCESS
    printf("    [MEM_WRITE16] 0x%08X <= 0x%04X\n", addr, val);
    #endif
}

/**
 * Read 32 bits from memory
 * CORRECTION: Forced 4-byte alignment
 */
uint32_t mem_read32(PB010381_CPU *cpu, uint32_t addr) {
    // CRITICAL: 4-byte alignment for 32-bit accesses
    if (addr & 3) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Unaligned 32-bit read @ 0x%08X -> aligned to 0x%08X\n", 
               addr, addr & ~3);
        #endif
        addr &= ~3;
    }
    
    uint32_t high = mem_read16(cpu, addr);
    uint32_t low  = mem_read16(cpu, addr + 2);
    return (high << 16) | low;
}

/**
 * Write 32 bits to memory
 * CORRECTION: Forced 4-byte alignment
 */
void mem_write32(PB010381_CPU *cpu, uint32_t addr, uint32_t val) {
    // CRITICAL: 4-byte alignment for 32-bit accesses
    if (addr & 3) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Unaligned 32-bit write @ 0x%08X -> aligned to 0x%08X\n", 
               addr, addr & ~3);
        #endif
        addr &= ~3;
    }
    
    mem_write16(cpu, addr,     (val >> 16) & 0xFFFF);
    mem_write16(cpu, addr + 2, val & 0xFFFF);
}

/* ================= UTILITIES ================= */

static inline uint16_t fetch16(PB010381_CPU *cpu) {
    uint16_t val = mem_read16(cpu, cpu->PC);
    cpu->PC += 2;
    return val;
}

static inline uint32_t fetch32(PB010381_CPU *cpu) {
    // CRITICAL: Do NOT force alignment for code immediates!
    // 32-bit immediates follow a 16-bit header, so never 4-byte aligned.
    uint32_t addr = cpu->PC;
    uint32_t val = (mem_read8(cpu, addr) << 24) |
                   (mem_read8(cpu, addr + 1) << 16) |
                   (mem_read8(cpu, addr + 2) << 8) |
                    mem_read8(cpu, addr + 3);
    cpu->PC += 4;
    return val;
}

/**
 * CORRECTION: 32-bit PUSH/POP for consistency
 * Registers are 32-bit, so PUSH/POP must be too
 */
static inline void push32(PB010381_CPU *cpu, uint32_t val) {
    cpu->R[15] -= 4;  // SP decrements by 4 (32-bit alignment)
    mem_write32(cpu, cpu->R[15], val);
    #if DEBUG_STACK
    printf("    [PUSH32] SP=0x%08X <= 0x%08X\n", cpu->R[15], val);
    #endif
}

static inline uint32_t pop32(PB010381_CPU *cpu) {
    uint32_t val = mem_read32(cpu, cpu->R[15]);
    cpu->R[15] += 4;
    #if DEBUG_STACK
    printf("    [POP32] SP=0x%08X => 0x%08X\n", cpu->R[15] - 4, val);
    #endif
    return val;
}

/* Keep push16/pop16 for compatibility if needed */
static inline void push16(PB010381_CPU *cpu, uint16_t val) {
    cpu->R[15] -= 2;
    mem_write16(cpu, cpu->R[15], val);
    #if DEBUG_STACK
    printf("    [PUSH16] SP=0x%08X <= 0x%04X\n", cpu->R[15], val);
    #endif
}

static inline uint16_t pop16(PB010381_CPU *cpu) {
    uint16_t val = mem_read16(cpu, cpu->R[15]);
    cpu->R[15] += 2;
    #if DEBUG_STACK
    printf("    [POP16] SP=0x%08X => 0x%04X\n", cpu->R[15] - 2, val);
    #endif
    return val;
}

/**
 * CORRECTION: Correct signed overflow calculation
 */
static inline bool calc_overflow_add(uint32_t a, uint32_t b, uint32_t result) {
    // Overflow if: (a and b same sign) AND (result sign different)
    bool a_sign = (a >> 31) & 1;
    bool b_sign = (b >> 31) & 1;
    bool r_sign = (result >> 31) & 1;
    return (a_sign == b_sign) && (a_sign != r_sign);
}

static inline bool calc_overflow_sub(uint32_t a, uint32_t b, uint32_t result) {
    // Overflow if: (a and b different signs) AND (result sign != a)
    bool a_sign = (a >> 31) & 1;
    bool b_sign = (b >> 31) & 1;
    bool r_sign = (result >> 31) & 1;
    return (a_sign != b_sign) && (a_sign != r_sign);
}

static inline void update_zn(PB010381_CPU *cpu, uint32_t val) {
    cpu->Flags.Z = (val == 0);
    cpu->Flags.N = (val >> 31) & 1;
}

/* ================= CPU LOOP ================= */

void cpu_step(PB010381_CPU *cpu) {
    if (cpu->halted) return;

    // ============================================================
    // INTERRUPT DISPATCH
    // ============================================================

    // --- NMI (Non-Maskable Interrupt) — highest priority ---
    if (cpu->nmi_pending) {
        cpu->nmi_pending    = false;
        cpu->waiting_vblank = false;   // Wake up waiting VSYNC

        // Save PC and flags to stack
        push32(cpu, cpu->PC);
        uint32_t flags_word = ((uint32_t)cpu->Flags.Z)          |
                              ((uint32_t)cpu->Flags.N     << 1)  |
                              ((uint32_t)cpu->Flags.C     << 2)  |
                              ((uint32_t)cpu->Flags.V     << 3)  |
                              ((uint32_t)cpu->irq_enabled << 4);
        push32(cpu, flags_word);
        cpu->irq_enabled = false;      // IRQ disabled during NMI handler

        uint32_t vector = mem_read32(cpu, NMI_VECTOR_ADDR);
        if (vector != 0) {
			#if DEBUG_NMI
            printf("    [NMI] Dispatch -> 0x%08X (PC saved: 0x%08X)\n",
                   vector, cpu->PC);
			#endif
            cpu->PC = vector;
        }
        cpu->total_cycles++;
        return;
    }

    // --- IRQ (maskable) ---
    if (cpu->irq_pending && cpu->irq_enabled) {
        cpu->irq_pending  = false;
        cpu->irq_enabled  = false;     // Disable IRQs during handler

        push32(cpu, cpu->PC);
        uint32_t flags_word = ((uint32_t)cpu->Flags.Z)          |
                              ((uint32_t)cpu->Flags.N     << 1)  |
                              ((uint32_t)cpu->Flags.C     << 2)  |
                              ((uint32_t)cpu->Flags.V     << 3)  |
                              ((uint32_t)cpu->irq_enabled << 4);
        push32(cpu, flags_word);

        uint32_t vector = mem_read32(cpu, IRQ_VECTOR_ADDR);
        if (vector != 0) {
			#if DEBUG_IRQ
            printf("    [IRQ] Dispatch -> 0x%08X (PC saved: 0x%08X)\n",
                   vector, cpu->PC);
			#endif
            cpu->PC = vector;
        }
        cpu->total_cycles++;
        return;
    }

    // --- If waiting for VBlank (VSYNC), suspend execution ---
    if (cpu->waiting_vblank) return;

    // ============================================================
    // NORMAL EXECUTION
    // ============================================================
  
    uint32_t pc_before = cpu->PC;
    uint16_t header = fetch16(cpu);
    
    uint8_t op = (header >> 8) & 0xFF;
    uint8_t rd = (header >> 4) & 0x0F;
    uint8_t rs = header & 0x0F;
    
    #if DEBUG_CPU_STEPS
    printf("[CPU] PC=0x%08X | OP=0x%02X | Rd=R%d | Rs=R%d\n", 
           pc_before, op, rd, rs);
    #endif
    
    switch (op) {
        /* ===== CONTROL ===== */
        case 0x00:  // HALT
            printf("    [HALT] CPU stopped\n");
            cpu->halted = true;
            break;
            
        case 0xFF:  // NOP
            break;
            
        case 0xFC:  // CLC - Clear Carry
            cpu->Flags.C = false;
            break;
            
        case 0xFB:  // SEC - Set Carry
            cpu->Flags.C = true;
            break;

        case 0xF8:  // RTI — Return from Interrupt
        {
            uint32_t flags_word = pop32(cpu);
            cpu->Flags.Z       = (flags_word >> 0) & 1;
            cpu->Flags.N       = (flags_word >> 1) & 1;
            cpu->Flags.C       = (flags_word >> 2) & 1;
            cpu->Flags.V       = (flags_word >> 3) & 1;
            cpu->irq_enabled   = (flags_word >> 4) & 1;
            uint32_t ret_addr  = pop32(cpu);
            #if DEBUG_JUMPS
            printf("    [RTI] PC: 0x%08X -> 0x%08X (IRQ enabled: %d)\n",
                   pc_before, ret_addr, cpu->irq_enabled);
            #endif
            cpu->PC = ret_addr;
        }
        break;

        case 0xF9:  // CLI — Clear Interrupt Enable
            cpu->irq_enabled = false;
            #if DEBUG_CPU_STEPS
            printf("    [CLI] IRQ disabled\n");
            #endif
            break;

        case 0xFA:  // SEI — Set Interrupt Enable
            cpu->irq_enabled = true;
            #if DEBUG_CPU_STEPS
            printf("    [SEI] IRQ enabled\n");
            #endif
            break;

        /* ===== LOAD/STORE WITH 32-BIT ADDRESSES ===== */
        
        /* 8-bit instructions - NEW */
        case 0x62:  // LDRI8 Rd, imm32 - Load 8-bit from [imm32]
        {
            uint32_t addr = fetch32(cpu);
            cpu->R[rd] = mem_read8(cpu, addr);
            update_zn(cpu, cpu->R[rd]);
            #if DEBUG_MEMORY_ACCESS
            printf("    [LDRI8] R%d = mem8[0x%08X] = 0x%02X\n", rd, addr, cpu->R[rd]);
            #endif
        }
        break;
        
        case 0x63:  // STRI8 imm32, Rs - Store 8-bit to [imm32]
        {
            uint32_t addr = fetch32(cpu);
            mem_write8(cpu, addr, cpu->R[rs] & 0xFF);
            #if DEBUG_MEMORY_ACCESS
            printf("    [STRI8] mem8[0x%08X] = R%d = 0x%02X\n", addr, rs, cpu->R[rs] & 0xFF);
            #endif
        }
        break;
        
        case 0x64:  // LDR8 Rd, Rs - Load 8-bit from [Rs]
        {
            cpu->R[rd] = mem_read8(cpu, cpu->R[rs]);
            update_zn(cpu, cpu->R[rd]);
            #if DEBUG_MEMORY_ACCESS
            printf("    [LDR8] R%d = mem8[R%d=0x%08X] = 0x%02X\n", 
                   rd, rs, cpu->R[rs], cpu->R[rd]);
            #endif
        }
        break;
        
        case 0x65:  // STR8 Rs, Rd - Store 8-bit to [Rd]
        {
            mem_write8(cpu, cpu->R[rd], cpu->R[rs] & 0xFF);
            #if DEBUG_MEMORY_ACCESS
            printf("    [STR8] mem8[R%d=0x%08X] = R%d = 0x%02X\n", 
                   rd, cpu->R[rd], rs, cpu->R[rs] & 0xFF);
            #endif
        }
        break;
        
        /* 16-bit instructions */
        case 0x01:  // LDRI Rd, imm32 - Load 16-bit from [imm32]
        {
            uint32_t addr = fetch32(cpu);
            cpu->R[rd] = mem_read16(cpu, addr);
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x02:  // STRI imm32, Rs - Store 16-bit to [imm32]
        {
            uint32_t addr = fetch32(cpu);
            mem_write16(cpu, addr, cpu->R[rs] & 0xFFFF);
        }
        break;
        
        /* 32-bit instructions */
        case 0x03:  // LDR Rd, Rs - Load 32-bit from [Rs]
            cpu->R[rd] = mem_read32(cpu, cpu->R[rs]);
            update_zn(cpu, cpu->R[rd]);
            break;
            
        case 0x04:  // STR Rs, Rd - Store 32-bit to [Rd]
            mem_write32(cpu, cpu->R[rd], cpu->R[rs]);
            break;

        /* ===== TRANSFER & IMMEDIATE ===== */
        case 0x05:  // MOV Rd, Rs
            cpu->R[rd] = cpu->R[rs];
            update_zn(cpu, cpu->R[rd]);
            break;
            
        case 0x06:  // LI Rd, imm16
        {
            uint16_t imm = fetch16(cpu);
            cpu->R[rd] = imm;
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x60:  // LIL Rd, imm16 - Load Low (bits 0-15)
        {
            uint32_t imm = fetch16(cpu);
            cpu->R[rd] = (cpu->R[rd] & 0xFFFF0000) | (imm & 0xFFFF);
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x61:  // LIH Rd, imm16 - Load High (bits 16-31)
        {
            uint32_t imm = fetch16(cpu);
            // CORRECTION: Shift by 16 bits instead of 8
            cpu->R[rd] = (cpu->R[rd] & 0x0000FFFF) | (imm << 16); 
            update_zn(cpu, cpu->R[rd]);
        }
        break;

        /* ===== ARITHMETIC ===== */
        case 0x07:  // ADD Rd, Rs
        {
            uint32_t result = cpu->R[rd] + cpu->R[rs];
            cpu->Flags.C = result < cpu->R[rd];  // Carry on unsigned overflow
            cpu->Flags.V = calc_overflow_add(cpu->R[rd], cpu->R[rs], result);
            cpu->R[rd] = result;
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x08:  // SUB Rd, Rs
        {
            uint32_t result = cpu->R[rd] - cpu->R[rs];
            cpu->Flags.C = cpu->R[rs] > cpu->R[rd];  // Borrow
            cpu->Flags.V = calc_overflow_sub(cpu->R[rd], cpu->R[rs], result);
            cpu->R[rd] = result;
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x09:  // MUL Rd, Rs
        {
            uint64_t result = (uint64_t)cpu->R[rd] * (uint64_t)cpu->R[rs];
            cpu->Flags.C = (result >> 32) != 0;  // Carry on 32-bit overflow
            cpu->R[rd] = (uint32_t)result;
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x0A:  // DIV Rd, Rs
            if (cpu->R[rs] == 0) {
                cpu->div_error = true;
                cpu->Flags.V = true;  // Error signal
                printf("    [DIV_ERROR] Division by zero\n");
            } else {
                cpu->R[rd] /= cpu->R[rs];
                cpu->div_error = false;
                cpu->Flags.V = false;
                update_zn(cpu, cpu->R[rd]);
            }
            break;
            
        case 0x0B:  // MOD Rd, Rs
            if (cpu->R[rs] == 0) {
                cpu->div_error = true;
                cpu->Flags.V = true;
                printf("    [MOD_ERROR] Modulo by zero\n");
            } else {
                cpu->R[rd] %= cpu->R[rs];
                cpu->div_error = false;
                cpu->Flags.V = false;
                update_zn(cpu, cpu->R[rd]);
            }
            break;
            
        case 0x0C:  // INC Rd
        {
            uint32_t old = cpu->R[rd];
            cpu->R[rd]++;
            cpu->Flags.C = (cpu->R[rd] == 0);  // Wraparound
            cpu->Flags.V = calc_overflow_add(old, 1, cpu->R[rd]);
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x0D:  // DEC Rd
        {
            uint32_t old = cpu->R[rd];
            cpu->R[rd]--;
            cpu->Flags.C = (old == 0);  // Borrow
            cpu->Flags.V = calc_overflow_sub(old, 1, cpu->R[rd]);
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x0E:  // CMP Rd, Rs
        {
            uint32_t result = cpu->R[rd] - cpu->R[rs];
            cpu->Flags.C = cpu->R[rs] > cpu->R[rd];
            cpu->Flags.V = calc_overflow_sub(cpu->R[rd], cpu->R[rs], result);
            update_zn(cpu, result);
        }
        break;
        
        case 0x0F:  // ADDI Rd, imm16
        {
            uint16_t imm = fetch16(cpu);
            uint32_t result = cpu->R[rd] + imm;
            cpu->Flags.C = result < cpu->R[rd];
            cpu->Flags.V = calc_overflow_add(cpu->R[rd], imm, result);
            cpu->R[rd] = result;
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x14:  // SUBI Rd, imm16
        {
            uint16_t imm = fetch16(cpu);
            uint32_t result = cpu->R[rd] - imm;
            cpu->Flags.C = imm > cpu->R[rd];
            cpu->Flags.V = calc_overflow_sub(cpu->R[rd], imm, result);
            cpu->R[rd] = result;
            update_zn(cpu, cpu->R[rd]);
        }
        break;

        /* ===== LOGIC ===== */
        case 0x1B:  // AND Rd, Rs
            cpu->R[rd] &= cpu->R[rs];
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.C = false;
            cpu->Flags.V = false;
            break;
            
        case 0x1C:  // OR Rd, Rs
            cpu->R[rd] |= cpu->R[rs];
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.C = false;
            cpu->Flags.V = false;
            break;
            
        case 0x1D:  // XOR Rd, Rs
            cpu->R[rd] ^= cpu->R[rs];
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.C = false;
            cpu->Flags.V = false;
            break;
            
        case 0x1E:  // NOT Rd
            cpu->R[rd] = ~cpu->R[rd];
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.C = false;
            cpu->Flags.V = false;
            break;
            
        case 0x1F:  // SHL Rd, Rs (shift left)
        {
            // CORRECTION: Limit shift to max 31 bits (32-bit registers)
            uint32_t shift = cpu->R[rs] & 0x1F;  // 0-31 bits
            if (shift > 0) {
                // The last bit out is the bit that goes into carry
                cpu->Flags.C = ((cpu->R[rd] >> (32 - shift)) & 1) != 0;
                cpu->R[rd] <<= shift;
            } else {
                cpu->Flags.C = false;
            }
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.V = false;
        }
        break;
            
        case 0x21:  // SHR Rd, Rs (logical shift right)
        {
            // CORRECTION: Limit shift to max 31 bits (32-bit registers)
            uint32_t shift = cpu->R[rs] & 0x1F;  // 0-31 bits
            if (shift > 0) {
                // The last bit out is the LSB before shift
                cpu->Flags.C = ((cpu->R[rd] >> (shift - 1)) & 1) != 0;
                cpu->R[rd] >>= shift;
            } else {
                cpu->Flags.C = false;
            }
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.V = false;
        }
        break;

        /* ===== JUMPS (32-BIT ADDRESSES) ===== */
        case 0x10:  // JMP imm32
        {
            uint32_t addr = fetch32(cpu);
            #if DEBUG_JUMPS
            printf("    [JMP] PC: 0x%08X -> 0x%08X\n", pc_before, addr);
            #endif
            cpu->PC = addr;
        }
        break;
        
        case 0x11:  // JZ imm32 (Jump if Zero)
        {
            uint32_t addr = fetch32(cpu);
            if (cpu->Flags.Z) {
                #if DEBUG_JUMPS
                printf("    [JZ] TAKEN: PC: 0x%08X -> 0x%08X\n", pc_before, addr);
                #endif
                cpu->PC = addr;
            }
        }
        break;
        
        case 0x12:  // JNZ imm32 (Jump if Not Zero)
        {
            uint32_t addr = fetch32(cpu);
            if (!cpu->Flags.Z) {
                #if DEBUG_JUMPS
                printf("    [JNZ] TAKEN: PC: 0x%08X -> 0x%08X\n", pc_before, addr);
                #endif
                cpu->PC = addr;
            }
        }
        break;
        
        case 0x17:  // CALL imm32
        {
            uint32_t addr = fetch32(cpu);
            push32(cpu, cpu->PC);  // Save return address
            #if DEBUG_JUMPS
            printf("    [CALL] PC: 0x%08X -> 0x%08X (RET @ 0x%08X)\n", 
                   pc_before, addr, cpu->PC);
            #endif
            cpu->PC = addr;
        }
        break;
        
        case 0x18:  // RET
        {
            uint32_t ret_addr = pop32(cpu);
            #if DEBUG_JUMPS
            printf("    [RET] PC: 0x%08X -> 0x%08X\n", pc_before, ret_addr);
            #endif
            cpu->PC = ret_addr;
        }
        break;

        /* ===== STACK ===== */
        case 0x19:  // PUSH Rs
            push32(cpu, cpu->R[rs]);
            break;
            
        case 0x1A:  // POP Rd
            cpu->R[rd] = pop32(cpu);
            update_zn(cpu, cpu->R[rd]);
            break;

        /* ===== GPU ===== */
        case 0x51:  // VSYNC — Wait for next VBlank
            #if DEBUG_GPU_OPS
            printf("    [VSYNC] Suspended until next VBlank\n");
            #endif
            cpu->waiting_vblank = true;   // CPU suspended until NMI VBlank
            break;

        /* ===== MEMORY ===== */
        case 0x40:  // MCPY - Memory Copy (R1=src, R2=dst, R3=len)
        {
            uint32_t src = cpu->R[1];
            uint32_t dst = cpu->R[2];
            uint32_t len = cpu->R[3];
            
            #if DEBUG_MEMORY_ACCESS
            printf("    [MCPY] src=0x%08X dst=0x%08X len=%u\n", src, dst, len);
            #endif
            
            // CORRECTION: Use mem_read8/mem_write8 to support VRAM
            for (uint32_t i = 0; i < len; i++) {
                uint8_t byte = mem_read8(cpu, src + i);
                mem_write8(cpu, dst + i, byte);
            }
        }
        break;
        
        case 0x42:  // MSET - Memory Set (R1=val, R2=addr, R3=len)
        {
            uint8_t val = cpu->R[1] & 0xFF;
            uint32_t addr = cpu->R[2];
            uint32_t len = cpu->R[3];
            
            #if DEBUG_MEMORY_ACCESS
            printf("    [MSET] val=0x%02X addr=0x%08X len=%u\n", val, addr, len);
            #endif
            
            // CORRECTION: Use mem_write8 to support VRAM
            for (uint32_t i = 0; i < len; i++) {
                mem_write8(cpu, addr + i, val);
            }
        }
        break;

        /* ===== UNKNOWN INSTRUCTION ===== */
        default:
            printf("    [CPU_ERROR] Unknown opcode: 0x%02X @ PC=0x%08X\n", op, pc_before);
            cpu->halted = true;
            break;
    }
    
    cpu->total_cycles++;
}
