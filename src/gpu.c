/* =========================================================
Copyright (C) 2026 - Peneaux Benjamin 
This program is free software; 
you may redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; 
either version 3 of the license.
- GNU GENERAL PUBLIC LICENSE V3 -
========================================================= */

#include "gpu.h"
#include "cpu.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEBUG_BG_RENDERING  	1    // Display each instruction
#define DEBUG_PIXELS_RENDERED	1
#define DEBUG_DMA				1
#define DEBUG_BG 				1
#define DEBUG_RAYCAST			1

/* =========================================================
   INITIALIZATION
========================================================= */

void gpu_init(PB010381_GPU *gpu) {
    memset(gpu, 0, sizeof(PB010381_GPU));
    
    gpu->enabled = true;
	
	gpu->COLLISION_CTRL = 1;
	
    gpu->VBLANK_LINE = 224;  // VBlank starts after line 224
    gpu->video_mode = 0;      // Default 4:3 mode
    
    // Initialize sin/cos LUTs for raycasting
    for (int i = 0; i < 360; i++) {
        double rad = (i * M_PI) / 180.0;
        gpu->sin_lut[i] = (int16_t)(sin(rad) * 256.0);  // Fixed-point 8.8
        gpu->cos_lut[i] = (int16_t)(cos(rad) * 256.0);
    }
    
    // Initialize all sprites as disabled
    for (int i = 0; i < MAX_SPRITES; i++) {
        gpu->sprites[i].enabled = 0;
        gpu->sprites[i].scale_x = 256;  // 100%
        gpu->sprites[i].scale_y = 256;
    }
    
    printf("[GPU] Initialized\n");
    printf("      - VRAM: %d KB\n", VRAM_SIZE / 1024);
    printf("      - Sprites: %d max\n", MAX_SPRITES);
    printf("      - Palettes: %d x %d colors\n", MAX_PALETTES, PALETTE_SIZE);
    printf("      - BG planes: 3 (BG0, BG1, BG2)\n");
    printf("      - Foreground (FG): Yes\n");
    printf("      - Fixed HUD: Yes\n");
    printf("      - Hardware collision: Yes\n");
    printf("      - Raycasting: Yes\n");
}

/* =========================================================
   MMIO READ
========================================================= */

