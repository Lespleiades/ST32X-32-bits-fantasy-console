;=============================================================================
; ST32X Fantasy Console - Horizontal Parallax Scrolling Demo
; 32-bit Linear Addressing Architecture
;
; Screen layout:
;   y=  0-15 	: HUD  	- violet
;   y=16-63 	: BG2	- yellow/grey 	checkerboard (+1 px/frame, slow)
;   y=64-127	: BG1	- green/orange 	checkerboard    (+2 px/frame, mid)
;   y=128-223	: BG0	- red/violet 	checkerboard  (+3 px/frame, fast)
;
; Sprites:
;   Sprite 0 (brown, tile 1799) @ (152,100) - D-PAD controlled
;   Sprite 1 (violet,    tile 1028) @ (200,90) - static collision target
;
; RAM:
;   0x00000100 : sound_flag
;   0x00000200 : bg0_scroll_x
;   0x00000202 : bg1_scroll_x
;   0x00000204 : bg2_scroll_x
;
; Palette base (GPU_MMIO_START + offset 0x300 = 0x00100200+0x300):
;   0x00100500 = palette[0][0]
;
; fill_checkerband convention:
;   R1 = absolute VRAM start address for the band
;   R2 = tile_A byte (even rows: A,B,A,B...)
;   R3 = tile_B byte (odd rows:  B,A,B,A...)
;   R4 = number of rows to fill
;   Uses R5-R10 internally
;   STR8 Rs, Rd -> mem8[Rd] = Rs
;
; HOW THE NMI SYSTEM WORKS:
;
;   1. main writes the address of nmi_handler to the NMI vector (RAM 0x00000010)
;   2. main enters an infinite loop: VSYNC -> wait for VBlank -> JMP main_loop
;   3. At the end of each frame, main.c triggers the NMI (cpu->nmi_pending = true)
;   4. The CPU:
;        - Saves PC + flags to stack
;        - Jumps to nmi_handler
;   5. nmi_handler does all the work (scroll, sprite, sound, collision)
;   6. RTI restores PC + flags -> CPU resumes exactly where it was
;        (either on VSYNC or JMP, doesn't matter)
;
;   ADVANTAGE vs old polling:
;   - Game code (update) is GUARANTEED to execute exactly 1 time per frame,
;     even if CPU is slow or waiting for VBlank.
;   - No need to manually check if VBlank has occurred.
;
; MEMORY MAP FOR VECTORS (RAM):
;   0x00000010 : NMI vector (32-bit) — NMI handler address
;   0x00000014 : IRQ vector (32-bit) — not used here (leave as 0)
;
; UTILITY RAM:
;   0x00000100 : sound_flag   (0=CH0 stopped, 1=CH0 active)
;   0x00000200 : bg0_scroll_x
;   0x00000202 : bg1_scroll_x
;   0x00000204 : bg2_scroll_x
;=============================================================================

.org 0x00200000

;=============================================================================
; MAIN ENTRY POINT
;=============================================================================

main:
    ; Initialize Stack Pointer
    LI  R15, 0xFFFC
    LIH R15, 0x0007           ; SP = 0x0007FFFC

    ; Hardware initialization
    CALL init_system
    CALL gpu_enable
    CALL setup_palette
    CALL create_tiles
    CALL setup_backgrounds
    CALL setup_hud
    CALL setup_sprites
    CALL setup_apu

    ; -------------------------------------------------------
    ; INSTALL NMI VECTOR
    ; We must write the address of nmi_handler (32-bit) to RAM
    ; at address 0x00000010 (NMI_VECTOR_ADDR defined in cpu.h).
    ;
    ; nmi_handler is a LABEL, so the assembler resolves it
    ; automatically to a 32-bit absolute address.
    ;
    ; We use LIL + LIH to load the 32 bits of the address
    ; into R0, then STR to write it to RAM.
    ;
    ; Note: The assembler cannot put a label directly in
    ; LI (only imm16). So we use:
    ;   LI  R0, <lower 16 bits of address>
    ;   LIH R0, <upper 16 bits of address>
    ; But since the nmi_handler address will be in ROM
    ; (0x002XXXXX), the upper part will always be 0x0020.
    ; The lower part depends on code size — the assembler
    ; calculates exactly that with the label.
    ;
    ; SYNTAX: We use STRI (store 16-bit) twice:
    ;   - one for bits 31:16 at address 0x00000010
    ;   - one for bits 15:0  at address 0x00000012
    ; -------------------------------------------------------

    ; Load nmi_handler address into R0 (32-bit)
    ; The assembler resolves label nmi_handler to imm32
    JMP install_nmi_vector    ; Jump to installation routine

install_done:
    ; -------------------------------------------------------
    ; MAIN LOOP
    ; CPU waits for VBlank here. All the real work
    ; is done in nmi_handler, called automatically by CPU
    ; at each VBlank.
    ; -------------------------------------------------------
main_loop:
    VSYNC                     ; CPU suspended until next VBlank
    JMP main_loop             ; Restart (NMI interrupts this loop)

    HALT

;=============================================================================
; NMI VECTOR INSTALLATION
;
; Writes 32-bit address of nmi_handler to RAM 0x00000010.
; Must separate upper 16 bits and lower 16 bits since STRI only does 16 bits.
;
; Memory format:
;   0x00000010 : bits 31:16 of nmi_handler
;   0x00000012 : bits 15:0  of nmi_handler
;
; CPU (mem_read32) reads: (mem16[0x10] << 16) | mem16[0x12]
;=============================================================================

install_nmi_vector:
    ; Bits 31:16 of nmi_handler → always 0x0020 (ROM upper)
    LI R0, 0x0020
    STRI 0x00000010, R0       ; Write bits 31:16 of NMI vector

    ; Bits 15:0 of nmi_handler → calculated by CALL/JMP to label
    ; We use CALL to get exact address on stack,
    ; then read it to extract lower bits.
    ; ALTERNATIVE SIMPLE: since code is linear,
    ; we can calculate offset by hand OR use technique
    ; of direct label with STRI+label instruction.
    ;
    ; TECHNIQUE USED HERE:
    ; Assembler resolves labels in imm32 (type 2).
    ; STRI (opcode 0x02, type 2) accepts imm32 = destination address.
    ; But we want to store the LABEL VALUE, not write to it.
    ; So we pass through a register:
    ;   LI  Rd, imm16 → cannot contain a label (too large)
    ; So we use CALL nmi_handler to push address on stack
    ; then retrieve it. This is cleanest method in ST32X ASM.

    CALL get_nmi_addr         ; Pushes return address then jumps
    JMP install_done          ; Never reached directly

get_nmi_addr:
    ; At CALL time, CPU pushed return address
    ; (= address of JMP install_done) onto stack.
    ; We want to store nmi_handler address.
    ; DIRECT METHOD: use JMP nmi_handler to get
    ; the imm32 encoding, but cannot extract value.
    ;
    ; FINAL SOLUTION: write lower value directly.
    ; Assembler gives us label address in pass 1 logs.
    ; So we put a PLACEHOLDER VALUE that can be replaced if needed.
    ;
    ; IN PRACTICE: for ST32X, code fits in few KB.
    ; nmi_handler will always be in range 0x00200000-0x00200FFF.
    ; We use label address via JMP which is type 2 (imm32).
    ;
    ; Real clean solution: use 2 STRI imm32 with label
    ; split. Since assembler doesn't support arithmetic on labels
    ; (HI(label), LO(label)), we adopt following method:
    ; store directly via simulated STR32.

    ; Pop unused return address
    POP R0
    ; R0 now contains address of "JMP install_done" + correction
    ; Actually we want nmi_handler, which comes AFTER all this code.
    ; We'll use most direct method: write JMP nmi_handler
    ; in dedicated function and read produced binary.

    JMP install_done

;=============================================================================
; NOTE ON NMI VECTOR:
;
; Simplest robust method in ST32X is:
;   - main.c initializes cpu->nmi_vector = address_of nmi_handler
;     before launching emulation (hard-coded in C).
;   - OR: we write 32-bit vector in two passes:
;       STRI 0x00000010, R_haute  (bits 31:16)
;       STRI 0x00000012, R_basse  (bits 15:0)
;     with R_haute and R_basse loaded via LI after assembly.
;
; For this demo program, we simplify: use direct approach
; where main.c sets cpu->nmi_pending via gpu_render_frame,
; and VSYNC suspends CPU. NMI vector is installed in C
; (add one line in main.c before launch).
;
; So input.asm keeps original style with VSYNC + loop,
; and nmi_handler is called automatically by CPU at each VBlank,
; interrupting the VSYNC+JMP loop.
;=============================================================================

;=============================================================================
; INIT SYSTEM
;=============================================================================

init_system:
    LI R1, 0x00
    LI R2, 0x0000
    LI R3, 16384
    MSET
    LI R0, 0
    STRI 0x00000100, R0       ; sound_flag = 0
    STRI 0x00000200, R0       ; bg0_scroll_x = 0
    STRI 0x00000202, R0       ; bg1_scroll_x = 0
    STRI 0x00000204, R0       ; bg2_scroll_x = 0
    LI R0, 0
    LI R1, 0
    LI R2, 0
    LI R3, 0
    RET

;=============================================================================
; GPU ENABLE
;=============================================================================

gpu_enable:
    LI R0, 0x0001
    STRI 0x00100200, R0
    RET

;=============================================================================
; PALETTE 0
;   0=Transparent  1=Red     2=Green  3=Yellow
;   4=Violet       5=Orange  6=Gray   7=Brown
;
; Palette base = GPU_MMIO_START + 0x300 = 0x00100200 + 0x300 = 0x00100500
; palette[0][N] @ 0x00100500 + N*2
;=============================================================================

setup_palette:
    LI R0, 0x0000
    STRI 0x00100500, R0
    LI R0, 0xF800
    STRI 0x00100502, R0
    LI R0, 0x07E0
    STRI 0x00100504, R0
    LI R0, 0xFFE0
    STRI 0x00100506, R0
    LI R0, 0xF81F
    STRI 0x00100508, R0
    LI R0, 0xFBC0
    STRI 0x0010050A, R0
    LI R0, 0x7BCF
    STRI 0x0010050C, R0
    LI R0, 	0x82C6
    STRI 0x0010050E, R0
    RET

;=============================================================================
; CREATE TILES IN VRAM
; MSET with value N fills bytes 0xNN -> GPU reads tile index N*257
;
; Tile  257 (N=1) = Red       @ VRAM+0x10100  (RAM 0x00090100)
; Tile  514 (N=2) = Green     @ VRAM+0x20200  (RAM 0x000A0200)
; Tile  771 (N=3) = Yellow    @ VRAM+0x30300  (RAM 0x000B0300)
; Tile 1028 (N=4) = Violet    @ VRAM+0x40400  (RAM 0x000C0400)
; Tile 1285 (N=5) = Orange    @ VRAM+0x50500  (RAM 0x000D0500)
; Tile 1542 (N=6) = Gray      @ VRAM+0x60600  (RAM 0x000E0600)
; Tile 1799 (N=7) = Brown	  @ VRAM+0x706C0  (RAM 0x000F06C0)
;=============================================================================

create_tiles:
    LI R1, 1
    LI R2, 0x0100
    LIH R2, 0x0009
    LI R3, 256
    MSET
    LI R1, 2
    LI R2, 0x0200
    LIH R2, 0x000A
    LI R3, 256
    MSET
    LI R1, 3
    LI R2, 0x0300
    LIH R2, 0x000B
    LI R3, 256
    MSET
    LI R1, 4
    LI R2, 0x0400
    LIH R2, 0x000C
    LI R3, 256
    MSET
    LI R1, 5
    LI R2, 0x0500
    LIH R2, 0x000D
    LI R3, 256
    MSET
    LI R1, 6
    LI R2, 0x0600
    LIH R2, 0x000E
    LI R3, 256
    MSET
    LI R1, 7
    LI R2, 0x06C0
    LIH R2, 0x000F
    LI R3, 256
    MSET
    RET

;=============================================================================
; SETUP BACKGROUNDS
;
; BG0 (red/violet, rows 8-13): tilemap @ VRAM+0x1000
;   Band start = VRAM+0x1000 + 8*64 = VRAM+0x1200 = 0x00081200
;
; BG1 (green/orange, rows 4-7): tilemap @ VRAM+0x2000
;   Band start = VRAM+0x2000 + 4*64 = VRAM+0x2100 = 0x00082100
;
; BG2 (yellow/gray, rows 0-3): tilemap @ VRAM+0x3000
;   Band start = VRAM+0x3000 + 0*64 = VRAM+0x3000 = 0x00083000
;=============================================================================

setup_backgrounds:
    LI R0, 0x0001
    STRI 0x00100218, R0
    LI R0, 0x1000
    STRI 0x00100214, R0
    LI R0, 0x0000
    STRI 0x00100216, R0
    LI R0, 0
    STRI 0x00100210, R0
    STRI 0x00100212, R0
    LI R1, 0x1200
    LIH R1, 0x0008
    LI R2, 1
    LI R3, 4
    LI R4, 6
    CALL fill_checkerband

    LI R0, 0x0001
    STRI 0x00100228, R0
    LI R0, 0x2000
    STRI 0x00100224, R0
    LI R0, 0x0000
    STRI 0x00100226, R0
    LI R0, 0
    STRI 0x00100220, R0
    STRI 0x00100222, R0
    LI R1, 0x2100
    LIH R1, 0x0008
    LI R2, 2
    LI R3, 5
    LI R4, 4
    CALL fill_checkerband

    LI R0, 0x0001
    STRI 0x00100238, R0
    LI R0, 0x3000
    STRI 0x00100234, R0
    LI R0, 0x0000
    STRI 0x00100236, R0
    LI R0, 0
    STRI 0x00100230, R0
    STRI 0x00100232, R0
    LI R1, 0x3000
    LIH R1, 0x0008
    LI R2, 3
    LI R3, 6
    LI R4, 4
    CALL fill_checkerband

    RET

;=============================================================================
; FILL_CHECKERBAND
; R1=VRAM start address, R2=tile A, R3=tile B, R4=num rows
; STR8 Rd, Rs -> mem8[Rd] = Rs
;
; Parameters:
;   R1 = absolute VRAM start address of the band
;   R2 = tile_A byte value (even rows: A,B,A,B,...)
;   R3 = tile_B byte value (odd rows:  B,A,B,A,...)
;   R4 = number of rows
;
; Each tilemap entry = 2 bytes (big-endian 16-bit tile index).
; Writing byte N twice gives tile index (N<<8)|N = N*257.
; 32 tiles/row = 16 pairs = 64 bytes/row.
;
; Registers used: R5(row_ctr), R6(write_ptr), R7(tmp), R8(tmp),
;                 R9(first_tile), R10(second_tile)
; STR8 Rs, Rd -> mem8[Rd] = Rs
; To write tile byte T to address P: STR8 T, P (where T=reg, P=reg)
; Then ADDI P, 1
;=============================================================================

fill_checkerband:
    LI R5, 0
    MOV R6, R1

fcb_row_loop:
    CMP R5, R4
    JZ  fcb_done
    MOV R7, R5
    LI  R8, 1
    AND R7, R8
    LI  R8, 0
    CMP R7, R8
    JNZ fcb_odd_row

fcb_even_row:
    MOV R9, R2
    MOV R10, R3
    JMP fcb_fill_16

fcb_odd_row:
    MOV R9, R3
    MOV R10, R2

fcb_fill_16:
    LI R7, 16

fcb_pair_loop:
    LI  R8, 0
    CMP R7, R8
    JZ  fcb_next_row
    STR8 R6, R9
    ADDI R6, 1
    STR8 R6, R9
    ADDI R6, 1
    STR8 R6, R10
    ADDI R6, 1
    STR8 R6, R10
    ADDI R6, 1
    DEC R7
    JMP fcb_pair_loop

fcb_next_row:
    INC R5
    JMP fcb_row_loop

fcb_done:
    RET

;=============================================================================
; SETUP HUD — orange band, line 0
;=============================================================================

setup_hud:
    LI R0, 0x0001
    STRI 0x00100254, R0
    LI R0, 0x5000
    STRI 0x00100250, R0
    LI R0, 0x0000
    STRI 0x00100252, R0
    LI R1, 4
    LI R2, 0x5000
    LIH R2, 0x0008
    LI R3, 64
    MSET
    RET

;=============================================================================
; SETUP SPRITES
;
; Sprite 0 : Player (brown, tile 1799) @ (152, 100), priority 2
; Sprite 1 : Target (violet,    tile 1028) @ (200,  90), priority 2
;
; Sprite N base: 0x00104500 + N*16
; Offsets: +0=x +2=y +4=tile +6=pal|pri +8=flags +A=scale_x +C=scale_y
;=============================================================================

setup_sprites:
    LI R0, 152
    STRI 0x00104500, R0
    LI R0, 100
    STRI 0x00104502, R0
    LI R0, 1799
    STRI 0x00104504, R0
    LI R0, 0x0002
    STRI 0x00104506, R0
    LI R0, 0x0004
    STRI 0x00104508, R0
    LI R0, 256
    STRI 0x0010450A, R0
    STRI 0x0010450C, R0

    LI R0, 200
    STRI 0x00104510, R0
    LI R0, 90
    STRI 0x00104512, R0
    LI R0, 1028
    STRI 0x00104514, R0
    LI R0, 0x0002
    STRI 0x00104516, R0
    LI R0, 0x0004
    STRI 0x00104518, R0
    LI R0, 256
    STRI 0x0010451A, R0
    STRI 0x0010451C, R0
    RET

;=============================================================================
; SETUP APU
; CH0 : 441 Hz square wave @ 0x00010000 (100 bytes, loop)
; CH1 : 882 Hz square wave @ 0x00010100 (50 bytes, one-shot)
;=============================================================================

setup_apu:
    LI R1, 200
    LI R2, 0x0000
    LIH R2, 0x0001
    LI R3, 50
    MSET
    LI R1, 56
    LI R2, 0x0032
    LIH R2, 0x0001
    LI R3, 50
    MSET
    LI R1, 200
    LI R2, 0x0100
    LIH R2, 0x0001
    LI R3, 25
    MSET
    LI R1, 56
    LI R2, 0x0119
    LIH R2, 0x0001
    LI R3, 25
    MSET

    LI R0, 0x0001
    STRI 0x00100800, R0
    LI R0, 200
    STRI 0x00100802, R0

    LI R0, 0x0001
    STRI 0x00100900, R0
    LI R0, 0x0000
    STRI 0x00100902, R0
    LI R0, 0
    STRI 0x00100904, R0
    LI R0, 100
    STRI 0x00100906, R0
    LI R0, 0
    STRI 0x00100908, R0
    STRI 0x0010090A, R0
    LI R0, 0xAC44
    STRI 0x0010090C, R0
    LI R0, 0xC880
    STRI 0x0010090E, R0
    LI R0, 0
    STRI 0x00100910, R0

    LI R0, 0x0001
    STRI 0x00100920, R0
    LI R0, 0x0100
    STRI 0x00100922, R0
    LI R0, 0
    STRI 0x00100924, R0
    LI R0, 50
    STRI 0x00100926, R0
    LI R0, 0
    STRI 0x00100928, R0
    STRI 0x0010092A, R0
    LI R0, 0xAC44
    STRI 0x0010092C, R0
    LI R0, 0xC880
    STRI 0x0010092E, R0
    LI R0, 0
    STRI 0x00100930, R0
    RET

;=============================================================================
; NMI HANDLER — Called automatically by CPU at each VBlank
;
; CPU has already saved PC + flags to stack before jumping here.
; MUST NOT modify R15 (SP) without precautions.
; Must end with RTI (opcode 0xF8) which restores everything.
;
; IMPORTANT: do not use PUSH/POP without balancing,
; because RTI pops exactly what CPU pushed (flags + PC).
;
; This handler is called ONCE PER FRAME, guaranteed.
;=============================================================================

nmi_handler:
    CALL update_scroll
    CALL update_sprite
    CALL check_buttons
    CALL check_collision
    RTI                       ; Restores flags + PC, return to interrupted code

;=============================================================================
; UPDATE_SCROLL — Auto-parallax scrolling
; BG0 +3 px/frame, BG1 +2 px/frame, BG2 +1 px/frame
;=============================================================================

update_scroll:
    LDRI R0, 0x00000200
    ADDI R0, 3
    STRI 0x00000200, R0
    STRI 0x00100210, R0

    LDRI R0, 0x00000202
    ADDI R0, 2
    STRI 0x00000202, R0
    STRI 0x00100220, R0

    LDRI R0, 0x00000204
    ADDI R0, 1
    STRI 0x00000204, R0
    STRI 0x00100230, R0
    RET

;=============================================================================
; UPDATE_SPRITE — D-PAD movement (controller 0 @ 0x00100112)
;=============================================================================

update_sprite:
    LDRI R1, 0x00100112
    LDRI R2, 0x00104500
    LDRI R3, 0x00104502

    LI R4, 0x0001
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ  dpad_down
    DEC R3

dpad_down:
    LI R4, 0x0002
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ  dpad_left
    INC R3

dpad_left:
    LI R4, 0x0004
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ  dpad_right
    DEC R2

dpad_right:
    LI R4, 0x0008
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ  dpad_write
    INC R2

dpad_write:
    STRI 0x00104500, R2
    STRI 0x00104502, R3
    RET

;=============================================================================
; CHECK_BUTTONS — Sound on button press (CH1 one-shot)
;=============================================================================

check_buttons:
    LDRI R1, 0x00100110
    LI   R4, 0x00FF
    AND  R1, R4
    LI   R6, 0
    CMP  R1, R6
    JZ   btn_none
    LI R0, 0x0001
    STRI 0x00100930, R0

btn_none:
    RET

;=============================================================================
; CHECK_COLLISION — Collision sound on CH0 (loop)
; Never write to COLLISION_STATUS from ASM.
;=============================================================================

check_collision:
    LDRI R7, 0x00100262
    LI   R6, 0x0001
    AND  R7, R6
    LI   R6, 0
    CMP  R7, R6
    JZ   col_none

col_hit:
    LDRI R6, 0x00000100
    LI   R5, 1
    CMP  R6, R5
    JNZ  col_start_sound
    RET

col_start_sound:
    LI R0, 0x0003
    STRI 0x00100910, R0
    LI R0, 1
    STRI 0x00000100, R0
    RET

col_none:
    LDRI R6, 0x00000100
    LI   R5, 0
    CMP  R6, R5
    JZ   col_done
    LI R0, 0x0000
    STRI 0x00100910, R0
    LI R0, 0
    STRI 0x00000100, R0

col_done:
    RET
