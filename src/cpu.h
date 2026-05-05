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

// Interrupt Controller MMIO (0x00100000 - 0x0010000F)
// Occupies unused IO space before controllers
#define INT_MMIO_START  0x00100000
#define INT_MMIO_END    0x0010000F
// 0x00100000 : INT_CTRL   — bit0=IRQ enable, bit1=NMI enable
// 0x00100002 : INT_STATUS — bit0=IRQ active, bit1=NMI active (write 0 to clear)

// Interrupt vectors (fixed addresses in RAM)
#define NMI_VECTOR_ADDR  0x00000010  // 4 bytes: 32-bit NMI handler address
#define IRQ_VECTOR_ADDR  0x00000014  // 4 bytes: 32-bit IRQ handler address

// GPU MMIO
#define GPU_MMIO_START  0x00100200
#define GPU_MMIO_END  	0x001057FF

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
    uint32_t R[16];   // Registers R0-R15
    uint32_t PC;      // Program Counter
    
    struct {
        bool Z, N, C, V;
    } Flags;
    
    bool halted;
    bool div_error;   // Division by zero error flag
	
	// === INTERRUPT SYSTEM ===
	bool irq_enabled;     // Flag I: Maskable IRQs enabled (SEI/CLI)
	bool irq_pending;     // An IRQ waiting to be serviced
	bool nmi_pending;     // An NMI (VBlank) waiting to be serviced
	bool waiting_vblank;  // VSYNC: CPU suspended until next VBlank
	
    uint64_t total_cycles;
    
    uint8_t memory[MEM_SIZE];
    
    // Peripherals
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