uint16_t gpu_read16(PB010381_GPU *gpu, uint32_t addr) {
    uint32_t off = addr - GPU_MMIO_START;
    
    // PALETTE RAM (0x100 - 0x4FF)
    if (off >= 0x300 && off < 0x300 + (MAX_PALETTES * PALETTE_SIZE * 2)) {
        int palette_offset = (off - 0x300) / 2;
        int palette_idx = palette_offset / PALETTE_SIZE;
        int color_idx = palette_offset % PALETTE_SIZE;
        return gpu->palette[palette_idx][color_idx];
    }
    
    // SPRITE TABLE (0x2000 - 0x2FFF)
	if (off >= 0x4300 && off < 0x4300 + (MAX_SPRITES * 16)) {
		int sprite_idx = (off - 0x4300) / 16;
		int sprite_off = (off - 0x4300) % 16;
        GPU_Sprite *spr = &gpu->sprites[sprite_idx];
        
        switch (sprite_off) {
            case 0:  return spr->x;
            case 2:  return spr->y;
            case 4:  return spr->tile_index;
            case 6:  return (spr->palette << 8) | spr->priority;
            case 8:  return spr->flags;
            case 10: return spr->scale_x;
            case 12: return spr->scale_y;
            case 14: return (spr->hitbox_w << 8) | spr->hitbox_h;
            default: return 0;
        }
    }
    
    // STANDARD REGISTERS
    switch (off) {
        case 0x00: return gpu->CTRL;
        case 0x02: return gpu->STATUS;
        
        // BG0
        case 0x10: return gpu->BG_SCROLL_X[0];
        case 0x12: return gpu->BG_SCROLL_Y[0];
        case 0x14: return gpu->BG_TILEMAP_BASE[0];
        case 0x16: return gpu->BG_TILESET_BASE[0];
        case 0x18: return gpu->BG_CTRL[0];
        
        // BG1
        case 0x20: return gpu->BG_SCROLL_X[1];
        case 0x22: return gpu->BG_SCROLL_Y[1];
        case 0x24: return gpu->BG_TILEMAP_BASE[1];
        case 0x26: return gpu->BG_TILESET_BASE[1];
        case 0x28: return gpu->BG_CTRL[1];
        
        // BG2
        case 0x30: return gpu->BG_SCROLL_X[2];
        case 0x32: return gpu->BG_SCROLL_Y[2];
        case 0x34: return gpu->BG_TILEMAP_BASE[2];
        case 0x36: return gpu->BG_TILESET_BASE[2];
        case 0x38: return gpu->BG_CTRL[2];
        
        // FG (Foreground)
        case 0x40: return gpu->FG_SCROLL_X;
        case 0x42: return gpu->FG_SCROLL_Y;
        case 0x44: return gpu->FG_TILEMAP_BASE;
        case 0x46: return gpu->FG_TILESET_BASE;
        case 0x48: return gpu->FG_CTRL;
        
        // HUD
        case 0x50: return gpu->HUD_TILEMAP_BASE;
        case 0x52: return gpu->HUD_TILESET_BASE;
        case 0x54: return gpu->HUD_CTRL;
        
        // DMA
        case 0x500: return (gpu->dma_src & 0xFFFF);
        case 0x502: return (gpu->dma_src >> 16);
        case 0x504: return (gpu->dma_dst & 0xFFFF);
        case 0x506: return (gpu->dma_dst >> 16);
        case 0x508: return gpu->dma_len;
        case 0x50A: return gpu->dma_ctrl;
        
        // COLLISION
        case 0x60: return gpu->COLLISION_CTRL;
		case 0x62: return gpu->COLLISION_STATUS;
        
        // RAYCASTING
        case 0x700: return gpu->RAYCAST_CTRL;
        case 0x702: return gpu->RAYCAST_FOV;
        case 0x704: return gpu->RAYCAST_HEIGHT;
        
        default: return 0;
    }
}

/* =========================================================
   DMA
========================================================= */

// CORRECTION REMARK 5: DMA now uses PB010381_CPU instead of raw pointer
void dma_exec(PB010381_GPU *gpu, PB010381_CPU *cpu) {
    if (!cpu) {
		#if DEBUG_DMA
        printf("[GPU][DMA_ERROR] CPU pointer is NULL!\n");
		#endif
        return;
    }
    
    uint32_t src = gpu->dma_src;
    uint32_t dst = gpu->dma_dst;
    uint32_t len = gpu->dma_len;
    
	#if DEBUG_DMA
    printf("[GPU][DMA] Transfer: %d bytes (0x%08X -> 0x%08X)\n", len, src, dst);
	#endif
    
    // CORRECTION REMARK 12: Copy by offset, not raw pointers
    for (uint32_t i = 0; i < len; i++) {
        uint8_t byte_val = 0;
        
        // Read source by offset
        if (src >= VRAM_START && src <= VRAM_END) {
            uint32_t vram_off = src - VRAM_START;
            if (vram_off < VRAM_SIZE) {
                byte_val = gpu->vram[vram_off];
            }
        } else if (src >= RAM_START && src <= RAM_END) {
            uint32_t ram_off = src - RAM_START;
            if (ram_off < RAM_SIZE) {
                byte_val = cpu->memory[ram_off];
            }
        } else if (src >= ROM_START && src <= ROM_END) {
            uint32_t mem_off = src - RAM_START;
            if (mem_off < MEM_SIZE) {
                byte_val = cpu->memory[mem_off];
            }
        }
        
        // Write destination by offset
        if (dst >= VRAM_START && dst <= VRAM_END) {
            uint32_t vram_off = dst - VRAM_START;
            if (vram_off < VRAM_SIZE) {
                gpu->vram[vram_off] = byte_val;
            }
        } else if (dst >= RAM_START && dst <= RAM_END) {
            uint32_t ram_off = dst - RAM_START;
            if (ram_off < RAM_SIZE) {
                cpu->memory[ram_off] = byte_val;
            }
        } else if (dst >= ROM_START && dst <= ROM_END) {
            // ROM protection - ignore
        }
        
        src++;
        dst++;
    }
    
    // DMA end
    gpu->dma_ctrl &= ~1;
}

