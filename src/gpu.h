#ifndef PB010381_GPU_H
#define PB010381_GPU_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
   ST32X GPU - Architecture 2D
   =========================================================
   
   Plans d'affichage (du fond vers l'avant):
   - BG2: Arrière-plan lointain (parallax lent)
   - BG1: Arrière-plan moyen (parallax moyen)
   - BG0: Plan de jeu principal (playfield)
   - SPRITES: Objets mobiles avec priorité
   - FG: Premier plan (passe devant tout)
   - HUD: Interface fixe (pas de scrolling)
   
   Caractéristiques:
   - Scrolling hardware pixel-perfect
   - Tuiles 16x16 pixels, 8bpp (256 couleurs indexées)
   - Palettes RGB555 (32768 couleurs)
   - Sprites avec scaling/rotation
   - Collision hardware Sprite-Sprite et Sprite-Tile
   - Mode raycasting pour fausse 3D
========================================================= */

// GPU MMIO addresses (defined in cpu.h but repeated here for clarity)
//#define GPU_MMIO_START 0x00100200
//#define GPU_MMIO_END   0x001007FF

// VRAM configuration (512 KB)
//#define VRAM_START     0x00080000
//#define VRAM_END       0x000FFFFF
#define VRAM_SIZE      0x00080000

// Display configuration
#define MAX_SPRITES    256          // 256 sprites max
#define MAX_PALETTES   32           // 32 palettes de 256 couleurs
#define PALETTE_SIZE   256          // 256 couleurs par palette

/* Résolutions supportées */
#define SCREEN_WIDTH_4_3   320
#define SCREEN_WIDTH_16_9  400
#define SCREEN_HEIGHT      224

/* Tailles de tuiles */
#define TILE_SIZE      16           // Tuiles 16x16 pixels
#define TILE_PIXELS    256          // 16 * 16 = 256 pixels par tuile
#define TILEMAP_WIDTH  32           // 32 tuiles de large
#define TILEMAP_HEIGHT 32           // 32 tuiles de haut

/* =========================================================
   STRUCTURE SPRITE
========================================================= */
typedef struct {
    uint16_t x, y;              // Position à l'écran
    uint16_t tile_index;        // Index de la tuile dans VRAM
    uint8_t  palette;           // Numéro de palette (0-31)
    uint8_t  priority;          // Priorité d'affichage (0=derrière, 3=devant)
    
    // Flags de contrôle (8 bits)
    union {
        uint8_t flags;
        struct {
            uint8_t hflip     : 1;  // Flip horizontal
            uint8_t vflip     : 1;  // Flip vertical
            uint8_t enabled   : 1;  // Sprite activé
            uint8_t size_mode : 2;  // 00=16x16, 01=32x32, 10=64x64, 11=custom
            uint8_t reserved  : 3;  // Réservé
        };
    };
    
    // Scaling (256 = 100%, 512 = 200%, 128 = 50%)
    uint16_t scale_x;
    uint16_t scale_y;
    
    // Collision box (optionnel)
    uint8_t hitbox_w, hitbox_h;
} GPU_Sprite;

/* =========================================================
   STRUCTURE GPU PRINCIPALE
========================================================= */
typedef struct {
    /* === REGISTRES DE CONTRÔLE === */
    uint16_t CTRL;              // Registre de contrôle principal
    uint16_t STATUS;            // Registre de statut
    
    /* === PLANS DE FOND (BG0, BG1, BG2) === */
    uint16_t BG_SCROLL_X[3];    // Scrolling horizontal par plan
    uint16_t BG_SCROLL_Y[3];    // Scrolling vertical par plan
    uint16_t BG_TILEMAP_BASE[3]; // Adresse de base de la tilemap (offset dans VRAM)
    uint16_t BG_TILESET_BASE[3]; // Adresse de base du tileset (offset dans VRAM)
    uint16_t BG_CTRL[3];        // Contrôle par plan (enable, priorité, etc.)
    
    /* === PREMIER PLAN (FG) === */
    uint16_t FG_SCROLL_X;
    uint16_t FG_SCROLL_Y;
    uint16_t FG_TILEMAP_BASE;
    uint16_t FG_TILESET_BASE;
    uint16_t FG_CTRL;
    
    /* === HUD (INTERFACE FIXE) === */
    uint16_t HUD_TILEMAP_BASE;
    uint16_t HUD_TILESET_BASE;
    uint16_t HUD_CTRL;
    
    /* === MÉMOIRE === */
    uint8_t  vram[VRAM_SIZE];                   // VRAM 512 KB
    uint16_t palette[MAX_PALETTES][PALETTE_SIZE]; // 32 palettes x 256 couleurs RGB555
    GPU_Sprite sprites[MAX_SPRITES];            // Table des sprites
    
    /* === DMA === */
    uint32_t dma_src;           // Source 32-bits (adresse absolue)
    uint32_t dma_dst;           // Destination 32-bits (adresse absolue)
    uint16_t dma_len;           // Longueur du transfert
    uint16_t dma_ctrl;          // Contrôle (Start, Direction, etc.)
    
    /* === TIMING === */
    uint16_t SCANLINE;          // Ligne de scan actuelle
    uint16_t VBLANK_LINE;       // Ligne de début du VBlank
    
    /* === COLLISION === */
    uint16_t COLLISION_CTRL;    // Contrôle de la détection de collision
    uint16_t COLLISION_STATUS;  // Statut des collisions détectées
    uint8_t  collision_map[MAX_SPRITES]; // Map de collision sprite-sprite
    
    /* === RAYCASTING (FAUSSE 3D) === */
    uint16_t RAYCAST_CTRL;      // Contrôle du mode raycasting
    uint16_t RAYCAST_FOV;       // Field of View
    uint16_t RAYCAST_HEIGHT;    // Hauteur des murs
    
    // Look-Up Tables pour raycasting
    int16_t sin_lut[360];       // Table sin (degrés)
    int16_t cos_lut[360];       // Table cos (degrés)
    
    /* === FRAMEBUFFER === */
    uint32_t framebuffer[400 * 240]; // Buffer de pixels 32 bits pour SDL (max 16:9)
    
    /* === ÉTAT === */
    bool vblank;
    bool enabled;
    uint8_t video_mode;         // 0=4:3 (320x224), 1=16:9 (400x224)
} PB010381_GPU;

/* =========================================================
   FONCTIONS PUBLIQUES
========================================================= */

/* Initialisation */
void gpu_init(PB010381_GPU *gpu);

/* Cycle GPU (1 scanline) */
void gpu_step(PB010381_GPU *gpu);

/* Accès MMIO */
uint16_t gpu_read16(PB010381_GPU *gpu, uint32_t addr);
void gpu_write16(PB010381_GPU *gpu, uint32_t addr, uint16_t value);

/* Rendu */
void gpu_render_frame(PB010381_GPU *gpu);
void gpu_render_bg(PB010381_GPU *gpu, int plane);
void gpu_render_sprites(PB010381_GPU *gpu);
void gpu_render_hud(PB010381_GPU *gpu);

/* Collision */
void gpu_update_collisions(PB010381_GPU *gpu);
bool gpu_check_sprite_collision(PB010381_GPU *gpu, int sprite_a, int sprite_b);
bool gpu_check_tile_collision(PB010381_GPU *gpu, int sprite_idx, int tile_x, int tile_y);

/* Raycasting */
void gpu_raycast_render(PB010381_GPU *gpu);

/* Debug */
void gpu_debug_dump(PB010381_GPU *gpu);

#endif // PB010381_GPU_H