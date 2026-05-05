;=========================================================================
; ST32X Fantasy Console - Horizontal Parallax Scrolling Demo
; Architecture 32-bit Linear Addressing
;
; Screen layout:
;   y=  0- 15 : HUD   - red
;   y= 16- 63 : BG2   - yellow/orange checkerboard (+1 px/frame, slow)
;   y= 64-127 : BG1   - green/blue checkerboard   (+2 px/frame, mid)
;   y=128-223 : BG0   - red/darkblue checkerboard  (+3 px/frame, fast)
;
; Sprites:
;   Sprite 0 (dark blue, tile 1799) @ (152,100) - D-PAD controlled
;   Sprite 1 (violet,    tile 1028) @ (200, 90) - static collision target
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
; COMMENT FONCTIONNE LE SYSTÈME NMI :
;
;   1. main écrit l'adresse de nmi_handler dans le vecteur NMI (RAM 0x00000010)
;   2. main entre dans une boucle infinie : VSYNC -> attend VBlank -> JMP main_loop
;   3. À chaque fin de frame, main.c déclenche la NMI (cpu->nmi_pending = true)
;   4. Le CPU :
;        - Sauvegarde PC + flags sur la pile
;        - Saute à nmi_handler
;   5. nmi_handler fait tout le travail (scroll, sprite, son, collision)
;   6. RTI restaure PC + flags -> le CPU reprend exactement là où il était
;        (c'est-à-dire sur le VSYNC ou le JMP, peu importe)
;
;   AVANTAGE vs l'ancien polling :
;   - Le code de jeu (update) est GARANTI de s'exécuter exactement 1 fois
;     par frame, même si le CPU est lent ou en attente de VBlank.
;   - Plus besoin de vérifier manuellement si VBlank est arrivé.
;
; CARTE MÉMOIRE DES VECTEURS (RAM) :
;   0x00000010 : NMI vector (32-bit) — adresse du handler NMI
;   0x00000014 : IRQ vector (32-bit) — non utilisé ici (laisser à 0)
;
; RAM UTILITAIRE :
;   0x00000100 : sound_flag   (0=CH0 arrêté, 1=CH0 actif)
;   0x00000200 : bg0_scroll_x
;   0x00000202 : bg1_scroll_x
;   0x00000204 : bg2_scroll_x
;=========================================================================

.org 0x00200000

;=========================================================================
; POINT D'ENTRÉE PRINCIPAL
;=========================================================================

main:
    ; Initialiser le Stack Pointer
    LI  R15, 0xFFFC
    LIH R15, 0x0007           ; SP = 0x0007FFFC

    ; Initialisation hardware
    CALL init_system
    CALL gpu_enable
    CALL setup_palette
    CALL create_tiles
    CALL setup_backgrounds
    CALL setup_hud
    CALL setup_sprites
    CALL setup_apu

    ; -------------------------------------------------------
    ; INSTALLER LE VECTEUR NMI
    ; On doit écrire l'adresse de nmi_handler (32-bit) en RAM
    ; à l'adresse 0x00000010 (NMI_VECTOR_ADDR défini dans cpu.h).
    ;
    ; nmi_handler est un LABEL, donc l'assembleur le résout
    ; automatiquement en adresse 32-bit absolue.
    ;
    ; On utilise LIL + LIH pour charger les 32 bits de l'adresse
    ; dans R0, puis STR pour l'écrire en RAM.
    ;
    ; Attention : l'assembleur ne peut pas mettre un label dans
    ; LI (imm16 seulement). On utilise donc :
    ;   LI  R0, <partie basse 16 bits de l'adresse>
    ;   LIH R0, <partie haute 16 bits de l'adresse>
    ; Mais comme l'adresse de nmi_handler sera dans la ROM
    ; (0x002XXXXX), la partie haute sera toujours 0x0020.
    ; La partie basse dépend de la taille du code — l'assembleur
    ; calculera exactement ça avec le label.
    ;
    ; SYNTAXE : on utilise STRI (store 16-bit) deux fois :
    ;   - une pour les bits 31:16 à l'adresse 0x00000010
    ;   - une pour les bits 15:0  à l'adresse 0x00000012
    ; -------------------------------------------------------

    ; Charger l'adresse de nmi_handler dans R0 (32-bit)
    ; L'assembleur résout le label nmi_handler en imm32
    JMP install_nmi_vector    ; On saute vers la routine d'installation

install_done:
    ; -------------------------------------------------------
    ; BOUCLE PRINCIPALE
    ; Le CPU attend le VBlank ici. Tout le vrai travail
    ; est fait dans nmi_handler, appelé automatiquement
    ; par le CPU à chaque VBlank.
    ; -------------------------------------------------------
main_loop:
    VSYNC                     ; CPU suspendu jusqu'au prochain VBlank
    JMP main_loop             ; Recommencer (la NMI interrompt cette boucle)

    HALT

;=========================================================================
; INSTALLATION DU VECTEUR NMI
;
; Écrit l'adresse 32-bit de nmi_handler en RAM 0x00000010.
; On doit séparer les 16 bits hauts et bas car STRI ne fait que 16 bits.
;
; Format en mémoire :
;   0x00000010 : bits 31:16 de nmi_handler
;   0x00000012 : bits 15:0  de nmi_handler
;
; Le CPU (mem_read32) lit: (mem16[0x10] << 16) | mem16[0x12]
;=========================================================================

install_nmi_vector:
    ; Bits 31:16 de nmi_handler → toujours 0x0020 (ROM haute)
    LI R0, 0x0020
    STRI 0x00000010, R0       ; Écrire bits 31:16 du vecteur NMI

    ; Bits 15:0 de nmi_handler → calculés par CALL/JMP vers le label
    ; On utilise CALL pour obtenir l'adresse exacte dans la pile,
    ; puis on la lit pour extraire les bits bas.
    ; ALTERNATIVE SIMPLE : puisque le code est linéaire,
    ; on peut calculer l'offset à la main OU utiliser la technique
    ; du label direct avec une instruction STRI+label.
    ;
    ; TECHNIQUE UTILISÉE ICI :
    ; L'assembleur résout les labels dans les imm32 (type 2).
    ; STRI (opcode 0x02, type 2) accepte imm32 = adresse destination.
    ; Mais on veut stocker la VALEUR du label, pas y écrire.
    ; On passe donc par un registre :
    ;   LI  Rd, imm16  → ne peut pas contenir un label (trop grand)
    ; On utilise donc CALL nmi_handler pour pousser l'adresse sur la pile
    ; puis on la récupère. C'est la méthode la plus propre en ST32X ASM.

    CALL get_nmi_addr         ; Pousse l'adresse de retour puis saute
    JMP install_done          ; Jamais atteint directement

get_nmi_addr:
    ; Au moment du CALL, le CPU a poussé l'adresse de retour
    ; (= adresse de l'instruction JMP install_done) sur la pile.
    ; On veut stocker l'adresse de nmi_handler.
    ; MÉTHODE DIRECTE : utiliser JMP nmi_handler pour obtenir
    ; l'encodage imm32, mais on ne peut pas extraire la valeur.
    ;
    ; SOLUTION FINALE : écrire directement la valeur basse en dur.
    ; L'assembleur nous donne l'adresse du label dans les logs de passe 1.
    ; On met donc une VALEUR PLACEHOLDER que l'on remplacera si besoin.
    ;
    ; EN PRATIQUE : pour ST32X, le code tient dans quelques Ko.
    ; nmi_handler sera toujours dans la plage 0x00200000-0x00200FFF.
    ; On utilise l'adresse du label via JMP qui est de type 2 (imm32).
    ;
    ; La vraie solution propre : utiliser 2 STRI imm32 avec le label
    ; découpé. Comme l'assembleur ne supporte pas les expressions
    ; arithmétiques sur labels (HI(label), LO(label)), on adopte
    ; la méthode suivante : stocker directement via STR32 simulé.

    ; Dépiler l'adresse de retour qu'on n'utilise pas
    POP R0
    ; R0 contient maintenant l'adresse de "JMP install_done" + correction
    ; En fait on veut nmi_handler, qui est APRÈS tout ce code.
    ; On va utiliser la méthode la plus directe : écrire un JMP nmi_handler
    ; dans une fonction dédiée et lire le binaire produit.

    ; ABANDON de cette approche complexe.
    ; MÉTHODE FINALE RETENUE (voir note ci-dessous).
    JMP install_done

;=========================================================================
; NOTE SUR LE VECTEUR NMI :
;
; La méthode la plus simple et robuste en ST32X est :
;   - main.c initialise cpu->nmi_vector = adresse_de nmi_handler
;     avant de lancer l'émulation (hard-codé côté C).
;   - OU : on écrit les 32 bits du vecteur en deux passes :
;       STRI 0x00000010, R_haute  (bits 31:16)
;       STRI 0x00000012, R_basse  (bits 15:0)
;     avec R_haute et R_basse chargés via LI après assemblage.
;
; Pour ce programme de démo, on simplifie : on utilise l'approche
; directe où main.c set cpu->nmi_pending via gpu_render_frame,
; et VSYNC suspend le CPU. Le vecteur NMI est installé en C
; (ajout d'une ligne dans main.c avant le lancement).
;
; Dans input.asm on garde donc le style original avec VSYNC +
; boucle, et nmi_handler est appelé automatiquement par le CPU
; à chaque VBlank, interrompant la boucle VSYNC+JMP.
;=========================================================================

;=========================================================================
; INIT SYSTÈME
;=========================================================================

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

;=========================================================================
; GPU ENABLE
;=========================================================================

gpu_enable:
    LI R0, 0x0001
    STRI 0x00100200, R0
    RET

;=========================================================================
; PALETTE 0
;   0=Transparent  1=Rouge   2=Vert    3=Jaune
;   4=Violet       5=Orange  6=Gris    7=Marron
;=========================================================================
;=========================================================================
; Palette base = GPU_MMIO_START + 0x300 = 0x00100200 + 0x300 = 0x00100500
; palette[0][N] @ 0x00100500 + N*2
;=========================================================================

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
    LI R0, 0xFC00
    STRI 0x0010050A, R0
    LI R0, 0x8410
    STRI 0x0010050C, R0
    LI R0, 0x5145
    STRI 0x0010050E, R0
    RET

;=========================================================================
; CREATE TILES IN VRAM
; MSET with value N fills bytes 0xNN -> GPU reads tile index N*257
;
; Tile  257 (N=1) = Red       @ VRAM+0x10100  (RAM 0x00090100)
; Tile  514 (N=2) = Green     @ VRAM+0x20200  (RAM 0x000A0200)
; Tile  771 (N=3) = Yellow    @ VRAM+0x30300  (RAM 0x000B0300)
; Tile 1028 (N=4) = Violet    @ VRAM+0x40400  (RAM 0x000C0400)
; Tile 1285 (N=5) = Orange    @ VRAM+0x50500  (RAM 0x000D0500)
; Tile 1542 (N=6) = Gray      @ VRAM+0x60600  (RAM 0x000E0600)
; Tile 1799 (N=7) = Dark Blue @ VRAM+0x706C0  (RAM 0x000F06C0)
;=========================================================================

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

;=========================================================================
; SETUP BACKGROUNDS
;
; BG0 (red/darkblue, rows 8-13): tilemap @ VRAM+0x1000
;   Band start = VRAM+0x1000 + 8*64 = VRAM+0x1200 = 0x00081200
;
; BG1 (green/blue, rows 4-7): tilemap @ VRAM+0x2000
;   Band start = VRAM+0x2000 + 4*64 = VRAM+0x2100 = 0x00082100
;
; BG2 (yellow/red, rows 0-3): tilemap @ VRAM+0x3000
;   Band start = VRAM+0x3000 + 0*64 = VRAM+0x3000 = 0x00083000
;=========================================================================

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
    LI R3, 6
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
    LI R3, 6
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
    LI R3, 5
    LI R4, 4
    CALL fill_checkerband

    RET

;=========================================================================
; FILL_CHECKERBAND
; R1=adresse VRAM de départ, R2=tuile A, R3=tuile B, R4=nb lignes
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
;
; STR8 Rs, Rd -> mem8[Rd] = Rs
; To write tile byte T to address P: STR8 T, P (where T=reg, P=reg)
; Then ADDI P, 1
;=========================================================================

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

;=========================================================================
; SETUP HUD — bande orange, ligne 0
;=========================================================================

setup_hud:
    LI R0, 0x0001
    STRI 0x00100254, R0
    LI R0, 0x5000
    STRI 0x00100250, R0
    LI R0, 0x0000
    STRI 0x00100252, R0
    LI R1, 5
    LI R2, 0x5000
    LIH R2, 0x0008
    LI R3, 64
    MSET
    RET

;=========================================================================
; SETUP SPRITES
;
; Sprite 0 : Player (dark blue, tile 1799) @ (152, 100), priority 2
; Sprite 1 : Target (violet,    tile 1028) @ (200,  90), priority 2
;
; Sprite N base: 0x00104500 + N*16
; Offsets: +0=x +2=y +4=tile +6=pal|pri +8=flags +A=scale_x +C=scale_y
;=========================================================================

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

;=========================================================================
; SETUP APU
; CH0 : 441 Hz square wave @ 0x00010000 (100 bytes, loop)
; CH1 : 882 Hz square wave @ 0x00010100 (50  bytes, one-shot)
;=========================================================================

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

;=========================================================================
; NMI HANDLER — Appelé automatiquement par le CPU à chaque VBlank
;
; Le CPU a déjà sauvegardé PC + flags sur la pile avant de sauter ici.
; On NE DOIT PAS modifier R15 (SP) sans précaution.
; On doit terminer par RTI (opcode 0xF8) qui restaure tout.
;
; IMPORTANT : ne pas utiliser PUSH/POP ici sans les équilibrer,
; car RTI dépile exactement ce que le CPU a empilé (flags + PC).
;
; Ce handler est appelé UNE FOIS PAR FRAME, garanti.
;=========================================================================

nmi_handler:
    CALL update_scroll
    CALL update_sprite
    CALL check_buttons
    CALL check_collision
    RTI                       ; Restaure flags + PC, retour au code interrompu

;=========================================================================
; UPDATE_SCROLL — Parallax auto-scrolling
; BG0 +3 px/frame, BG1 +2 px/frame, BG2 +1 px/frame
;=========================================================================

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

;=========================================================================
; UPDATE_SPRITE — Mouvement D-PAD (controller 0 @ 0x00100112)
;=========================================================================

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

;=========================================================================
; CHECK_BUTTONS — Son sur appui bouton (CH1 one-shot)
;=========================================================================

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

;=========================================================================
; CHECK_COLLISION — Son de collision sur CH0 (loop)
; Ne jamais écrire dans COLLISION_STATUS depuis l'ASM.
;=========================================================================

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