/* =========================================================
   MMIO WRITE
========================================================= */

void gpu_write16(PB010381_GPU *gpu, uint32_t addr, uint16_t value) {
    uint32_t off = addr - GPU_MMIO_START;
    
    // PALETTE RAM
	if (off >= 0x300 && off < 0x300 + (MAX_PALETTES * PALETTE_SIZE * 2)) {
		int palette_offset = (off - 0x300) / 2;
        int palette_idx = palette_offset / PALETTE_SIZE;
        int color_idx = palette_offset % PALETTE_SIZE;
        gpu->palette[palette_idx][color_idx] = value;
        return;
    }
    
    // SPRITE TABLE
	if (off >= 0x4300 && off < 0x4300 + (MAX_SPRITES * 16)) {
		int sprite_idx = (off - 0x4300) / 16;
		int sprite_off = (off - 0x4300) % 16;
        GPU_Sprite *spr = &gpu->sprites[sprite_idx];
        
        switch (sprite_off) {
            case 0:  spr->x = value; break;
            case 2:  spr->y = value; break;
            case 4:  spr->tile_index = value; break;
            case 6:
                spr->palette = (value >> 8) & 0xFF;
                spr->priority = value & 0xFF;
                break;
            case 8:  spr->flags = value & 0xFF; break;
            case 10: spr->scale_x = value; break;
            case 12: spr->scale_y = value; break;
            case 14:
                spr->hitbox_w = (value >> 8) & 0xFF;
                spr->hitbox_h = value & 0xFF;
                break;
        }
        return;
    }
    
    // STANDARD REGISTERS
    switch (off) {
        case 0x00: gpu->CTRL = value; break;
        case 0x02: gpu->STATUS = value; break;
        
        // BG0
        case 0x10: gpu->BG_SCROLL_X[0] = value; break;
        case 0x12: gpu->BG_SCROLL_Y[0] = value; break;
        case 0x14: gpu->BG_TILEMAP_BASE[0] = value; break;
        case 0x16: gpu->BG_TILESET_BASE[0] = value; break;
        case 0x18: gpu->BG_CTRL[0] = value; break;
        
        // BG1
        case 0x20: gpu->BG_SCROLL_X[1] = value; break;
        case 0x22: gpu->BG_SCROLL_Y[1] = value; break;
        case 0x24: gpu->BG_TILEMAP_BASE[1] = value; break;
        case 0x26: gpu->BG_TILESET_BASE[1] = value; break;
        case 0x28: gpu->BG_CTRL[1] = value; break;
        
        // BG2
        case 0x30: gpu->BG_SCROLL_X[2] = value; break;
        case 0x32: gpu->BG_SCROLL_Y[2] = value; break;
        case 0x34: gpu->BG_TILEMAP_BASE[2] = value; break;
        case 0x36: gpu->BG_TILESET_BASE[2] = value; break;
        case 0x38: gpu->BG_CTRL[2] = value; break;
        
        // FG
        case 0x40: gpu->FG_SCROLL_X = value; break;
        case 0x42: gpu->FG_SCROLL_Y = value; break;
        case 0x44: gpu->FG_TILEMAP_BASE = value; break;
        case 0x46: gpu->FG_TILESET_BASE = value; break;
        case 0x48: gpu->FG_CTRL = value; break;
        
        // HUD
        case 0x50: gpu->HUD_TILEMAP_BASE = value; break;
        case 0x52: gpu->HUD_TILESET_BASE = value; break;
        case 0x54: gpu->HUD_CTRL = value; break;
        
        // DMA
        case 0x500: gpu->dma_src = (gpu->dma_src & 0xFFFF0000) | value; break;
        case 0x502: gpu->dma_src = (gpu->dma_src & 0x0000FFFF) | (value << 16); break;
        case 0x504: gpu->dma_dst = (gpu->dma_dst & 0xFFFF0000) | value; break;
        case 0x506: gpu->dma_dst = (gpu->dma_dst & 0x0000FFFF) | (value << 16); break;
        case 0x508: gpu->dma_len = value; break;
        case 0x50A:
            gpu->dma_ctrl = value;
            // Note: DMA start will be handled explicitly by CPU
            break;
        
        // COLLISION
        case 0x60: gpu->COLLISION_CTRL = value; break;
		case 0x62: gpu->COLLISION_STATUS = value; break;
        
        // RAYCASTING
        case 0x700: gpu->RAYCAST_CTRL = value; break;
        case 0x702: gpu->RAYCAST_FOV = value; break;
        case 0x704: gpu->RAYCAST_HEIGHT = value; break;
    }
}

