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

/* ================= CONFIGURATION DEBUG ================= */

// Activer/désactiver les différents niveaux de debug
#define DEBUG_CPU_STEPS     1    // Afficher chaque instruction
#define DEBUG_MEMORY_ACCESS 1    // Afficher les accès mémoire (verbeux - désactivé par défaut)
#define DEBUG_STACK         1    // Afficher les opérations pile
#define DEBUG_GPU_OPS       1    // Afficher les opérations GPU (désactivé par défaut)
#define DEBUG_JUMPS         1    // Afficher les sauts/appels
#define DEBUG_ALIGNMENT     1    // Afficher les warnings d'alignement

/* ================= ACCÈS MÉMOIRE UNIFIÉ ================= */

/**
 * CORRECTION: Fonctions unifiées d'accès mémoire 8-bit
 * Toutes les fonctions MCPY, DMA, MSET passent par ici
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
    
    // Adresse invalide
    return 0xFF;
}

void mem_write8(PB010381_CPU *cpu, uint32_t addr, uint8_t val) {
    // === Protection ROM ===
    if (addr >= ROM_START && addr <= ROM_END) {
        // Ignorer les écritures en ROM
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

/* ================= ACCÈS MÉMOIRE 16-BIT ================= */

/**
 * Lecture 16 bits depuis la mémoire
 * Utilise mem_read8 pour cohérence
 * CORRECTION: Alignement forcé sur 2 octets pour éviter les bugs
 */
uint16_t mem_read16(PB010381_CPU *cpu, uint32_t addr) {
    // CRITIQUE: Alignement sur mot pair pour garantir la cohérence
    if (addr & 1) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Lecture 16-bit non-alignée @ 0x%08X -> aligné à 0x%08X\n", 
               addr, addr & ~1);
        #endif
        addr &= ~1;
    }
    
    uint16_t val = 0;
    
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
    
    // === Mémoire normale (via mem_read8 unifié) ===
    val = (mem_read8(cpu, addr) << 8) | mem_read8(cpu, addr + 1);
    
    #if DEBUG_MEMORY_ACCESS
    printf("    [MEM_READ16] 0x%08X => 0x%04X\n", addr, val);
    #endif
    
    return val;
}

/**
 * Écriture 16 bits en mémoire
 * Utilise mem_write8 pour cohérence
 * CORRECTION: Alignement forcé sur 2 octets
 */
void mem_write16(PB010381_CPU *cpu, uint32_t addr, uint16_t val) {
    // CRITIQUE: Alignement sur mot pair pour garantir la cohérence
    if (addr & 1) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Écriture 16-bit non-alignée @ 0x%08X -> aligné à 0x%08X\n", 
               addr, addr & ~1);
        #endif
        addr &= ~1;
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
    
    // === Mémoire normale (via mem_write8 unifié) ===
    mem_write8(cpu, addr, (val >> 8) & 0xFF);
    mem_write8(cpu, addr + 1, val & 0xFF);
    
    #if DEBUG_MEMORY_ACCESS
    printf("    [MEM_WRITE16] 0x%08X <= 0x%04X\n", addr, val);
    #endif
}

/**
 * Lecture 32 bits depuis la mémoire
 * CORRECTION: Alignement forcé sur 4 octets
 */
uint32_t mem_read32(PB010381_CPU *cpu, uint32_t addr) {
    // CRITIQUE: Alignement sur 4 octets pour les accès 32-bit
    if (addr & 3) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Lecture 32-bit non-alignée @ 0x%08X -> aligné à 0x%08X\n", 
               addr, addr & ~3);
        #endif
        addr &= ~3;
    }
    
    uint32_t high = mem_read16(cpu, addr);
    uint32_t low  = mem_read16(cpu, addr + 2);
    return (high << 16) | low;
}

/**
 * Écriture 32 bits en mémoire
 * CORRECTION: Alignement forcé sur 4 octets
 */
