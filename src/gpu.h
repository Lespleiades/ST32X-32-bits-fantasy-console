/* =========================================================
Copyright (C) 2026 - Peneaux Benjamin 
This program is free software; 
you may redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; 
either version 3 of the license or (at your discretion) any later version.
========================================================= */

#ifndef PB010381_GPU_H
#define PB010381_GPU_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
   ST32X GPU - 2D Architecture
   =========================================================
   
   Display layers (back to front):
   - BG2: Distant background (slow parallax)
   - BG1: Midground background (medium parallax)
   - BG0: Main playfield layer
   - SPRITES: Moving objects with priority
   - FG: Foreground layer (passesthru everything)
   - HUD: Fixed overlay (no scrolling)
   
   Features:
   - Hardware pixel-perfect scrolling
   - 16x16 pixel tiles, 8bpp (256 indexed colors)
   - RGB555 palettes (32768 colors)
   - Sprites with scaling/rotation
   - Hardware Sprite-Sprite and Sprite-Tile collision
   - Raycasting mode for fake 3D
========================================================= */

// GPU MMIO addresses (defined in cpu.h but repeated here for clarity)
//#define GPU_MMIO_START 0x00100200
//#define GPU_MMIO_END   0x001007FF

// VRAM configuration (512 KB)
//#define VRAM_START     0x00080000
//#define VRAM_END       0x000FFFFF
#define VRAM_SIZE      0x00080000

// Display configuration
#define MAX_SPRITES    256          // Maximum 256 sprites
#define MAX_PALETTES   32           // 32 palettes of 256 colors each
#define PALETTE_SIZE   256          // 256 colors per palette

/* Supported resolutions */
#define SCREEN_WIDTH_4_3   320
#define SCREEN_WIDTH_16_9  400
#define SCREEN_HEIGHT      224

/* Tile sizes */
#define TILE_SIZE      16           // 16x16 pixel tiles
#define TILE_PIXELS    256          // 16 * 16 = 256 pixels per tile
#define TILEMAP_WIDTH  32           // 32 tiles wide
#define TILEMAP_HEIGHT 32           // 32 tiles high

/* =========================================================
   SPRITE STRUCTURE
========================================================= */
typedef struct {
    uint16_t x, y;              // Screen position
    uint16_t tile_index;        // Tile index in VRAM
    uint8_t  palette;           // Palette number (0-31)
    uint8_t  priority;          // Display priority (0=behind, 3=in front)
    
    // Control flags (8 bits)
    union {
        uint8_t flags;
        struct {
            uint8_t hflip     : 1;  // Horizontal flip
            uint8_t vflip     : 1;  // Vertical flip
            uint8_t enabled   : 1;  // Sprite enabled
            uint8_t size_mode : 2;  // 00=16x16, 01=32x32, 10=64x64, 11=custom
            uint8_t reserved  : 3;  // Reserved
        };
    };
    
    // Scaling (256 = 100%, 512 = 200%, 128 = 50%)
    uint16_t scale_x;
    uint16_t scale_y;
    
    // Collision box (optional)
    uint8_t hitbox_w, hitbox_h;
} GPU_Sprite;

/* =========================================================
   MAIN GPU STRUCTURE
========================================================= */
typedef struct {
    /* === CONTROL REGISTERS === */
    uint16_t CTRL;              // Main control register
    uint16_t STATUS;            // Status register
    
    /* === BACKGROUND LAYERS (BG0, BG1, BG2) === */
    uint16_t BG_SCROLL_X[3];    // Horizontal scroll per layer
    uint16_t BG_SCROLL_Y[3];    // Vertical scroll per layer
    uint16_t BG_TILEMAP_BASE[3]; // Base address of tilemap (VRAM offset)
    uint16_t BG_TILESET_BASE[3]; // Base address of tileset (VRAM offset)
    uint16_t BG_CTRL[3];        // Per-layer control (enable, priority, etc.)
    
    /* === FOREGROUND LAYER (FG) === */
    uint16_t FG_SCROLL_X;
    uint16_t FG_SCROLL_Y;
    uint16_t FG_TILEMAP_BASE;
    uint16_t FG_TILESET_BASE;
    uint16_t FG_CTRL;
    
    /* === HUD (FIXED INTERFACE) === */
    uint16_t HUD_TILEMAP_BASE;
    uint16_t HUD_TILESET_BASE;
    uint16_t HUD_CTRL;
    
    /* === MEMORY === */
    uint8_t  vram[VRAM_SIZE];                   // 512 KB VRAM
    uint16_t palette[MAX_PALETTES][PALETTE_SIZE]; // 32 palettes x 256 colors RGB555
    GPU_Sprite sprites[MAX_SPRITES];            // Sprite table
    
    /* === DMA === */
    uint32_t dma_src;           // Source 32-bit (absolute address)
    uint32_t dma_dst;           // Destination 32-bit (absolute address)
    uint16_t dma_len;           // Transfer length
    uint16_t dma_ctrl;          // Control (Start, Direction, etc.)
    
    /* === TIMING === */
    uint16_t SCANLINE;          // Current scanline
    uint16_t VBLANK_LINE;       // Start line of VBlank
    
    /* === COLLISION === */
    uint16_t COLLISION_CTRL;    // Collision detection control
    uint16_t COLLISION_STATUS;  // Status of detected collisions
    uint8_t  collision_map[MAX_SPRITES]; // Sprite-sprite collision map
    
    /* === RAYCASTING (FAKE 3D) === */
    uint16_t RAYCAST_CTRL;      // Raycasting mode control
    uint16_t RAYCAST_FOV;       // Field of View
    uint16_t RAYCAST_HEIGHT;    // Wall heights
    
    // Look-Up Tables for raycasting
    int16_t sin_lut[360];       // Sin table (degrees)
    int16_t cos_lut[360];       // Cos table (degrees)
    
    /* === FRAMEBUFFER === */
    uint32_t framebuffer[400 * 240]; // 32-bit pixel buffer for SDL (max 16:9)
    
    /* === STATE === */
    bool vblank;
    bool enabled;
    uint8_t video_mode;         // 0=4:3 (320x224), 1=16:9 (400x224)
} PB010381_GPU;

/* =========================================================
   PUBLIC FUNCTIONS
========================================================= */

/* Initialization */
void gpu_init(PB010381_GPU *gpu);

/* GPU cycle (1 scanline) */
void gpu_step(PB010381_GPU *gpu);

/* MMIO access */
uint16_t gpu_read16(PB010381_GPU *gpu, uint32_t addr);
void gpu_write16(PB010381_GPU *gpu, uint32_t addr, uint16_t value);

/* Rendering */
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