/* =========================================================
   GPU CYCLE
========================================================= */

void gpu_step(PB010381_GPU *gpu) {
    if (!gpu->enabled) return;
    
    gpu->SCANLINE++;
    
    // VBlank
    if (gpu->SCANLINE >= gpu->VBLANK_LINE) {
        gpu->vblank = true;
        gpu->STATUS |= 0x01;  // VBlank flag
    }
    
    // End of frame (return to line 0)
    if (gpu->SCANLINE >= 262) {
        gpu->SCANLINE = 0;
        gpu->vblank = false;
        gpu->STATUS &= ~0x01;
    }
}

/* =========================================================
   BG PLANE RENDERING
========================================================= */

void gpu_render_bg(PB010381_GPU *gpu, int plane) {
	#if DEBUG_BG_RENDERING
	printf("[GPU] Rendering BG%d\n", plane);
	#endif
    if (!gpu->enabled) return;
    if (plane < 0 || plane >= 3) return;
    if (!(gpu->BG_CTRL[plane] & 0x01)) return;  // Plane disabled
 
 
    // CORRECTION REMARK 2: Use correct width based on video mode
    int screen_w = (gpu->video_mode == 1) ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3;
    int screen_h = SCREEN_HEIGHT;
    
    uint32_t map_base = gpu->BG_TILEMAP_BASE[plane];
    uint32_t tileset_base = gpu->BG_TILESET_BASE[plane];
    int scroll_x = gpu->BG_SCROLL_X[plane];
    int scroll_y = gpu->BG_SCROLL_Y[plane];
    
    int pixels_rendered = 0;
    int first_tile_idx = -1;
    int first_color_idx = -1;
    
    for (int y = 0; y < screen_h; y++) {
        for (int x = 0; x < screen_w; x++) {
            int world_x = (x + scroll_x) % (TILEMAP_WIDTH * TILE_SIZE);
            int world_y = (y + scroll_y) % (TILEMAP_HEIGHT * TILE_SIZE);
            
            int tile_x = world_x / TILE_SIZE;
            int tile_y = world_y / TILE_SIZE;
            int pixel_x = world_x % TILE_SIZE;
            int pixel_y = world_y % TILE_SIZE;
            
            // Offset in VRAM for tilemap
            uint32_t tile_map_offset = map_base + ((tile_y * TILEMAP_WIDTH + tile_x) * 2);
            if (tile_map_offset + 1 >= VRAM_SIZE) continue;
            
            uint16_t tile_idx = (gpu->vram[tile_map_offset] << 8) | gpu->vram[tile_map_offset + 1];
            
            // Debug: capture first tile_idx
            if (first_tile_idx == -1 && x == 0 && y == 0) {
                first_tile_idx = tile_idx;
            }
            
            // Offset in VRAM for pixel
            uint32_t pixel_offset = tileset_base + (tile_idx * TILE_PIXELS) + (pixel_y * TILE_SIZE + pixel_x);
            if (pixel_offset >= VRAM_SIZE) continue;
            
            uint8_t color_idx = gpu->vram[pixel_offset];
            
            // Debug: capture first non-zero color_idx
            if (first_color_idx == -1 && color_idx != 0 && x == 0 && y == 0) {
                first_color_idx = color_idx;
            }
            
            if (color_idx == 0) continue;  // Transparent
            
            uint16_t rgb565 = gpu->palette[0][color_idx];
            
            // Convert RGB565 -> ARGB8888
            // Palette values are stored as RGB565 (5-6-5 bits for R-G-B)
            uint32_t r = ((rgb565 >> 11) & 0x1F) << 3;
            uint32_t g = ((rgb565 >>  5) & 0x3F) << 2;
            uint32_t b = ( rgb565        & 0x1F) << 3;
            uint32_t a = 255;
            
            uint32_t pixel_color = (a << 24) | (r << 16) | (g << 8) | b;
            
            // CORRECTION REMARK 2: Correct framebuffer index based on video mode
            // Framebuffer always has width of 400 (SCREEN_WIDTH_16_9)
            int fb_idx = y * SCREEN_WIDTH_16_9 + x;
            if (fb_idx < 400 * 240 && x < screen_w) {
                gpu->framebuffer[fb_idx] = pixel_color;
                if (pixel_color != 0) pixels_rendered++;
            }
        }
    }
    #if DEBUG_PIXELS_RENDERED
    printf("[GPU] BG%d: tile_idx[0,0]=%d color_idx[0,0]=%d pixels_rendered=%d\n",
           plane, first_tile_idx, first_color_idx, pixels_rendered);
    fflush(stdout);
	#endif
}