void mem_write32(PB010381_CPU *cpu, uint32_t addr, uint32_t val) {
    // CRITIQUE: Alignement sur 4 octets pour les accès 32-bit
    if (addr & 3) {
        #if DEBUG_ALIGNMENT
        printf("    [MEM_WARN] Écriture 32-bit non-alignée @ 0x%08X -> aligné à 0x%08X\n", 
               addr, addr & ~3);
        #endif
        addr &= ~3;
    }
    
    mem_write16(cpu, addr,     (val >> 16) & 0xFFFF);
    mem_write16(cpu, addr + 2, val & 0xFFFF);
}

/* ================= UTILITAIRES ================= */

static inline uint16_t fetch16(PB010381_CPU *cpu) {
    uint16_t val = mem_read16(cpu, cpu->PC);
    cpu->PC += 2;
    return val;
}

static inline uint32_t fetch32(PB010381_CPU *cpu) {
    // CRITIQUE: Ne PAS forcer l'alignement pour les immédiat du code !
    // Les immédiat 32-bit suivent un header 16-bit, donc ne sont jamais alignés sur 4.
    uint32_t addr = cpu->PC;
    uint32_t val = (mem_read8(cpu, addr) << 24) |
                   (mem_read8(cpu, addr + 1) << 16) |
                   (mem_read8(cpu, addr + 2) << 8) |
                    mem_read8(cpu, addr + 3);
    cpu->PC += 4;
    return val;
}

/**
 * CORRECTION: PUSH/POP 32-bit pour cohérence
 * Les registres sont 32-bit, donc PUSH/POP doivent l'être aussi
 */
