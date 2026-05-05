/* =========================================================
Copyright (C) 2026 - Peneaux Benjamin 
This program is free software; 
you may redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; 
either version 3 of the license.
- GNU GENERAL PUBLIC LICENSE V3 -
========================================================= */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Windows/SDL2 specific include
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "cpu.h"

#define WINDOW_SCALE  2

// SDL audio callback
void audio_callback(void* userdata, Uint8* stream, int len) {
    PB010381_APU *apu = (PB010381_APU*)userdata;
    apu_generate_samples(apu, (int16_t*)stream, len / 4);  // len / 4 because stereo 16-bit
}

/* ==========================================
   SYMBOL TABLE READER
   Reads the .sym file produced by the assembler
   and returns the address of a given label (or 0 if not found).
========================================== */
static uint32_t read_symbol(const char* sym_file, const char* label_name) {
    FILE* f = fopen(sym_file, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;  // skip comments/blank

        char name[128];
        uint32_t addr = 0;
        // Format: "label_name 0xABCDEF01"
        if (sscanf(line, "%127s 0x%X", name, &addr) == 2) {
            if (strcmp(name, label_name) == 0) {
                fclose(f);
                return addr;
            }
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char* argv[]) {
    // Manual initialization if necessary for MSYS2
    SDL_SetMainReady();

    printf("========================================\n");
    printf("ST32X Fantasy Console - 32-bit Linear\n");
    printf("========================================\n\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        printf("[SDL][ERROR] %s\n", SDL_GetError());
        return 1;
    }
    printf("[SDL] Subsystems initialized.\n");

    // ==========================================
    // DEVICE ALLOCATION & INITIALIZATION
    // ==========================================
    
    PB010381_CPU *cpu = malloc(sizeof(PB010381_CPU));
    PB010381_GPU *gpu = malloc(sizeof(PB010381_GPU));
    PB010381_APU *apu = malloc(sizeof(PB010381_APU));
    PB010381_Controllers *controllers = malloc(sizeof(PB010381_Controllers));
    
    if (!cpu || !gpu || !apu || !controllers) {
        printf("[ERROR] Memory allocation failed!\n");
        return 1;
    }

    // CRITICAL: Zero out entire CPU structure
    memset(cpu, 0, sizeof(PB010381_CPU));
    
    // GPU initialization
    gpu_init(gpu);
    cpu->gpu = gpu;
    
    // APU initialization
    apu_init(apu);
    cpu->apu = apu;
    apu->system_ram = cpu->memory;
    
    // Controllers initialization
    controllers_init(controllers);
    cpu->controllers = controllers;

    // === CPU CONFIGURATION - 32-BIT ADDRESSING ===
    printf("[CPU] Configuration (32-bit linear addressing):\n");
    
    // NEW MEMORY MAP:
    // - RAM : 0x00000000 - 0x0007FFFF (512 KB)
    // - ROM : 0x00200000 - 0x03FFFFFF (~62 MB)
    cpu->PC = ROM_START;              // Program starts at 0x00200000 (ROM)
    cpu->R[15] = RAM_END - 3;         // Stack Pointer at top of RAM (4-byte aligned)
    cpu->halted = false;              // CPU active
    
    printf("      PC (32-bit) = 0x%08X (ROM Start)\n", cpu->PC);
    printf("      SP (R15)    = 0x%08X (RAM Top, aligned)\n", cpu->R[15]);
    printf("      Halted      = %s\n", cpu->halted ? "true" : "false");
    printf("      Memory Size = %d MB (0x%08X bytes)\n\n", 
           MEM_SIZE / (1024*1024), MEM_SIZE);

    // Create window with default resolution (4:3)
    int window_width = SCREEN_WIDTH_4_3 * WINDOW_SCALE;
    int window_height = SCREEN_HEIGHT * WINDOW_SCALE;
    
    SDL_Window* window = SDL_CreateWindow(
        "ST32X game test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_width, window_height, 
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("[SDL][ERROR] Window creation failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        400, 240 
    );

    printf("[SDL] Window and renderer created.\n\n");

    // === AUDIO SDL CONFIGURATION ===
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = APU_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = APU_BUFFER_SIZE;
    want.callback = audio_callback;
    want.userdata = apu;
    
    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device == 0) {
        printf("[AUDIO][WARN] Cannot open audio device: %s\n", SDL_GetError());
    } else {
        printf("[AUDIO] Audio device opened:\n");
        printf("        Frequency: %d Hz\n", have.freq);
        printf("        Channels: %d\n", have.channels);
        printf("        Buffer: %d samples\n", have.samples);
        SDL_PauseAudioDevice(audio_device, 0);  // Start playback
    }

    // ==========================================
    // ROM LOADING
    // ==========================================
    
    FILE *f = fopen("output.bin", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t rom_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        // Load directly to ROM address (0x00200000)
        // Calculate offset in memory[]
        uint32_t rom_offset = ROM_START - RAM_START;
        size_t bytes_read = fread(&cpu->memory[rom_offset], 1, rom_size, f);
        fclose(f);
        
        printf("[ROM] Load successful:\n");
        printf("      File          = output.bin\n");
        printf("      Size          = %zu bytes (0x%zX)\n", rom_size, rom_size);
        printf("      Target address = 0x%08X (ROM)\n", ROM_START);
        printf("      Memory offset = 0x%08X\n", rom_offset);
        printf("      Bytes read    = %zu\n\n", bytes_read);
        
        // Verification: display first 16 bytes
        printf("[ROM] First bytes (hexdump @ 0x%08X):\n      ", ROM_START);
        for (int i = 0; i < 16; i++) {
            printf("%02X ", cpu->memory[rom_offset + i]);
            if (i == 7) printf("\n      ");
        }
        printf("\n\n");
        
        // Decode first instruction
        uint16_t first_instr = (cpu->memory[rom_offset] << 8) | 
                               cpu->memory[rom_offset + 1];
        uint8_t first_op = (first_instr >> 8) & 0xFF;
        uint8_t first_rd = (first_instr >> 4) & 0x0F;
        uint8_t first_rs = first_instr & 0x0F;
        
        printf("[ROM] First instruction decoded:\n");
        printf("      Bytes  = %02X %02X\n", 
               cpu->memory[rom_offset], cpu->memory[rom_offset + 1]);
        printf("      Header = 0x%04X\n", first_instr);
        printf("      Opcode = 0x%02X\n", first_op);
        printf("      Rd     = R%d\n", first_rd);
        printf("      Rs     = R%d\n\n", first_rs);
        
    } else {
        printf("[ROM][ERROR] output.bin not found!\n");
        printf("      Compile first with: ./assembler input.asm output.bin\n\n");
        return 1;
    }

    // ==========================================
    // MEMORY VERIFICATION
    // ==========================================
    
    printf("[MEM] Memory map (new organization):\n");
    printf("      RAM:         0x%08X - 0x%08X (%d KB)\n", 
           RAM_START, RAM_END, RAM_SIZE / 1024);
    printf("      VRAM:        0x%08X - 0x%08X (%d KB)\n", 
           VRAM_START, VRAM_END, VRAM_SIZE / 1024);
    printf("      I/O:         0x%08X - 0x%08X\n", 
           IO_START, IO_END);
    printf("      Controllers: 0x%08X - 0x%08X\n", 
           CONTROLLER_MMIO_START, CONTROLLER_MMIO_END);
    printf("      GPU:         0x%08X - 0x%08X\n", 
           GPU_MMIO_START, GPU_MMIO_END);
    printf("      APU:         0x%08X - 0x%08X\n", 
           APU_MMIO_START, APU_MMIO_END);
    printf("      ROM:         0x%08X - 0x%08X (~%d MB)\n\n", 
           ROM_START, ROM_END, ROM_SIZE / (1024*1024));

	// === NMI VECTOR INSTALLATION ===
	// Read nmi_handler address from the .sym file produced by the assembler.
	// This is robust to any code size change — no more hardcoded addresses.
	uint32_t nmi_addr = read_symbol("output.sym", "nmi_handler");
	if (nmi_addr == 0) {
		printf("[NMI][ERROR] 'nmi_handler' not found in output.sym!\n");
		printf("             Assemble first: ./assembler input.asm output.bin\n");
		return 1;
	}
	printf("[NMI] Symbol 'nmi_handler' resolved to 0x%08X\n", nmi_addr);
	// Write 32-bit vector to RAM (big-endian, 2 x 16-bit)
	cpu->memory[0x10] = (nmi_addr >> 24) & 0xFF;
	cpu->memory[0x11] = (nmi_addr >> 16) & 0xFF;
	cpu->memory[0x12] = (nmi_addr >>  8) & 0xFF;
	cpu->memory[0x13] = (nmi_addr >>  0) & 0xFF;
	printf("[NMI] Vector installed @ 0x%08X\n", nmi_addr);
	printf("[NMI] Handler content (first 32 bytes) :\n");
	uint32_t base = nmi_addr - RAM_START;   // address to memory offset conversion
	for (int i = 0; i < 32; i++) {
		printf("%02X ", cpu->memory[base + i]);
		if ((i + 1) % 16 == 0) printf("\n");
	}
	printf("\n");

    // ==========================================
    // MAIN LOOP
    // ==========================================
    
    printf("========================================\n");
    printf("Starting emulation...\n");
    printf("========================================\n\n");

    bool running = true;
    SDL_Event ev;
    uint64_t frame_count = 0;
    uint32_t last_pc = 0;
    int stuck_counter = 0;
    uint8_t last_video_mode = gpu->video_mode;

    while (running) {
        // SDL event handling
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                printf("\n[QUIT] User requested close.\n");
                running = false;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                printf("\n[QUIT] ESC key pressed.\n");
                running = false;
            }
            
            // Controller handling
            if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                controller_connect(controllers, ev.cdevice.which);
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                // Find which controller was disconnected
                for (int i = 0; i < MAX_CONTROLLERS; i++) {
                    if (controllers->controllers[i].connected && 
                        SDL_JoystickInstanceID(controllers->controllers[i].sdl_joystick) == ev.cdevice.which) {
                        controller_disconnect(controllers, i);
                        break;
                    }
                }
            }
        }
        
        // Update controllers
        controllers_update(controllers);

        // CPU execution (~1000 cycles per frame for ~10 main_loop iterations)
        // This enables smooth scrolling without scroll_x becoming too large
		for (int i = 0; i < 1000 && !cpu->halted; i++) {
			if (cpu->waiting_vblank) break;   // Don't execute if waiting for VBlank
			cpu_step(cpu);
            
            // Infinite loop detection
            if (cpu->PC == last_pc) {
                stuck_counter++;
                if (stuck_counter > 100) {
                    printf("\n[CPU][WARN] Infinite loop detected at PC=0x%08X\n", cpu->PC);
                    printf("            Stopping emulation.\n");
                    cpu->halted = true;
                    break;
                }
            } else {
                stuck_counter = 0;
            }
            last_pc = cpu->PC;
        }
        
        // Simulate a complete frame (224+ scanlines)
        for (int scanline = 0; scanline < 262; scanline++) {
            gpu_step(gpu);
        }
        
        // Render the frame
        gpu_render_frame(gpu);

		// === NMI VBLANK TRIGGER ===
		// Only if CPU actually waiting for VBlank (VSYNC executed)
		// Without this guard, NMI runs during init and corrupts registers
		if (cpu->waiting_vblank) {
			// Reinstall NMI vector each frame (init_system wipes RAM 0x00–0x3FFF with MSET,
			// which includes the NMI vector at 0x0010).
			// nmi_addr was resolved from output.sym at startup — no hardcoded address.
			cpu->memory[0x10] = (nmi_addr >> 24) & 0xFF;
			cpu->memory[0x11] = (nmi_addr >> 16) & 0xFF;
			cpu->memory[0x12] = (nmi_addr >>  8) & 0xFF;
			cpu->memory[0x13] = (nmi_addr >>  0) & 0xFF;

			cpu->nmi_pending    = true;
			cpu->waiting_vblank = false;
		}

        // Check if video mode changed and adjust window
        if (gpu->video_mode != last_video_mode) {
            last_video_mode = gpu->video_mode;
            int new_width = (gpu->video_mode == 1 ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3) * WINDOW_SCALE;
            int new_height = SCREEN_HEIGHT * WINDOW_SCALE;
            SDL_SetWindowSize(window, new_width, new_height);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            printf("[GPU] Video mode changed: %s\n", gpu->video_mode ? "16:9" : "4:3");
        }

        // Display using framebuffer
        // Framebuffer has fixed width of 400 pixels (SCREEN_WIDTH_16_9)
        // Pitch must always match this width
        int pitch = SCREEN_WIDTH_16_9 * 4;  // 400 * 4 = 1600 bytes per line
        
        
        // Update entire texture with framebuffer
        SDL_UpdateTexture(texture, NULL, gpu->framebuffer, pitch);
        
        // Set source region to copy (only active portion)
        int screen_width = (gpu->video_mode == 1) ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3;
        SDL_Rect src_rect;
        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.w = screen_width;  // 320 in 4:3, 400 in 16:9
        src_rect.h = SCREEN_HEIGHT;  // 224
        
        // Render: copy only active portion of texture to window
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, &src_rect, NULL);
        SDL_RenderPresent(renderer);

        // HALT verification
        if (cpu->halted) {
            printf("\n========================================\n");
            printf("CPU HALTED\n");
            printf("========================================\n");
            printf("PC final       : 0x%08X\n", cpu->PC);
            printf("Total cycles   : %llu\n", (unsigned long long)cpu->total_cycles);
            printf("Total frames   : %llu\n", (unsigned long long)frame_count);
            printf("\n[CPU] Register state:\n");
            for (int i = 0; i < 16; i++) {
                printf("      R%-2d = 0x%08X", i, cpu->R[i]);
                if (i == 15) printf(" (SP)");
                if (i == 14) printf(" (FP)");
                printf("\n");
            }
            printf("\n[CPU] Flags:\n");
            printf("      Z = %d (Zero)\n", cpu->Flags.Z);
            printf("      N = %d (Negative)\n", cpu->Flags.N);
            printf("      C = %d (Carry)\n", cpu->Flags.C);
            printf("      V = %d (Overflow)\n\n", cpu->Flags.V);
            
            // Wait 3 seconds before closing
            SDL_Delay(3000);
            running = false;
        }
        
        frame_count++;
        SDL_Delay(16); // ~60 FPS
    }

    // ==========================================
    // CLEANUP
    // ==========================================
    
    printf("========================================\n");
    printf("Stopping emulation\n");
    printf("========================================\n");
    printf("Total frames rendered: %llu\n", (unsigned long long)frame_count);
    printf("Total cycles executed: %llu\n\n", (unsigned long long)cpu->total_cycles);
    
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
    }
    
    controllers_shutdown(controllers);
    
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    free(cpu);
    free(gpu);
    free(apu);
    free(controllers);
    
    printf("Cleanup complete. Goodbye!\n");
    
    return 0;
}