/* =========================================================
   FOREGROUND (FG) RENDERING
========================================================= */

void gpu_render_fg(PB010381_GPU *gpu) {
	#if DEBUG_BG_RENDERING
	printf("[GPU] Rendering FG\n");
	#endif
    if (!gpu->enabled) return;
    if (!(gpu->FG_CTRL & 0x01)) return;  // FG disabled
    
    int screen_w = (gpu->video_mode == 1) ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3;
    int screen_h = SCREEN_HEIGHT;
    
    uint32_t map_base = gpu->FG_TILEMAP_BASE;
    uint32_t tileset_base = gpu->FG_TILESET_BASE;
    int scroll_x = gpu->FG_SCROLL_X;
    int scroll_y = gpu->FG_SCROLL_Y;
    
    int pixels_rendered = 0;
    int first_tile_idx = -1;
    int first_color_idx = -1;
    
    for (int y = 0; y < screen_h; y++) {
        for (int x = 0; x < screen_w; x++) {
            int world_x = (x + scroll_x) % (TILEMAP_WIDTH * TILE_SIZE);
            int world_y = (y + scroll_y) % (TILEMAP_HEIGHT * TILE_SIZE);
            
            int tile_x = world_x / TILE_SIZE;
            int tile_y = world_y / TILE_SIZE;
            int pixel_x = world_x % TILE_SIZE;
            int pixel_y = world_y % TILE_SIZE;
            
            uint32_t tile_map_offset = map_base + ((tile_y * TILEMAP_WIDTH + tile_x) * 2);
            if (tile_map_offset + 1 >= VRAM_SIZE) continue;
            
            uint16_t tile_idx = (gpu->vram[tile_map_offset] << 8) | gpu->vram[tile_map_offset + 1];
            
            if (first_tile_idx == -1 && x == 0 && y == 0) {
                first_tile_idx = tile_idx;
            }
            
            uint32_t pixel_offset = tileset_base + (tile_idx * TILE_PIXELS) + (pixel_y * TILE_SIZE + pixel_x);
            if (pixel_offset >= VRAM_SIZE) continue;
            
            uint8_t color_idx = gpu->vram[pixel_offset];
            
            if (first_color_idx == -1 && color_idx != 0 && x == 0 && y == 0) {
                first_color_idx = color_idx;
            }
            
            if (color_idx == 0) continue;  // Transparent
            
            uint16_t rgb565 = gpu->palette[0][color_idx];
            
            // Convert RGB565 -> ARGB8888
            uint32_t r = ((rgb565 >> 11) & 0x1F) << 3;
            uint32_t g = ((rgb565 >>  5) & 0x3F) << 2;
            uint32_t b = ( rgb565        & 0x1F) << 3;
            uint32_t a = 255;
            
            uint32_t pixel_color = (a << 24) | (r << 16) | (g << 8) | b;
            
            int fb_idx = y * SCREEN_WIDTH_16_9 + x;
            if (fb_idx < 400 * 240 && x < screen_w) {
                gpu->framebuffer[fb_idx] = pixel_color;
                if (pixel_color != 0) pixels_rendered++;
            }
        }
    }
    
	#if DEBUG_BG
    printf("[GPU] FG: tile_idx[0,0]=%d color_idx[0,0]=%d pixels_rendered=%d\n",
           first_tile_idx, first_color_idx, pixels_rendered);
	#endif
    fflush(stdout);
}