static inline void push32(PB010381_CPU *cpu, uint32_t val) {
    cpu->R[15] -= 4;  // SP décrémente de 4 (alignement 32-bit)
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

/* Garder push16/pop16 pour compatibilité si nécessaire */
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
 * CORRECTION: Calcul correct de l'overflow signé
 */
static inline bool calc_overflow_add(uint32_t a, uint32_t b, uint32_t result) {
    // Overflow si : (a et b même signe) ET (résultat signe différent)
    bool a_sign = (a >> 31) & 1;
    bool b_sign = (b >> 31) & 1;
    bool r_sign = (result >> 31) & 1;
    return (a_sign == b_sign) && (a_sign != r_sign);
}

static inline bool calc_overflow_sub(uint32_t a, uint32_t b, uint32_t result) {
    // Overflow si : (a et b signes différents) ET (résultat signe != a)
    bool a_sign = (a >> 31) & 1;
    bool b_sign = (b >> 31) & 1;
    bool r_sign = (result >> 31) & 1;
    return (a_sign != b_sign) && (a_sign != r_sign);
}

static inline void update_zn(PB010381_CPU *cpu, uint32_t val) {
    cpu->Flags.Z = (val == 0);
    cpu->Flags.N = (val >> 31) & 1;
}

/* ================= BOUCLE CPU ================= */

void cpu_step(PB010381_CPU *cpu) {
    if (cpu->halted) return;
    
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
        /* ===== CONTRÔLE ===== */
        case 0x00:  // HALT
            printf("    [HALT] Arrêt du CPU\n");
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

        /* ===== CHARGEMENT/SAUVEGARDE AVEC ADRESSES 32-BIT ===== */
        
        /* Instructions 8-bit - NOUVELLES */
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
        
        /* Instructions 16-bit */
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
        
        /* Instructions 32-bit */
        case 0x03:  // LDR Rd, Rs - Load 32-bit from [Rs]
            cpu->R[rd] = mem_read32(cpu, cpu->R[rs]);
            update_zn(cpu, cpu->R[rd]);
            break;
            
        case 0x04:  // STR Rs, Rd - Store 32-bit to [Rd]
            mem_write32(cpu, cpu->R[rd], cpu->R[rs]);
            break;

        /* ===== TRANSFERT & IMMÉDIAT ===== */
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
            // CORRECTION: Décalage de 16 bits au lieu de 8
            cpu->R[rd] = (cpu->R[rd] & 0x0000FFFF) | (imm << 16); 
            update_zn(cpu, cpu->R[rd]);
        }
        break;

        /* ===== ARITHMÉTIQUE ===== */
        case 0x07:  // ADD Rd, Rs
        {
            uint32_t result = cpu->R[rd] + cpu->R[rs];
            cpu->Flags.C = result < cpu->R[rd];  // Carry sur dépassement non-signé
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
            cpu->Flags.C = (result >> 32) != 0;  // Carry si débordement 32-bit
            cpu->R[rd] = (uint32_t)result;
            update_zn(cpu, cpu->R[rd]);
        }
        break;
        
        case 0x0A:  // DIV Rd, Rs
            if (cpu->R[rs] == 0) {
                cpu->div_error = true;
                cpu->Flags.V = true;  // Signal d'erreur
                printf("    [DIV_ERROR] Division par zéro\n");
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
                printf("    [MOD_ERROR] Modulo par zéro\n");
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

        /* ===== LOGIQUE ===== */
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
            // CORRECTION: Limiter le shift à 31 bits max (registres 32-bit)
            uint32_t shift = cpu->R[rs] & 0x1F;  // 0-31 bits
            if (shift > 0) {
                // Le dernier bit sorti est le bit qui passe dans le carry
                cpu->Flags.C = ((cpu->R[rd] >> (32 - shift)) & 1) != 0;
                cpu->R[rd] <<= shift;
            } else {
                cpu->Flags.C = false;
            }
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.V = false;
        }
        break;
            
        case 0x21:  // SHR Rd, Rs (shift right logique)
        {
            // CORRECTION: Limiter le shift à 31 bits max (registres 32-bit)
            uint32_t shift = cpu->R[rs] & 0x1F;  // 0-31 bits
            if (shift > 0) {
                // Le dernier bit sorti est le LSB avant le shift
                cpu->Flags.C = ((cpu->R[rd] >> (shift - 1)) & 1) != 0;
                cpu->R[rd] >>= shift;
            } else {
                cpu->Flags.C = false;
            }
            update_zn(cpu, cpu->R[rd]);
            cpu->Flags.V = false;
        }
        break;

        /* ===== SAUTS (ADRESSES 32-BIT) ===== */
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
                printf("    [JZ] PRIS: PC: 0x%08X -> 0x%08X\n", pc_before, addr);
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
                printf("    [JNZ] PRIS: PC: 0x%08X -> 0x%08X\n", pc_before, addr);
                #endif
                cpu->PC = addr;
            }
        }
        break;
        
        case 0x17:  // CALL imm32
        {
            uint32_t addr = fetch32(cpu);
            push32(cpu, cpu->PC);  // Sauvegarder l'adresse de retour
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

        /* ===== PILE ===== */
        case 0x19:  // PUSH Rs
            push32(cpu, cpu->R[rs]);
            break;
            
        case 0x1A:  // POP Rd
            cpu->R[rd] = pop32(cpu);
            update_zn(cpu, cpu->R[rd]);
            break;

        /* ===== GPU ===== */
        case 0x51:  // VSYNC
            // Attendre le VBlank (simulé par une courte pause)
            #if DEBUG_GPU_OPS
            printf("    [VSYNC] Attente VBlank\n");
            #endif
            break;

        /* ===== MÉMOIRE ===== */
        case 0x40:  // MCPY - Memory Copy (R1=src, R2=dst, R3=len)
        {
            uint32_t src = cpu->R[1];
            uint32_t dst = cpu->R[2];
            uint32_t len = cpu->R[3];
            
            #if DEBUG_MEMORY_ACCESS
            printf("    [MCPY] src=0x%08X dst=0x%08X len=%u\n", src, dst, len);
            #endif
            
            // CORRECTION: Utiliser mem_read8/mem_write8 pour supporter VRAM
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
            
            // CORRECTION: Utiliser mem_write8 pour supporter VRAM
            for (uint32_t i = 0; i < len; i++) {
                mem_write8(cpu, addr + i, val);
            }
        }
        break;

        /* ===== INSTRUCTION INCONNUE ===== */
        default:
            printf("    [CPU_ERROR] Opcode inconnu: 0x%02X @ PC=0x%08X\n", op, pc_before);
            cpu->halted = true;
            break;
    }
    
    cpu->total_cycles++;
}