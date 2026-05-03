# ST32X Fantasy Console — Technical Documentation

**Version:** 3.29
**Author:** Peneaux Benjamin
**Architecture:** 32-bit Linear Addressing
**Endianness:** Big-Endian
**Date:** 05/03/2026

---

## Table of Contents

1.  [Overview](#1-overview)
2.  [CPU Architecture](#2-cpu-architecture)
3.  [Memory Map](#3-memory-map)
4.  [Instruction Set (ISA)](#4-instruction-set-isa)
5.  [GPU — Display](#5-gpu--display)
6.  [APU — Audio](#6-apu--audio)
7.  [Controllers](#7-controllers)
8.  [Assembler](#8-assembler)
9.  [MSET Rules and Tilemaps](#9-mset-rules-and-tilemaps)
10. [Embedding Assets in ROM](#10-embedding-assets-in-rom)
11. [Complete Program Example](#11-complete-program-example)

---

## 1. Overview

The ST32X is a 32-bit fantasy console inspired by 16-bit architectures of the 1990s, featuring a 32-bit linear address bus and extended capabilities.

### Main Specifications

| Component 	| Specification 													|
|---------------|-------------------------------------------------------------------|
| CPU 			| 32-bit, 16 general-purpose registers, Big-Endian 					|
| RAM 			| 512 KB (0x00000000 – 0x0007FFFF) 									|
| VRAM 			| 512 KB (0x00080000 – 0x000FFFFF), stored in `gpu->vram[]` 		|
| ROM 			| ~62 MB (0x00200000 – 0x03FFFFFF), read-only 						|
| GPU 			| 6 display layers, 256 sprites, hardware scrolling 				|
| APU 			| 16-channel wavetable PCM 8/16-bit at 44100 Hz 					|
| Controllers 	| 4 USB gamepads (SNES/Xbox type) 									|
| Resolutions 	| 320×224 (4:3) or 400×224 (16:9) 									|
| Colors 		| RGB555 — 32768 colors, 32 palettes × 256 colors 					|
| Sprites 		| 256 sprites 16×16 with scaling, flip, priority 					|
| Collision 	| Hardware sprite-sprite (AABB) 									|

---

## 2. CPU Architecture

### Registers

| Register 		| Usage 															|
|---------------|-------------------------------------------------------------------|
| R0 – R13 		| General-purpose 32-bit registers 									|
| R14 			| Frame Pointer (FP) — by convention 								|
| R15 			| Stack Pointer (SP) — points to top of stack 						|
| PC 			| 32-bit Program Counter 											|

### Flags

| Flag 			| Name 							| Set condition 					|
|---------------|-------------------------------|-----------------------------------|
| Z 			| Zero 							| Result == 0 						|
| N 			| Negative 						| Bit 31 of result == 1 			|
| C 			| Carry 						| Unsigned overflow/borrow 			|
| V 			| Overflow 						| Signed overflow or 				|
|				|						  		| division/modulo by zero 			|

### Instruction Format

All instructions have a mandatory **16-bit header**, optionally followed by an immediate:

```
Header : [Opcode:8 | Rd:4 | Rs:4]

Type 0 : Header only                (2 bytes)
Type 1 : Header + imm16             (4 bytes)
Type 2 : Header + imm32             (6 bytes)
```

Type 2 32-bit immediates are **not** subject to 32-bit alignment since they
follow a 16-bit header. The CPU reads them byte by byte via `mem_read8`.

### Alignment Rules (cpu.c — mem_read16 / mem_write16 / mem_read32 / mem_write32)

| Access 		| Required alignment 			| Behavior if unaligned 			|
|---------------|-------------------------------|-----------------------------------|
| 8-bit 		| None | Normal 				|									|
| 16-bit 		| Even address (bit 0 = 0) 		| Auto-forced (`addr &= ~1`) 		|
| 32-bit 		| Multiple of 4 (bits 1:0 = 00) | Auto-forced (`addr &= ~3`) 		|

---

## 3. Memory Map


```
0x00000000 ┌─────────────────────────────┐
           │  RAM — 512 KB               │  Variables, stack, heap
0x0007FFFF └─────────────────────────────┘

0x00080000 ┌─────────────────────────────┐
           │  VRAM — 512 KB              │  Tilesets, tilemaps (gpu->vram[])
0x000FFFFF └─────────────────────────────┘

0x00100000 ┌─────────────────────────────┐
           │  I/O & MMIO — 64 KB         │
           │                             │
           │  0x00100100 – 0x001001FF    │  Controllers MMIO
           │  0x00100200 – 0x001057FF    │  GPU MMIO (extended GPU_MMIO_END)
           │    ├ 0x00100200             │    GPU_CTRL
           │    ├ 0x00100210 – 0x00100238│    BG0/BG1/BG2 registers
           │    ├ 0x00100240 – 0x00100248│    FG registers
           │    ├ 0x00100250 – 0x00100254│    HUD registers
           │    ├ 0x00100260 – 0x00100262│    Collision CTRL/STATUS
           │    ├ 0x00100300 – 0x001042FF│    Palettes (internal offset 0x100)
           │    └ 0x00104500 – 0x001057FF│    Sprite table (internal offset 0x4300)
           │  0x00100800 – 0x00100FFF    │  APU MMIO
0x0010FFFF └─────────────────────────────┘

0x00200000 ┌─────────────────────────────┐
           │  ROM / Cartridge — ~62 MB   │  Code, graphics, audio (read-only)
0x03FFFFFF └─────────────────────────────┘
```

> **VRAM note**: VRAM (0x00080000–0x000FFFFF) is physically stored in
> `gpu->vram[]`, not in `cpu->memory[]`. MSET/MCPY access it via
> `mem_write8`/`mem_read8` which redirect to `cpu->gpu->vram[offset]`.

> **Check order in cpu.c**: APU must be checked **before** GPU in
> `mem_read16` and `mem_write16` because `GPU_MMIO_END` extended to
> `0x001057FF` now covers APU addresses (0x00100800+).

### Stack Pointer Initialization (CRITICAL)

SP must be initialized **first**, before any `CALL`.

```asm
main:
    LI  R15, 0xFFFC     ; bits 15:0  → R15 = 0x0000FFFC
    LIH R15, 0x0007     ; bits 31:16 → R15 = 0x0007FFFC ✓
    CALL init_system
```

> ⚠️ Always **LI before LIH**. LIH overwrites bits 31:16 while keeping bits 15:0.
> SP must point into RAM and be aligned to 4 bytes.

---

## 4. Instruction Set (ISA)

### Control

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| HALT 		| 0x00 		| 0 	| Stop CPU execution 								|
| NOP  		| 0xFF 		| 0 	| No operation 										|
| CLC  		| 0xFC 		| 0 	| Clear Carry flag 									|
| SEC  		| 0xFB 		| 0 	| Set Carry flag 									|

### 8-bit Load / Store

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| LDRI8 	| 0x62 		| 2 	| `Rd = mem8[imm32]` 								|
| STRI8 	| 0x63 		| 2 	| `mem8[imm32] = Rs & 0xFF` 						|
| LDR8  	| 0x64 		| 0 	| `Rd = mem8[Rs]` 									|
| STR8  	| 0x65 		| 0 	| `mem8[Rd] = Rs & 0xFF` 							|

### 16-bit Load / Store

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| LDRI 		| 0x01 		| 2 	| `Rd = mem16[imm32]` 								|
| STRI 		| 0x02 		| 2 	| `mem16[imm32] = Rs & 0xFFFF` 						|

### 32-bit Load / Store

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| LDR 		| 0x03 		| 0 	| `Rd = mem32[Rs]` 									|
| STR 		| 0x04 		| 0 	| `mem32[Rd] = Rs` 									|

### Transfer and Immediate

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| MOV 		| 0x05 		| 0 	| `Rd = Rs` 										|
| LI  		| 0x06 		| 1 	| `Rd = imm16 (zero-extended to 32-bit) 			|
| LIL 		| 0x60 		| 1 	| `Rd[15:0] = imm16`(bits 31:16 unchanged) 			|
| LIH 		| 0x61 		| 1 	| `Rd[31:16] = imm16` (bits 15:0 unchanged) 		|

#### Building a 32-bit Address

```asm
; Load 0x00081200 into R2
LI  R2, 0x1200      ; R2 = 0x00001200
LIH R2, 0x0008      ; R2 = 0x00081200  ← always LI then LIH
```

### Arithmetic

| Mnemonic 	| Opcode	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| ADD  		| 0x07 		| 0 	| `Rd = Rd + Rs` — updates Z, N, C, V 				|
| SUB  		| 0x08 		| 0 	| `Rd = Rd - Rs` — updates Z, N, C, V 				|
| MUL  		| 0x09 		| 0 	| `Rd = Rd * Rs` — C=1 on 32-bit overflow 			|
| DIV  		| 0x0A 		| 0 	| `Rd = Rd / Rs` — V=1 on divide by zero 			|
| MOD  		| 0x0B 		| 0 	| `Rd = Rd % Rs` — V=1 on modulo by zero 			|
| INC  		| 0x0C 		| 0 	| `Rd = Rd + 1` 									|
| DEC 		| 0x0D 		| 0 	| `Rd = Rd - 1` 									|
| CMP  		| 0x0E 		| 0 	| Computes `Rd - Rs`, updates flags, Rd unchanged 	|
| ADDI 		| 0x0F 		| 1 	| `Rd = Rd + imm16` 								|
| SUBI 		| 0x14 		| 1 	| `Rd = Rd - imm16` 								|

### Logic and Shifts

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| AND 		| 0x1B 		| 0 	| `Rd = Rd & Rs` 									|
| OR  		| 0x1C 		| 0 	| `Rd = Rd \| Rs` 									|
| XOR 		| 0x1D 		| 0 	| `Rd = Rd ^ Rs` 									|
| NOT 		| 0x1E 		| 0 	| `Rd = ~Rd` 										|
| SHL 		| 0x1F 		| 0 	| `Rd = Rd << (Rs & 0x1F)` 							|
| SHR 		| 0x21 		| 0 	| `Rd = Rd >> (Rs & 0x1F)` (logical) 				|

> SHL/SHR clamp Rs to 0–31 bits to avoid C undefined behavior.

### Jumps and Calls (32-bit absolute addresses)

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| JMP  		| 0x10 		| 2 	| `PC = imm32` 										|
| JZ   		| 0x11 		| 2 	| `if (Z==1) PC = imm32` 							|
| JNZ  		| 0x12 		| 2 	| `if (Z==0) PC = imm32` 							|
| CALL 		| 0x17 		| 2 	| `Push32(PC) ; PC = imm32` 						|
| RET  		| 0x18 		| 0 	| `PC = Pop32()` 									|

### Stack (32-bit, SP = R15)

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| PUSH 		| 0x19 		| 0 	| `R15 -= 4 ; mem32[R15] = Rs` 						|
| POP  		| 0x1A 		| 0 	| `Rd = mem32[R15] ; R15 += 4` 						|

### GPU and Memory

| Mnemonic 	| Opcode 	| Type 	| Description 										|
|-----------|-----------|-------|---------------------------------------------------|
| VSYNC 	| 0x51 		| 0 	| Wait for VBlank 									|
| MCPY  	| 0x40 		| 0 	| Copy R3 bytes from [R1] to [R2] 					|
| MSET  	| 0x42 		| 0 	| Fill R3 bytes at [R2] with `R1 & 0xFF` 			|

---

## 5. GPU — Display

### Display Layers

```c
gpu_render_bg(gpu, 2);    		// BG2 — far background
gpu_render_bg(gpu, 1);    		// BG1 — mid background
gpu_render_bg(gpu, 0);    		// BG0 — playfield
gpu_render_sprites(gpu);  		// Sprites (priority 0 → 3)
gpu_update_collisions(gpu); 	// Collision detection (fix required)
gpu_render_fg(gpu);       		// FG — foreground
gpu_render_hud(gpu);      		// HUD — fixed overlay
```

> **Sprite priority**: sprites with `priority=0` draw between BG0 and FG.
> Sprites with `priority=3` draw just before FG. Valid range: **0–3**.
> A priority outside this range makes the sprite invisible.

### Important Renderer Limitations

> ⚠️ **BG/FG/HUD palette**: The BG, FG and HUD renderers always use
> `gpu->palette[0]` (hardcoded at lines 384/461/585 of gpu.c). Only sprites
> use `gpu->palette[spr->palette]`. Writing colors to any palette other than
> palette 0 has no effect on backgrounds.

> ⚠️ **Sprite tileset base**: The sprite renderer computes pixels as
> `tile_index * 256 + pixel_offset` — without adding any `tileset_base`.
> A sprite's `tile_index` is therefore an **absolute index from VRAM[0]**,
> unlike BG layers which add `tileset_base`.

### Tile Format

- Size: **16×16 pixels**
- Format: **8bpp** (1 byte per pixel = color index into the active palette)
- Memory size: **256 bytes per tile** (`TILE_PIXELS = 256`)

### GPU MMIO Registers (base = GPU_MMIO_START = 0x00100200)

#### Global Control

| Absolute Address 	| Offset 	| Register 		| Description 						|
|-------------------|-----------|---------------|-----------------------------------|
| 0x00100200 		| 0x00 		| GPU_CTRL 		| Bit 0 = Enable 					|
| 0x00100202 		| 0x02 		| GPU_STATUS	| Bit 0 = VBlank active 			|

#### BG Layers (verified in gpu_read16 / gpu_write16 switch)

| Layer | SCROLL_X 		| SCROLL_Y 		| TILEMAP_BASE	| TILESET_BASE	| CTRL 		|
|-------|---------------|---------------|---------------|---------------|-----------|
| BG0 	| 0x00100210 	| 0x00100212 	| 0x00100214 	| 0x00100216	| 0x00100218|
| BG1 	| 0x00100220 	| 0x00100222 	| 0x00100224 	| 0x00100226 	| 0x00100228|
| BG2 	| 0x00100230 	| 0x00100232 	| 0x00100234 	| 0x00100236 	| 0x00100238|

`CTRL` bit 0 = Enable. `TILEMAP_BASE` and `TILESET_BASE` are offsets within VRAM.

```asm
; Enable BG0 : tilemap @ VRAM+0x1000, tileset @ VRAM+0x0000, scroll=0
LI R0, 0x0001 : STRI 0x00100218, R0   ; BG0_CTRL = Enable
LI R0, 0x1000 : STRI 0x00100214, R0   ; BG0_TILEMAP_BASE
LI R0, 0x0000 : STRI 0x00100216, R0   ; BG0_TILESET_BASE
LI R0, 0      : STRI 0x00100210, R0   ; BG0_SCROLL_X
              : STRI 0x00100212, R0   ; BG0_SCROLL_Y
```

#### Foreground (FG)

| Register 						| Absolute Address 									|
|-------------------------------|---------------------------------------------------|
| FG_SCROLL_X 					| 0x00100240 										|
| FG_SCROLL_Y 					| 0x00100242 										|
| FG_TILEMAP_BASE 				| 0x00100244 										|
| FG_TILESET_BASE 				| 0x00100246 										|
| FG_CTRL 						| 0x00100248 										|

#### HUD

| Register 						| Absolute Address 									|
|-------------------------------|---------------------------------------------------|
| HUD_TILEMAP_BASE 				| 0x00100250 										|
| HUD_TILESET_BASE 				| 0x00100252 										|
| HUD_CTRL 						| 0x00100254 										|

```asm
; Enable HUD, tilemap @ VRAM+0x5000
LI R0, 0x0001 : STRI 0x00100254, R0   ; HUD_CTRL = Enable
LI R0, 0x5000 : STRI 0x00100250, R0   ; HUD_TILEMAP_BASE
LI R0, 0x0000 : STRI 0x00100252, R0   ; HUD_TILESET_BASE
```

### RGB555 Palettes

**Base address:** `0x00100300`
(internal offset `0x100` from GPU_MMIO_START — fixed to `0x300`)

**Address of palette N, color C:** `0x00100300 + (N × 512) + (C × 2)`

Palette 0 example colors:

| Color index	| Address 		| RGB555 value	| Color 							|
|---------------|---------------|---------------|-----------------------------------|
| 0 			| 0x00100300 	| 0x0000 		| Transparent / Black 				|
| 1	 			| 0x00100302 	| 0xF800 		| Red 								|
| 2 			| 0x00100304 	| 0x07E0 		| Green 							|
| 3 			| 0x00100306 	| 0xFFE0 		| Yellow 							|
| 4 			| 0x00100308 	| 0xF81F 		| Magenta 							|
| 5 			| 0x0010030A 	| 0xFC00 		| Orange 							|
| 6 			| 0x0010030C 	| 0x8410 		| Gray 								|
| 7 			| 0x0010030E 	| 0x000F 		| Dark Blue 						|
| 8				| 0x00100310 	| 0x001F 		| Bright Blue 						|
| 9 			| 0x00100312 	| 0xFFFF 		| White 							|

**RGB555 bit layout:**
```
Bit 15     : unused (0)
Bits 14:10 : Red   (0–31)
Bits  9:5  : Green (0–31)
Bits  4:0  : Blue  (0–31)
```

> Reminder: BG/FG/HUD always read from `palette[0]`. To change a background
> color, modify the colors in palette 0.

### Sprites (verified in gpu_write16 cases 0–14)

**Base address:** `0x00104500`
(internal offset `0x4300` from GPU_MMIO_START)

**Address of sprite N:** `0x00104500 + (N × 16)`

| Offset 	| Switch case	| Register		| Description 							|
|-----------|---------------|---------------|---------------------------------------|
| +0x00		| case 0 		| X 			| X position (16-bit) 					|
| +0x02 	| case 2 		| Y 			| Y position (16-bit) 					|
| +0x04 	| case 4 		| TILE_INDEX 	| Absolute tile index from VRAM[0] 		|
| +0x06 	| case 6 		| PAL\|PRI 		| High byte = palette (0–31), 			|
|			|				|				| low byte = priority (0–3) 			|
| +0x08 	| case 8 		| FLAGS 		| See table below 						|
| +0x0A 	| case 10 		| SCALE_X 		| X scale (256 = 100%, 512 = 200%) 		|
| +0x0C 	| case 12 		| SCALE_Y 		| Y scale (256 = 100%) 					|
| +0x0E 	| case 14 		| HITBOX 		| High byte = hitbox_w, 				|
|			|				|				| low byte = hitbox_h 					|

**FLAGS bits:**

| Bit	| Field 	| Description 													|
|-------|-----------|---------------------------------------------------------------|
| 0 	| hflip 	| Horizontal flip 												|
| 1 	| vflip 	| Vertical flip 												|
| 2 	| enabled 	| Sprite visible — must be 1 									|
| 3–4 	| size_mode | 00=16×16, 01=32×32, 10=64×64, 11=custom 						|

```asm
; Sprite 0 : dark blue, position (152,100), priority 2
LI R0, 152   : STRI 0x00104500, R0   ; x
LI R0, 100   : STRI 0x00104502, R0   ; y
LI R0, 1799  : STRI 0x00104504, R0   ; tile_index (absolute from VRAM[0])
LI R0, 0x0002: STRI 0x00104506, R0   ; palette=0, priority=2
LI R0, 0x0004: STRI 0x00104508, R0   ; flags: enabled=1
LI R0, 256   : STRI 0x0010450A, R0   ; scale_x = 100%
             : STRI 0x0010450C, R0   ; scale_y = 100%
```

### Collision Detection

| Address 	| Offset| Register 			| Description 								|
|-----------|-------|-------------------|-------------------------------------------|
| 0x00100260| 0x60 	| COLLISION_CTRL	| Bit 0 = Enable detection 					|
| 0x00100262| 0x62 	| COLLISION_STATUS 	| Bit 0 = 1 if collision detected this frame|

**Guaranteed behavior after fixes (verified in gpu_update_collisions):**
- `gpu_update_collisions` is called inside `gpu_render_frame` (fix required)
- The function resets `COLLISION_STATUS = 0` at entry (fix required)
- Then sets bit 0 to 1 if any sprite-sprite AABB collision is detected
- `COLLISION_STATUS` therefore remains stable for the entire host frame duration

> ⚠️ **Never write to `COLLISION_STATUS` from ASM.**
> `gpu_update_collisions` is the sole owner of this register, called once per frame.
> If ASM clears it manually, the collision sound will flicker because
> `main_loop` runs ~8 times per host frame.

```asm
check_collision:
    LDRI R7, 0x00100262       ; COLLISION_STATUS
    LI R6, 0x0001
    AND R7, R6                ; isolate bit 0
    LI R6, 0
    CMP R7, R6
    JZ no_collision
    ; ... collision is active
no_collision:
    ; ... no collision
    RET
```

---

## 6. APU — Audio

### Specifications

- 16 independent wavetable channels
- PCM **8-bit** unsigned (128 = silence) or **16-bit** signed Big-Endian
- Sample rate: **44100 Hz**
- Hardware pitch-shifting (fixed-point 16.16: `increment = pitch << 16 / 44100`)
- Volume 0–255 and stereo panning 0–255 (0=left, 128=center, 255=right)
- Automatic looping with configurable loop start point
- Mixed stereo 16-bit signed output across all active channels

### Global APU Registers (@ APU_MMIO_START = 0x00100800)

| Address 	| Offset| Register 			| Description 								|
|-----------|-------|-------------------|-------------------------------------------|
| 0x00100800| 0x00 	| MASTER_CTRL 		| Bit 0 = Enable, Bit 1 = Mute 				|
| 0x00100802| 0x02 	| MASTER_VOLUME 	| Global volume 0–255 						|

### Channels

Internal channel offset: `0x100 + N × 32`

**Channel N base address:** `APU_MMIO_START + 0x100 + N × 32 = 0x00100900 + N × 32`

| Channel 							| Base Address 									|
|-----------------------------------|-----------------------------------------------|
| CH0 								| 0x00100900 									|
| CH1 								| 0x00100920 									|
| CH2 								| 0x00100940 									|
| … 								| … 											|
| CH15 								| 0x00100AF0 									|

**Channel registers (offsets relative to channel base):**

| Offset| Case| Register 	| Description 											|
|-------|-----|-------------|-------------------------------------------------------|
| +0x00 | 0x00| ADDR_H		| Bits 31:16 of sample RAM address 						|
| +0x02 | 0x02| ADDR_L 		| Bits 15:0 — address assembled after writing ADDR_L 	|
| +0x04 | 0x04| LENGTH_H 	| Bits 31:16 of length in bytes 						|
| +0x06 | 0x06| LENGTH_L 	| Bits 15:0 — length assembled after writing LENGTH_L 	|
| +0x08 | 0x08| LOOP_START_H| Bits 31:16 of loop point 								|
| +0x0A | 0x0A| LOOP_START_L| Bits 15:0 											|
| +0x0C | 0x0C| PITCH 		| Playback frequency in Hz (16-bit, e.g. 0xAC44 = 44100)|
| +0x0E | 0x0E| VOL_PAN 	| High byte = Volume (0–255), Low byte = Pan (0–255) 	|
| +0x10 | 0x10| CTRL 		| play/loop/16bit bits — see table 						|

**CTRL register bits (apu.h union and apu.c case 0x10):**

| Bit| Field | Description 															|
|----|-------|----------------------------------------------------------------------|
| 0  | play  | 1 = start playback 													|
| 1  | loop  | 1 = loop back to loop_start 											|
| 2  | bit16 | 0 = 8-bit samples, 1 = 16-bit signed samples 						|

**Common CTRL values:**

| Value 					| Meaning 												|
|---------------------------|-------------------------------------------------------|
| 0x0000 					| Stop 													|
| 0x0001 					| Play one-shot 8-bit 									|
| 0x0003 					| Play + Loop 8-bit 									|
| 0x0005 					| Play one-shot 16-bit 									|
| 0x0007 					| Play + Loop 16-bit 									|

> ⚠️ **CTRL restart behavior**:
> `if (chan->play && !(chan->status & 0x01))` — the channel only starts if it
> is **not already playing**. Writing `CTRL = 0x0003` to an active channel
> does **nothing**. To restart: write `0x0000` (stop) then `0x0003` (play).

### Creating a Square Wave Sample in RAM

```asm
; 441 Hz square wave @ RAM 0x00010000 (100 bytes, 8-bit)
; f = 44100 / 100 = 441 Hz
; Bytes  0–49 : 200 (HIGH, above 128)
; Bytes 50–99 :  56 (LOW,  below 128)

LI R1, 200
LI R2, 0x0000 : LIH R2, 0x0001    ; 0x00010000
LI R3, 50
MSET

LI R1, 56
LI R2, 0x0032 : LIH R2, 0x0001    ; 0x00010032 (offset 50)
LI R3, 50
MSET
```

### Complete Channel Setup Example

```asm
; Enable APU
LI R0, 0x0001 : STRI 0x00100800, R0   ; MASTER_CTRL = Enable
LI R0, 200    : STRI 0x00100802, R0   ; MASTER_VOLUME = 200

; CH0 — 441 Hz low tone, looping, sample @ 0x00010000 (100 bytes)
LI R0, 0x0001 : STRI 0x00100900, R0   ; ADDR_H
LI R0, 0x0000 : STRI 0x00100902, R0   ; ADDR_L  ← address committed here
LI R0, 0      : STRI 0x00100904, R0   ; LENGTH_H
LI R0, 100    : STRI 0x00100906, R0   ; LENGTH_L ← length committed here
LI R0, 0      : STRI 0x00100908, R0   ; LOOP_START_H
              : STRI 0x0010090A, R0   ; LOOP_START_L
LI R0, 0xAC44 : STRI 0x0010090C, R0  ; PITCH = 44100 Hz
LI R0, 0xC880 : STRI 0x0010090E, R0  ; VOL=0xC8=200, PAN=0x80=128 (center)
LI R0, 0x0003 : STRI 0x00100910, R0  ; CTRL = play+loop 8-bit
```

---

## 7. Controllers

### Specifications

- Up to **4 simultaneous gamepads** (MAX_CONTROLLERS = 4)
- **8 buttons**: A, B, X, Y, L, R, SELECT, START
- **D-PAD** 4 directions
- SDL2 GameController API + raw joystick fallback for compatibility

### MMIO Registers (base = CONTROLLER_MMIO_START = 0x00100100)

**Global status (offset 0x00):**

| Address 		| Offset 	| Description 											|
|---------------|-----------|-------------------------------------------------------|
| 0x00100100 	| 0x00 		| Bit N = 1 if gamepad N is connected (N = 0–3) 		|

**Per-gamepad registers** — base offset = `0x10 + N × 8`
(verified in `controllers_read16`: `ctrl_idx = (off - 0x10) / 8`, `reg = (off - 0x10) % 8`)

| Gamepad 	| BUTTONS (reg=0) 	| DPAD (reg=2) 	| BUTTONS_PREV (reg=4) 				|
|-----------|-------------------|---------------|-----------------------------------|
| 0 		| 0x00100110 		| 0x00100112 	| 0x00100114 						|
| 1 		| 0x00100118 		| 0x0010011A 	| 0x0010011C 						|
| 2 		| 0x00100120 		| 0x00100122 	| 0x00100124 						|
| 3 		| 0x00100128 		| 0x0010012A 	| 0x0010012C 						|

**BUTTONS masks:**

| Bit 	| Button 			| Mask 													|
|-------|-------------------|-------------------------------------------------------|
| 0 	| A 				| 0x0001 												|
| 1 	| B 				| 0x0002 												|
| 2 	| X 				| 0x0004 												|
| 3 	| Y 				| 0x0008 												|
| 4 	| L (LB) 			| 0x0010 												|
| 5 	| R (RB) 			| 0x0020 												|
| 6 	| SELECT 			| 0x0040 												|
| 7 	| START 			| 0x0080 												|
| 0–7 	| All buttons 		| 0x00FF 												|

**DPAD masks:**

| Bit 	| Direction 		| Mask 													|
|-------|-------------------|-------------------------------------------------------|
| 0 	| UP 				| 0x0001 												|
| 1 	| DOWN 				| 0x0002 												|
| 2 	| LEFT 				| 0x0004 												|
| 3 	| RIGHT 			| 0x0008 												|

> Controller registers are **read-only**. `controllers_write16` ignores all
> writes (no rumble or LED support in this version).

### Example: D-PAD Movement

```asm
update_sprite:
    LDRI R1, 0x00100112       ; DPAD gamepad 0
    LDRI R2, 0x00104500       ; sprite[0].x
    LDRI R3, 0x00104502       ; sprite[0].y

    LI R4, 0x0001             ; UP mask
    MOV R5, R1 : AND R5, R4
    LI R6, 0   : CMP R5, R6
    JZ test_down
    DEC R3

test_down:
    LI R4, 0x0002             ; DOWN mask
    MOV R5, R1 : AND R5, R4
    LI R6, 0   : CMP R5, R6
    JZ test_left
    INC R3

test_left:
    LI R4, 0x0004             ; LEFT mask
    MOV R5, R1 : AND R5, R4
    LI R6, 0   : CMP R5, R6
    JZ test_right
    DEC R2

test_right:
    LI R4, 0x0008             ; RIGHT mask
    MOV R5, R1 : AND R5, R4
    LI R6, 0   : CMP R5, R6
    JZ write_pos
    INC R2

write_pos:
    STRI 0x00104500, R2
    STRI 0x00104502, R3
    RET
```

### Example: Detect Any Button Press

```asm
check_buttons:
    LDRI R1, 0x00100110       ; BUTTONS gamepad 0
    LI R4, 0x00FF             ; all buttons mask
    AND R1, R4
    LI R6, 0
    CMP R1, R6
    JZ btn_none

    ; Trigger CH1 one-shot (8-bit)
    LI R0, 0x0001             ; play=1, loop=0, 8-bit
    STRI 0x00100930, R0       ; CH1 CTRL

btn_none:
    RET
```

### Example: Single-press Edge Detection

```asm
; Detect button A pressed this frame only (not held from previous frame)
LDRI R1, 0x00100110       ; current BUTTONS
LDRI R2, 0x00100114       ; BUTTONS_PREV
NOT  R2, R2               ; ~prev
AND  R1, R2               ; current & ~prev = just pressed this frame
LI   R3, 0x0001           ; A mask
AND  R1, R3
LI   R4, 0
CMP  R1, R4
JZ   not_pressed          ; non-zero means A was just pressed
```

---

## 8. Assembler

### Usage

```bash
./assembler input.asm output.bin
# or with custom arguments
./assembler my_game.asm my_game.bin
```

The produced binary is loaded into ROM at `0x00200000` by `main.c`.

### Directives

| Directive 		| Description 													|
|-------------------|---------------------------------------------------------------|
| `.org address`	| Set current PC (pads with 0x00 bytes if advancing forward) 	|

### Labels

```asm
main_loop:
    VSYNC
    JMP main_loop
```

Labels are resolved in pass 1 as absolute 32-bit addresses. They can be used
as an operand of any Type 2 instruction (JMP, JZ, JNZ, CALL, LDRI, STRI, etc.).

### Comments

`;` comments extend to end of line. Inline comments after instructions
are supported.

### Special Encoding Cases (verified in assembler.c)

| Instruction 								| Rd| Rs| Immediate 					|
|-------------------------------------------|---|---|-------------------------------|
| `STRI imm32, Rs` 							| 0 | Rs| token[1] = address 			|
| `LDRI Rd, imm32` 							| Rd| 0 | token[last] = address 		|
| `PUSH Rs` 								| 0 | Rs| — 							|
| `POP Rd`, `INC Rd`, `DEC Rd`, `NOT Rd` 	| Rd| 0 | — 							|

---

## 9. MSET Rules and Tilemaps

### Internal Mechanism

`MSET` writes the value `R1 & 0xFF` (one byte) repeated R3 times.
The tilemap stores **16-bit** indices. The GPU reads:
`tile_idx = (vram[offset] << 8) | vram[offset + 1]`

If R1 = N, two consecutive bytes are `N, N` → `tile_idx = (N << 8) | N = N × 257`.

**Correspondence table:**

| R1 (MSET) | GPU index 	| VRAM address of tile 									|
|-----------|---------------|-------------------------------------------------------|
| 1 		| 257 (0x0101) 	| 0x00080000 + 257×256 = 0x00090100 					|
| 2 		| 514 (0x0202) 	| 0x000A0200 											|
| 3 		| 771 (0x0303) 	| 0x000B0300 											|
| 4 		| 1028 (0x0404) | 0x000C0400 											|
| 5 		| 1285 (0x0505) | 0x000D0500 											|
| 6 		| 1542 (0x0606) | 0x000E0600 											|
| 7 		| 1799 (0x0707) | 0x000F06C0 											|
| N 		| N × 257 		| 0x00080000 + N×257×256 								|

> Exception: for N=7, the address is 0x000F06C0 (1799 × 256 = 0x706C0,
> VRAM_START + 0x706C0). The `N × 0x10100` shortcut only holds for small N
> within the 512 KB VRAM boundary.

### Creating a Tile and Filling a Background

```asm
; Create tile color 1 (red) at index 257
LI R1, 1
LI R2, 0x0100 : LIH R2, 0x0009    ; VRAM + 0x10100 = 0x00090100
LI R3, 256
MSET

; Fill entire BG0 tilemap with this tile
LI R1, 1                           ; → index 257 everywhere
LI R2, 0x1000 : LIH R2, 0x0008    ; VRAM + 0x1000 = 0x00081000
LI R3, 2048                        ; 32 tiles × 32 rows × 2 bytes
MSET
```

### Filling a Horizontal Band Only

The tilemap is 32 tiles wide = **64 bytes per row**.

```asm
; BG1 green — only rows 4 to 7 (offset 4×64=0x100, length 4×64=256 bytes)
LI R1, 2
LI R2, 0x2100 : LIH R2, 0x0008    ; VRAM + 0x2000 + 0x100 = 0x00082100
LI R3, 256
MSET
```

---

## 10. Embedding Assets in ROM

ROM starts at `0x00200000`. Recommended layout:

```
0x00200000 – 0x002FFFFF : Compiled ASM code (~1 MB)
0x00300000 – 0x007FFFFF : Graphics data
0x00800000 – 0x00FFFFFF : PCM audio data
0x01000000 – 0x03FFFFFF : Reserved / expansion
```

### Loading a ROM Asset into VRAM

```asm
load_tile_from_rom:
    LI  R1, 0x0000 : LIH R1, 0x0030   ; R1 = 0x00300000 (source in ROM)
    LI  R2, 0x0000 : LIH R2, 0x0008   ; R2 = 0x00080000 (dest in VRAM)
    LI  R3, 256
    MCPY
    RET
```

> MCPY routes automatically through `mem_read8`/`mem_write8`, which correctly
> handle ROM → VRAM transfers.

---

## 11. Complete Program Example

### Typical Main Loop

```asm
main_loop:
    VSYNC
    CALL update_sprite        ; D-PAD movement
    CALL check_buttons        ; Button sound (CH1 one-shot)
    CALL check_collision      ; Collision sound (CH0 loop)
    JMP main_loop
```

### Collision + Sound Pattern

```asm
; RAM 0x00000100 : sound_flag (0 = CH0 stopped, 1 = CH0 active)

check_collision:
    LDRI R7, 0x00100262       ; COLLISION_STATUS

    LI R6, 0x0001
    AND R7, R6                ; isolate bit 0

    LI R6, 0
    CMP R7, R6
    JZ col_none               ; 0 → no collision

; --- Collision active ---
col_hit:
    LDRI R6, 0x00000100       ; sound_flag
    LI R5, 1
    CMP R6, R5
    JNZ col_start_sound       ; flag != 1 → start CH0
    RET                       ; CH0 already active

col_start_sound:
    LI R0, 0x0003             ; play=1, loop=1, 8-bit
    STRI 0x00100910, R0       ; CH0 CTRL = play+loop
    LI R0, 1
    STRI 0x00000100, R0       ; sound_flag = 1
    RET

; --- No collision ---
col_none:
    LDRI R6, 0x00000100
    LI R5, 0
    CMP R6, R5
    JZ col_done               ; sound already stopped

    LI R0, 0x0000
    STRI 0x00100910, R0       ; stop CH0
    LI R0, 0
    STRI 0x00000100, R0       ; sound_flag = 0

col_done:
    RET
```

---

END OF DOCUMENTATION
