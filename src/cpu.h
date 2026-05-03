#ifndef PB010381_CPU_H
#define PB010381_CPU_H

#include <stdint.h>
#include <stdbool.h>
#include "gpu.h"
#include "apu.h"
#include "controller.h"

/* =========================================================
   ST32X MEMORY MAP - 32-bit Linear Addressing
   =========================================================
   
   0x00000000 - 0x0007FFFF : RAM (512 KB)
   0x00080000 - 0x000FFFFF : VRAM (512 KB)
   0x00100000 - 0x0010FFFF : I/O & MMIO (64 KB)
   0x00200000 - 0x03FFFFFF : ROM (~62 MB)
   
   Total: 64 MB addressable
========================================================= */

// RAM (512 KB)
#define RAM_START       0x00000000
#define RAM_END         0x0007FFFF
#define RAM_SIZE        0x00080000

// VRAM (512 KB)
#define VRAM_START      0x00080000
#define VRAM_END        0x000FFFFF
#define VRAM_SIZE       0x00080000

// I/O & MMIO (64 KB)
#define IO_START        0x00100000
#define IO_END          0x0010FFFF

// Controllers MMIO
#define CONTROLLER_MMIO_START  0x00100100
#define CONTROLLER_MMIO_END    0x001001FF

// GPU MMIO
#define GPU_MMIO_START  0x00100200
#define GPU_MMIO_END   	0x001057FF

// APU MMIO
#define APU_MMIO_START  0x00100800
#define APU_MMIO_END    0x00100FFF

// ROM (~62 MB)
#define ROM_START       0x00200000
#define ROM_END         0x03FFFFFF
#define ROM_SIZE        0x03E00000

// Total memory size
#define MEM_SIZE        0x04000000  // 64 MB

/* =========================================================
   CPU STRUCTURE
========================================================= */

typedef struct {
    uint32_t R[16];   // Registres R0-R15
    uint32_t PC;      // Program Counter
    
    struct {
        bool Z, N, C, V;
    } Flags;
    
    bool halted;
    bool div_error;   // Flag d'erreur division par zéro
    uint64_t total_cycles;
    
    uint8_t memory[MEM_SIZE];
    
    // Périphériques
    PB010381_GPU *gpu;
    PB010381_APU *apu;
    PB010381_Controllers *controllers;
} PB010381_CPU;

/* =========================================================
   FUNCTION PROTOTYPES
========================================================= */

void cpu_step(PB010381_CPU *cpu);
uint16_t mem_read16(PB010381_CPU *cpu, uint32_t addr);
void mem_write16(PB010381_CPU *cpu, uint32_t addr, uint16_t val);
uint32_t mem_read32(PB010381_CPU *cpu, uint32_t addr);
void mem_write32(PB010381_CPU *cpu, uint32_t addr, uint32_t val);

#endif // PB010381_CPU_H