/* =========================================================
   SPRITE RENDERING
========================================================= */

void gpu_render_sprites(PB010381_GPU *gpu) {
    if (!gpu->enabled) return;
    
    // CORRECTION REMARK 2: Use correct width
    int screen_w = (gpu->video_mode == 1) ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3;
    int screen_h = SCREEN_HEIGHT;
    
    // Render by priority (0 = behind, 3 = in front)
    for (int priority = 0; priority < 4; priority++) {
        for (int i = 0; i < MAX_SPRITES; i++) {
            GPU_Sprite *spr = &gpu->sprites[i];
            if (!spr->enabled) continue;
            if (spr->priority != priority) continue;
            
            // Sprite size based on scale
            int tile_size = TILE_SIZE;
            int sprite_w = (tile_size * spr->scale_x) / 256;
            int sprite_h = (tile_size * spr->scale_y) / 256;
            
            // Render the sprite
            for (int dy = 0; dy < sprite_h; dy++) {
                for (int dx = 0; dx < sprite_w; dx++) {
                    int screen_x = spr->x + dx;
                    int screen_y = spr->y + dy;
                    
                    // Clipping
                    if (screen_x < 0 || screen_x >= screen_w) continue;
                    if (screen_y < 0 || screen_y >= screen_h) continue;
                    
                    // Inverse mapping with scaling
                    int src_x = (dx * tile_size) / sprite_w;
                    int src_y = (dy * tile_size) / sprite_h;
                    
                    // Flip
                    if (spr->hflip) src_x = (tile_size - 1) - src_x;
                    if (spr->vflip) src_y = (tile_size - 1) - src_y;
                    
                    // Pixel offset in VRAM
                    uint32_t pixel_offset = (spr->tile_index * TILE_PIXELS) + (src_y * TILE_SIZE + src_x);
                    if (pixel_offset >= VRAM_SIZE) continue;
                    
                    uint8_t color_idx = gpu->vram[pixel_offset];
                    if (color_idx == 0) continue;  // Transparent
                    
                    // Get color
                    uint16_t rgb565 = gpu->palette[spr->palette][color_idx];
                    
                    // Convert RGB565 -> ARGB8888
                    uint32_t r = ((rgb565 >> 11) & 0x1F) << 3;
                    uint32_t g = ((rgb565 >>  5) & 0x3F) << 2;
                    uint32_t b = ( rgb565        & 0x1F) << 3;
                    uint32_t a = 255;
                    
                    // CORRECTION REMARK 2: Correct framebuffer index
                    int fb_idx = screen_y * SCREEN_WIDTH_16_9 + screen_x;
                    if (fb_idx < 400 * 240) {
                        gpu->framebuffer[fb_idx] = (a << 24) | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }
}

/* =========================================================
   HUD RENDERING
========================================================= */

void gpu_render_hud(PB010381_GPU *gpu) {
    if (!gpu->enabled) return;
    if (!(gpu->HUD_CTRL & 0x01)) return;  // HUD disabled
    
    // CORRECTION REMARK 2: Use correct width
    int screen_w = (gpu->video_mode == 1) ? SCREEN_WIDTH_16_9 : SCREEN_WIDTH_4_3;
    int screen_h = SCREEN_HEIGHT;
    
    uint32_t map_base = gpu->HUD_TILEMAP_BASE;
    uint32_t tileset_base = gpu->HUD_TILESET_BASE;
    
    // Identical to BG plane rendering but without scrolling
    for (int y = 0; y < screen_h; y++) {
        for (int x = 0; x < screen_w; x++) {
            int tile_x = x / TILE_SIZE;
            int tile_y = y / TILE_SIZE;
            int pixel_x = x % TILE_SIZE;
            int pixel_y = y % TILE_SIZE;
            
            uint32_t tile_map_offset = map_base + ((tile_y * TILEMAP_WIDTH + tile_x) * 2);
            if (tile_map_offset + 1 >= VRAM_SIZE) continue;
            
            uint16_t tile_idx = (gpu->vram[tile_map_offset] << 8) | gpu->vram[tile_map_offset + 1];
            
            uint32_t pixel_offset = tileset_base + (tile_idx * TILE_PIXELS) + (pixel_y * TILE_SIZE + pixel_x);
            if (pixel_offset >= VRAM_SIZE) continue;
            
            uint8_t color_idx = gpu->vram[pixel_offset];
            if (color_idx == 0) continue;  // Transparent
            
            uint16_t rgb565 = gpu->palette[0][color_idx];
            
            uint32_t r = ((rgb565 >> 11) & 0x1F) << 3;
            uint32_t g = ((rgb565 >>  5) & 0x3F) << 2;
            uint32_t b = ( rgb565        & 0x1F) << 3;
            uint32_t a = 255;
            
            // CORRECTION REMARK 2: Correct framebuffer index
            int fb_idx = y * SCREEN_WIDTH_16_9 + x;
            if (fb_idx < 400 * 240 && x < screen_w) {
                gpu->framebuffer[fb_idx] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

/* =========================================================
   COMPLETE FRAME RENDERING
========================================================= */

void gpu_render_frame(PB010381_GPU *gpu) {
    if (!gpu->enabled) return;
    
    // Clear framebuffer (black) - ALWAYS clear full size
    memset(gpu->framebuffer, 0, sizeof(gpu->framebuffer));
    
    // Render planes (back to front)
    gpu_render_bg(gpu, 2);  // BG2 (distant background)
    gpu_render_bg(gpu, 1);  // BG1 (midground)
    gpu_render_bg(gpu, 0);  // BG0 (playfield)
    
    // Sprites (with integrated priority handling)
    gpu_render_sprites(gpu);
	
	gpu_update_collisions(gpu);
    
    // Foreground (in front of everything except HUD)
    gpu_render_fg(gpu);
    
    // HUD (always on top)
    gpu_render_hud(gpu);
}

/* =========================================================
   COLLISION DETECTION
========================================================= */

void gpu_update_collisions(PB010381_GPU *gpu) {
    if (!(gpu->COLLISION_CTRL & 0x01)) return;  // Collision disabled
	
	gpu->COLLISION_STATUS = 0;
    memset(gpu->collision_map, 0, MAX_SPRITES);
    
    // Sprite vs Sprite
    for (int i = 0; i < MAX_SPRITES; i++) {
        if (!gpu->sprites[i].enabled) continue;
        
        for (int j = i + 1; j < MAX_SPRITES; j++) {
            if (!gpu->sprites[j].enabled) continue;
            
            if (gpu_check_sprite_collision(gpu, i, j)) {
                gpu->collision_map[i] |= (1 << (j % 8));
                gpu->collision_map[j] |= (1 << (i % 8));
                gpu->COLLISION_STATUS |= 0x01;  // Collision detected
            }
        }
    }
}

bool gpu_check_sprite_collision(PB010381_GPU *gpu, int sprite_a, int sprite_b) {
    GPU_Sprite *a = &gpu->sprites[sprite_a];
    GPU_Sprite *b = &gpu->sprites[sprite_b];
    
    // AABB collision
    int aw = (TILE_SIZE * a->scale_x) / 256;
    int ah = (TILE_SIZE * a->scale_y) / 256;
    int bw = (TILE_SIZE * b->scale_x) / 256;
    int bh = (TILE_SIZE * b->scale_y) / 256;
    
    if (a->x < b->x + bw && a->x + aw > b->x &&
        a->y < b->y + bh && a->y + ah > b->y) {
        return true;
    }
    
    return false;
}

bool gpu_check_tile_collision(PB010381_GPU *gpu, int sprite_idx, int tile_x, int tile_y) {
    GPU_Sprite *spr = &gpu->sprites[sprite_idx];
    if (!spr->enabled) return false;
    
    // Check if sprite overlaps non-empty tile
    uint32_t map_base = gpu->BG_TILEMAP_BASE[0];  // Collision on BG0
    uint32_t tile_map_offset = map_base + ((tile_y * TILEMAP_WIDTH + tile_x) * 2);
    
    if (tile_map_offset + 1 >= VRAM_SIZE) return false;
    
    uint16_t tile_idx = (gpu->vram[tile_map_offset] << 8) | gpu->vram[tile_map_offset + 1];
    
    return (tile_idx != 0);  // Tile 0 = empty
}

/* =========================================================
   RAYCASTING (NOT YET IMPLEMENTED) (POSSIBLE TO REMOVE)
========================================================= */

void gpu_raycast_render(PB010381_GPU *gpu) {
    if (!(gpu->RAYCAST_CTRL & 0x01)) return;
    
    // TODO: Implement complete raycasting
    // For now, just a placeholder
    #if DEBUG_RAYCAST
    printf("[GPU][RAYCAST] Raycasting rendering not yet implemented\n");
	#endif
}

/* =========================================================
   DEBUG
========================================================= */

void gpu_debug_dump(PB010381_GPU *gpu) {
    printf("\n========== GPU DEBUG ==========\n");
    printf("CTRL: 0x%04X  STATUS: 0x%04X\n", gpu->CTRL, gpu->STATUS);
    printf("Enabled: %d  Video Mode: %s\n", gpu->enabled, 
           gpu->video_mode ? "16:9 (400x224)" : "4:3 (320x224)");
    printf("SCANLINE: %d  VBLANK: %d\n", gpu->SCANLINE, gpu->vblank);
    
    printf("\n--- BG PLANES ---\n");
    for (int i = 0; i < 3; i++) {
        printf("BG%d: Scroll(%d,%d) Map=0x%04X Tileset=0x%04X Ctrl=0x%04X\n",
               i, gpu->BG_SCROLL_X[i], gpu->BG_SCROLL_Y[i],
               gpu->BG_TILEMAP_BASE[i], gpu->BG_TILESET_BASE[i], gpu->BG_CTRL[i]);
    }
    
    printf("\n--- ACTIVE SPRITES ---\n");
    int active_count = 0;
    for (int i = 0; i < MAX_SPRITES && active_count < 10; i++) {
        if (gpu->sprites[i].enabled) {
            printf("Sprite %d: Pos(%d,%d) Tile=%d Pal=%d Pri=%d Scale(%d%%,%d%%)\n",
                   i, gpu->sprites[i].x, gpu->sprites[i].y, 
                   gpu->sprites[i].tile_index, gpu->sprites[i].palette,
                   gpu->sprites[i].priority,
                   (gpu->sprites[i].scale_x * 100) / 256,
                   (gpu->sprites[i].scale_y * 100) / 256);
            active_count++;
        }
    }
    
    printf("\n--- COLLISION ---\n");
    printf("CTRL: 0x%04X  STATUS: 0x%04X\n", 
           gpu->COLLISION_CTRL, gpu->COLLISION_STATUS);
    
    printf("===============================\n\n");
}
