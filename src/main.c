#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Inclusion spécifique pour Windows/SDL2
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "cpu.h"

#define WINDOW_SCALE  2

// Callback audio SDL
void audio_callback(void* userdata, Uint8* stream, int len) {
    PB010381_APU *apu = (PB010381_APU*)userdata;
    apu_generate_samples(apu, (int16_t*)stream, len / 4);  // len / 4 car stéréo 16-bit
}

int main(int argc, char* argv[]) {
    // Initialisation manuelle si nécessaire pour MSYS2
    SDL_SetMainReady();

    printf("========================================\n");
    printf("ST32X Fantasy Console - 32-bit Linear\n");
    printf("========================================\n\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        printf("[SDL][ERROR] %s\n", SDL_GetError());
        return 1;
    }
    printf("[SDL] Sous-systèmes initialisés.\n");

    // ==========================================
    // ALLOCATION & INITIALISATION MATÉRIEL
    // ==========================================
    
    PB010381_CPU *cpu = malloc(sizeof(PB010381_CPU));
    PB010381_GPU *gpu = malloc(sizeof(PB010381_GPU));
    PB010381_APU *apu = malloc(sizeof(PB010381_APU));
    PB010381_Controllers *controllers = malloc(sizeof(PB010381_Controllers));
    
    if (!cpu || !gpu || !apu || !controllers) {
        printf("[ERROR] Allocation mémoire échouée!\n");
        return 1;
    }

    // CRITIQUE : Mettre toute la structure CPU à zéro
    memset(cpu, 0, sizeof(PB010381_CPU));
    
    // Initialisation GPU
    gpu_init(gpu);
    cpu->gpu = gpu;
    
    // Initialisation APU
    apu_init(apu);
    cpu->apu = apu;
    apu->system_ram = cpu->memory;
    
    // Initialisation Controllers
    controllers_init(controllers);
    cpu->controllers = controllers;

    // === CONFIGURATION CPU - ADRESSAGE 32 BITS ===
    printf("[CPU] Configuration (32-bit linear addressing):\n");
    
    // NOUVELLE CARTE MÉMOIRE :
    // - RAM : 0x00000000 - 0x0007FFFF (512 KB)
    // - ROM : 0x00200000 - 0x03FFFFFF (~62 MB)
    cpu->PC = ROM_START;              // Programme commence à 0x00200000 (ROM)
    cpu->R[15] = RAM_END - 3;         // Stack Pointer au sommet de la RAM (aligné sur 4 bytes)
    cpu->halted = false;              // CPU actif
    
    printf("      PC (32-bit) = 0x%08X (ROM Start)\n", cpu->PC);
    printf("      SP (R15)    = 0x%08X (RAM Top, aligned)\n", cpu->R[15]);
    printf("      Halted      = %s\n", cpu->halted ? "true" : "false");
    printf("      Memory Size = %d MB (0x%08X bytes)\n\n", 
           MEM_SIZE / (1024*1024), MEM_SIZE);

    // Créer fenêtre avec la résolution par défaut (4:3)
    int window_width = SCREEN_WIDTH_4_3 * WINDOW_SCALE;
    int window_height = SCREEN_HEIGHT * WINDOW_SCALE;
    
    SDL_Window* window = SDL_CreateWindow(
        "ST16X Fantasy Console - v5.0 (512KB RAM/VRAM)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_width, window_height, 
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("[SDL][ERROR] Fenêtre impossible : %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        400, 240 
    );

    printf("[SDL] Fenêtre et renderer créés.\n\n");

    // === CONFIGURATION AUDIO SDL ===
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
        printf("[AUDIO][WARN] Impossible d'ouvrir le périphérique audio: %s\n", SDL_GetError());
    } else {
        printf("[AUDIO] Périphérique audio ouvert:\n");
        printf("        Fréquence: %d Hz\n", have.freq);
        printf("        Canaux: %d\n", have.channels);
        printf("        Buffer: %d samples\n", have.samples);
        SDL_PauseAudioDevice(audio_device, 0);  // Démarrer la lecture
    }

    // ==========================================
    // CHARGEMENT DE LA ROM
    // ==========================================
    
    FILE *f = fopen("output.bin", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        size_t rom_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        // Charger directement à l'adresse ROM (0x00200000)
        // Calcul de l'offset dans memory[]
        uint32_t rom_offset = ROM_START - RAM_START;
        size_t bytes_read = fread(&cpu->memory[rom_offset], 1, rom_size, f);
        fclose(f);
        
        printf("[ROM] Chargement réussi:\n");
        printf("      Fichier       = output.bin\n");
        printf("      Taille        = %zu octets (0x%zX)\n", rom_size, rom_size);
        printf("      Adresse cible = 0x%08X (ROM)\n", ROM_START);
        printf("      Offset mémoire= 0x%08X\n", rom_offset);
        printf("      Octets lus    = %zu\n\n", bytes_read);
        
        // Vérification : afficher les 16 premiers octets
        printf("[ROM] Premiers octets (hexdump @ 0x%08X):\n      ", ROM_START);
        for (int i = 0; i < 16; i++) {
            printf("%02X ", cpu->memory[rom_offset + i]);
            if (i == 7) printf("\n      ");
        }
        printf("\n\n");
        
        // Décoder la première instruction
        uint16_t first_instr = (cpu->memory[rom_offset] << 8) | 
                               cpu->memory[rom_offset + 1];
        uint8_t first_op = (first_instr >> 8) & 0xFF;
        uint8_t first_rd = (first_instr >> 4) & 0x0F;
        uint8_t first_rs = first_instr & 0x0F;
        
        printf("[ROM] Première instruction décodée:\n");
        printf("      Bytes  = %02X %02X\n", 
               cpu->memory[rom_offset], cpu->memory[rom_offset + 1]);
        printf("      Header = 0x%04X\n", first_instr);
        printf("      Opcode = 0x%02X\n", first_op);
        printf("      Rd     = R%d\n", first_rd);
        printf("      Rs     = R%d\n\n", first_rs);
        
    } else {
        printf("[ROM][ERREUR] output.bin introuvable!\n");
        printf("      Compilez d'abord avec : ./assembler input.asm output.bin\n\n");
        return 1;
    }

    // ==========================================
    // VÉRIFICATIONS MÉMOIRE
    // ==========================================
    
    printf("[MEM] Carte mémoire (nouvelle organisation):\n");
    printf("      RAM:        0x%08X - 0x%08X (%d KB)\n", 
           RAM_START, RAM_END, RAM_SIZE / 1024);
    printf("      VRAM:       0x%08X - 0x%08X (%d KB)\n", 
           VRAM_START, VRAM_END, VRAM_SIZE / 1024);
    printf("      I/O:        0x%08X - 0x%08X\n", 
           IO_START, IO_END);
    printf("      Controllers:0x%08X - 0x%08X\n", 
           CONTROLLER_MMIO_START, CONTROLLER_MMIO_END);
    printf("      GPU:        0x%08X - 0x%08X\n", 
           GPU_MMIO_START, GPU_MMIO_END);
    printf("      APU:        0x%08X - 0x%08X\n", 
           APU_MMIO_START, APU_MMIO_END);
    printf("      ROM:        0x%08X - 0x%08X (~%d MB)\n\n", 
           ROM_START, ROM_END, ROM_SIZE / (1024*1024));

	// === INSTALLATION DU VECTEUR NMI ===
	// Chercher l'adresse de nmi_handler dans la ROM chargée.
	// nmi_handler est le label qui suit setup_apu dans input.asm.
	// D'après les logs d'assemblage (passe 1), son adresse apparaît
	// dans "Label 'nmi_handler' @ 0xXXXXXXXX".
	// On l'écrit directement en RAM aux adresses 0x10 et 0x12.

	uint32_t nmi_addr = 0x0020049E; // remplacer par l'adresse réelle, visible dans les logs passe 1
	// Écrire le vecteur 32-bit en RAM (big-endian, 2 x 16-bit)
	cpu->memory[0x10] = (nmi_addr >> 24) & 0xFF;
	cpu->memory[0x11] = (nmi_addr >> 16) & 0xFF;
	cpu->memory[0x12] = (nmi_addr >>  8) & 0xFF;
	cpu->memory[0x13] = (nmi_addr >>  0) & 0xFF;
	printf("[NMI] Vecteur installé @ 0x%08X\n", nmi_addr);
	printf("[NMI] Contenu du handler (32 premiers octets) :\n");
	uint32_t base = nmi_addr - RAM_START;   // conversion adresse → offset mémoire
	for (int i = 0; i < 32; i++) {
		printf("%02X ", cpu->memory[base + i]);
		if ((i + 1) % 16 == 0) printf("\n");
	}
	printf("\n");

    // ==========================================
    // BOUCLE PRINCIPALE
    // ==========================================
    
    printf("========================================\n");
    printf("Démarrage de l'émulation...\n");
    printf("========================================\n\n");

    bool running = true;
    SDL_Event ev;
    uint64_t frame_count = 0;
    uint32_t last_pc = 0;
    int stuck_counter = 0;
    uint8_t last_video_mode = gpu->video_mode;

    while (running) {
        // Gestion des événements SDL
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                printf("\n[QUIT] Fermeture demandée par l'utilisateur.\n");
                running = false;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                printf("\n[QUIT] Touche ESC pressée.\n");
                running = false;
            }
            
            // Gestion des contrôleurs
            if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                controller_connect(controllers, ev.cdevice.which);
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                // Trouver quel contrôleur a été déconnecté
                for (int i = 0; i < MAX_CONTROLLERS; i++) {
                    if (controllers->controllers[i].connected && 
                        SDL_JoystickInstanceID(controllers->controllers[i].sdl_joystick) == ev.cdevice.which) {
                        controller_disconnect(controllers, i);
                        break;
                    }
                }
            }
        }
        
        // Mise à jour des contrôleurs
        controllers_update(controllers);

        // Exécution CPU (environ 1000 cycles par frame pour ~10 itérations de main_loop)
        // Cela permet un scrolling fluide sans que scroll_x ne devienne trop grand
		for (int i = 0; i < 1000 && !cpu->halted; i++) {
			if (cpu->waiting_vblank) break;   // Ne pas exécuter si en attente VBlank
			cpu_step(cpu);
            
            // Détection de boucle infinie
            if (cpu->PC == last_pc) {
                stuck_counter++;
                if (stuck_counter > 100) {
                    printf("\n[CPU][WARN] Boucle infinie détectée à PC=0x%08X\n", cpu->PC);
                    printf("            Arrêt de l'émulation.\n");
                    cpu->halted = true;
                    break;
                }
            } else {
                stuck_counter = 0;
            }
            last_pc = cpu->PC;
        }
        
        // Simuler une frame complète (224+ scanlines)
        for (int scanline = 0; scanline < 262; scanline++) {
            gpu_step(gpu);
        }
        
        // Rendu de la frame
        gpu_render_frame(gpu);

		// === DÉCLENCHEMENT NMI VBLANK ===
		// Seulement si le CPU attend réellement un VBlank (VSYNC exécuté)
		// Sans ce garde, la NMI s'exécute pendant l'init et corrompt les registres
		if (cpu->waiting_vblank) {
			// Réinstaller le vecteur NMI à chaque frame (init_system le wipe avec MSET)
			uint32_t nmi_addr = 0x00200486; // Adresse de nmi_handler (vérifier dans les logs passe 1)
			cpu->memory[0x10] = (nmi_addr >> 24) & 0xFF;
			cpu->memory[0x11] = (nmi_addr >> 16) & 0xFF;
			cpu->memory[0x12] = (nmi_addr >>  8) & 0xFF;
			cpu->memory[0x13] = (nmi_addr >>  0) & 0xFF;

			cpu->nmi_pending    = true;
			cpu->waiting_vblank = false;
		}

        // Vérifier si le mode vidéo a changé et adapter la fenêtre
        if (gpu->video_mode != last_video_mode) {
            last_video_mode = gpu->video_mode;
            int new_width = (gpu->video_mode == 1 ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3) * WINDOW_SCALE;
            int new_height = SCREEN_HEIGHT * WINDOW_SCALE;
            SDL_SetWindowSize(window, new_width, new_height);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            printf("[GPU] Mode vidéo changé : %s\n", gpu->video_mode ? "16:9" : "4:3");
        }

        // Affichage avec le framebuffer
        // Le framebuffer a une largeur fixe de 400 pixels (SCREEN_WIDTH_16_9)
        // Le pitch doit toujours correspondre à cette largeur
        int pitch = SCREEN_WIDTH_16_9 * 4;  // 400 * 4 = 1600 bytes par ligne

        
        // Mettre à jour toute la texture avec le framebuffer
        SDL_UpdateTexture(texture, NULL, gpu->framebuffer, pitch);
        
        // Définir la zone source à copier (seulement la partie active)
        int screen_width = (gpu->video_mode == 1) ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3;
        SDL_Rect src_rect;
        src_rect.x = 0;
        src_rect.y = 0;
        src_rect.w = screen_width;  // 320 en 4:3, 400 en 16:9
        src_rect.h = SCREEN_HEIGHT;  // 224
        
        // Rendre : copier uniquement la partie active de la texture vers la fenêtre
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, &src_rect, NULL);
        SDL_RenderPresent(renderer);

        // Vérification HALT
        if (cpu->halted) {
            printf("\n========================================\n");
            printf("CPU HALTED\n");
            printf("========================================\n");
            printf("PC final       : 0x%08X\n", cpu->PC);
            printf("Total cycles   : %llu\n", (unsigned long long)cpu->total_cycles);
            printf("Total frames   : %llu\n", (unsigned long long)frame_count);
            printf("\n[CPU] État des registres:\n");
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
            
            // Attendre 3 secondes avant de fermer
            SDL_Delay(3000);
            running = false;
        }
        
        frame_count++;
        SDL_Delay(16); // ~60 FPS
    }

    // ==========================================
    // NETTOYAGE
    // ==========================================
    
    printf("========================================\n");
    printf("Arrêt de l'émulation\n");
    printf("========================================\n");
    printf("Total frames rendues : %llu\n", (unsigned long long)frame_count);
    printf("Total cycles exécutés: %llu\n\n", (unsigned long long)cpu->total_cycles);
    
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
    
    printf("Nettoyage terminé. Au revoir!\n");
    
    return 0;
}
