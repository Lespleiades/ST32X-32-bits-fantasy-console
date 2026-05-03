;=========================================================================
; ST32X Fantasy Console - Backgrounds + HUD + 2 Sprites + Collision + Audio
; Architecture 32-bit Linear Addressing
;
; Disposition écran :
;   y=  0- 15 : HUD   (orange)
;   y= 16- 63 : BG2   (jaune)
;   y= 64-127 : BG1   (vert)
;   y=128-223 : BG0   (rouge)
;
; Sprites :
;   Sprite 0 (bleu foncé) @ (152, 100) → contrôlable au D-PAD
;   Sprite 1 (violet)     @ (200,  90) → cible statique
;
; Sons :
;   Collision → CH0 441 Hz grave (loop tant que collision)
;   Bouton    → CH1 882 Hz aigu  (one-shot, tous les boutons A/B/X/Y/L/R/Select/Start)
;
; IMPORTANT — Gestion COLLISION_STATUS :
;   gpu_update_collisions() (appelé dans gpu_render_frame) est le seul
;   gestionnaire de ce registre. Il le remet à 0 en début de chaque frame
;   puis le met à 1 si collision détectée.
;   L'ASM ne doit JAMAIS écrire dans COLLISION_STATUS (évite le bug
;   de clignotement quand main_loop tourne plusieurs fois par frame hôte).
;
; RAM utilitaire :
;   0x00000100 : sound_flag (0=CH0 arrêté, 1=CH0 actif)
;=========================================================================

.org 0x00200000

;=========================================================================
; POINT D'ENTRÉE
;=========================================================================

main:
    LI  R15, 0xFFFC
    LIH R15, 0x0007           ; SP = 0x0007FFFC

    CALL init_system
    CALL test_gpu_init
    CALL setup_palette
    CALL create_tiles
    CALL setup_backgrounds
    CALL setup_foreground
    CALL setup_hud
    CALL setup_sprites
    CALL setup_apu
    CALL main_loop
    HALT

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

    LI R0, 0
    LI R1, 0
    LI R2, 0
    LI R3, 0
    RET

;=========================================================================
; GPU — activation globale
;=========================================================================

test_gpu_init:
    LI R0, 0x0001
    STRI 0x00100200, R0
    RET

;=========================================================================
; PALETTE 0
;   0=Transparent  1=Rouge  2=Vert  3=Jaune
;   4=Violet       5=Orange 6=Gris  7=Bleu foncé
;=========================================================================

setup_palette:
    LI R0, 0x0000
    STRI 0x00100500, R0

    LI R0, 0xF800
    STRI 0x00100502, R0       ; 1 = Rouge

    LI R0, 0x07E0
    STRI 0x00100504, R0       ; 2 = Vert

    LI R0, 0xFFE0
    STRI 0x00100506, R0       ; 3 = Jaune

    LI R0, 0xF81F
    STRI 0x00100508, R0       ; 4 = Violet

    LI R0, 0xFC00
    STRI 0x0010050A, R0       ; 5 = Orange

    LI R0, 0x8410
    STRI 0x0010050C, R0       ; 6 = Gris

    LI R0, 0x000F
    STRI 0x0010050E, R0       ; 7 = Bleu foncé
    RET

;=========================================================================
; CRÉATION DES TUILES EN VRAM
;=========================================================================

create_tiles:
    LI R1, 1                  ; Rouge  → tuile 0x0101 = 257
    LI R2, 0x0100
    LIH R2, 0x0009
    LI R3, 256
    MSET

    LI R1, 2                  ; Vert   → tuile 0x0202 = 514
    LI R2, 0x0200
    LIH R2, 0x000A
    LI R3, 256
    MSET

    LI R1, 3                  ; Jaune  → tuile 0x0303 = 771
    LI R2, 0x0300
    LIH R2, 0x000B
    LI R3, 256
    MSET

    LI R1, 4                  ; Violet → tuile 0x0404 = 1028
    LI R2, 0x0400
    LIH R2, 0x000C
    LI R3, 256
    MSET

    LI R1, 5                  ; Orange → tuile 0x0505 = 1285
    LI R2, 0x0500
    LIH R2, 0x000D
    LI R3, 256
    MSET

    LI R1, 7                  ; Bleu f → tuile 0x0707 = 1799 (offset 0x706C0)
    LI R2, 0x06C0
    LIH R2, 0x000F
    LI R3, 256
    MSET

    RET

;=========================================================================
; BACKGROUNDS
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
    CALL fill_tilemap_bg0

    LI R0, 0x0001
    STRI 0x00100228, R0
    LI R0, 0x2000
    STRI 0x00100224, R0
    LI R0, 0x0000
    STRI 0x00100226, R0
    LI R0, 0
    STRI 0x00100220, R0
    STRI 0x00100222, R0
    CALL fill_tilemap_bg1

    LI R0, 0x0001
    STRI 0x00100238, R0
    LI R0, 0x3000
    STRI 0x00100234, R0
    LI R0, 0x0000
    STRI 0x00100236, R0
    LI R0, 0
    STRI 0x00100230, R0
    STRI 0x00100232, R0
    CALL fill_tilemap_bg2

    RET

fill_tilemap_bg0:             ; Rouge, lignes 8-13
    LI R1, 1
    LI R2, 0x1200
    LIH R2, 0x0008
    LI R3, 384
    MSET
    RET

fill_tilemap_bg1:             ; Vert, lignes 4-7
    LI R1, 2
    LI R2, 0x2100
    LIH R2, 0x0008
    LI R3, 256
    MSET
    RET

fill_tilemap_bg2:             ; Jaune, lignes 0-3
    LI R1, 3
    LI R2, 0x3000
    LIH R2, 0x0008
    LI R3, 256
    MSET
    RET

;=========================================================================
; FOREGROUND — désactivé
;=========================================================================

setup_foreground:
    RET

;=========================================================================
; HUD — bande orange, ligne 0 (y=0-15)
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
; SPRITES
;   Sprite 0 : Joueur bleu foncé @ (152, 100), tuile 1799
;   Sprite 1 : Cible violette    @ (200,  90), tuile 1028
;=========================================================================

setup_sprites:
    ; --- Sprite 0 : Joueur ---
    LI R0, 152
    STRI 0x00104500, R0
    LI R0, 100
    STRI 0x00104502, R0
    LI R0, 1799
    STRI 0x00104504, R0
    LI R0, 0x0002             ; palette=0, priority=2
    STRI 0x00104506, R0
    LI R0, 0x0004             ; enabled
    STRI 0x00104508, R0
    LI R0, 256
    STRI 0x0010450A, R0
    STRI 0x0010450C, R0

    ; --- Sprite 1 : Cible ---
    LI R0, 200
    STRI 0x00104510, R0
    LI R0, 90
    STRI 0x00104512, R0
    LI R0, 1028
    STRI 0x00104514, R0
    LI R0, 0x0002
    STRI 0x00104516, R0
    LI R0, 0x0004             ; enabled
    STRI 0x00104518, R0
    LI R0, 256
    STRI 0x0010451A, R0
    STRI 0x0010451C, R0

    RET

;=========================================================================
; APU
;   CH0 — 441 Hz grave @ RAM 0x00010000 (100 bytes, loop)
;   CH1 — 882 Hz aigu  @ RAM 0x00010100 ( 50 bytes, one-shot)
;=========================================================================

setup_apu:
    ; Sample grave 441 Hz
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

    ; Sample aigu 882 Hz
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

    ; Master APU
    LI R0, 0x0001
    STRI 0x00100800, R0
    LI R0, 200
    STRI 0x00100802, R0

    ; CH0 — 441 Hz grave (loop, démarré par check_collision)
    LI R0, 0x0001
    STRI 0x00100900, R0       ; ADDR_H
    LI R0, 0x0000
    STRI 0x00100902, R0       ; ADDR_L
    LI R0, 0
    STRI 0x00100904, R0       ; LENGTH_H
    LI R0, 100
    STRI 0x00100906, R0       ; LENGTH_L
    LI R0, 0
    STRI 0x00100908, R0       ; LOOP_START_H
    STRI 0x0010090A, R0       ; LOOP_START_L
    LI R0, 0xAC44
    STRI 0x0010090C, R0       ; PITCH = 44100
    LI R0, 0xC880
    STRI 0x0010090E, R0       ; VOL=200, PAN=128
    LI R0, 0
    STRI 0x00100910, R0       ; CTRL = stop

    ; CH1 — 882 Hz aigu (one-shot, déclenché par bouton)
    LI R0, 0x0001
    STRI 0x00100920, R0       ; ADDR_H
    LI R0, 0x0100
    STRI 0x00100922, R0       ; ADDR_L
    LI R0, 0
    STRI 0x00100924, R0       ; LENGTH_H
    LI R0, 50
    STRI 0x00100926, R0       ; LENGTH_L
    LI R0, 0
    STRI 0x00100928, R0       ; LOOP_START_H
    STRI 0x0010092A, R0       ; LOOP_START_L
    LI R0, 0xAC44
    STRI 0x0010092C, R0       ; PITCH = 44100
    LI R0, 0xC880
    STRI 0x0010092E, R0       ; VOL=200, PAN=128
    LI R0, 0
    STRI 0x00100930, R0       ; CTRL = stop

    RET

;=========================================================================
; BOUCLE PRINCIPALE
;=========================================================================

main_loop:
    VSYNC
    CALL update_sprite
    CALL check_buttons
    CALL check_collision
    JMP main_loop

;=========================================================================
; UPDATE_SPRITE — D-PAD controller 0
;   0x00100112 : DPAD  Bit0=UP Bit1=DOWN Bit2=LEFT Bit3=RIGHT
;=========================================================================

update_sprite:
    LDRI R1, 0x00100112
    LDRI R2, 0x00104500       ; sprite[0].x
    LDRI R3, 0x00104502       ; sprite[0].y

    LI R4, 0x0001             ; UP
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ dpad_down
    DEC R3

dpad_down:
    LI R4, 0x0002             ; DOWN
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ dpad_left
    INC R3

dpad_left:
    LI R4, 0x0004             ; LEFT
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ dpad_right
    DEC R2

dpad_right:
    LI R4, 0x0008             ; RIGHT
    MOV R5, R1
    AND R5, R4
    LI R6, 0
    CMP R5, R6
    JZ dpad_write
    INC R2

dpad_write:
    STRI 0x00104500, R2
    STRI 0x00104502, R3
    RET

;=========================================================================
; CHECK_BUTTONS — Son sur n'importe quel bouton (CH1 one-shot)
;
;   0x00100110 : BUTTONS
;     Bit0=A  Bit1=B  Bit2=X  Bit3=Y
;     Bit4=L  Bit5=R  Bit6=SELECT  Bit7=START
;
;   Masque 0x00FF → tous les 8 boutons
;   CTRL CH1 = 0x0005 : play=1 (bit0), 8bit=1 (bit2), loop=0 → one-shot
;=========================================================================

check_buttons:
    LDRI R1, 0x00100110       ; Lire BUTTONS controller 0

    LI R4, 0x00FF             ; Masque tous les 8 boutons (A/B/X/Y/L/R/Select/Start)
    AND R1, R4

    LI R6, 0
    CMP R1, R6
    JZ btn_none               ; Aucun bouton → rien

    LI R0, 0x0001             ; ← CORRECTION : 0x0005 → 0x0001
                              ;   play=1 (bit0), loop=0, 8bit=0 → one-shot 8-bit correct
    STRI 0x00100930, R0       ; CH1 CTRL = play one-shot

btn_none:
    RET

;=========================================================================
; CHECK_COLLISION — Son de collision sur CH0 (loop)
;
; RÈGLE IMPORTANTE :
;   On ne modifie JAMAIS COLLISION_STATUS depuis l'ASM.
;   Seul gpu_update_collisions() (dans gpu_render_frame) gère ce registre.
;   Il le remet à 0 chaque frame puis le met à 1 si collision détectée.
;   Ainsi COLLISION_STATUS est stable sur toutes les itérations de
;   main_loop dans la même frame hôte → pas de clignotement.
;
;   sound_flag (RAM 0x00000100) évite de relancer CH0 à chaque itération.
;=========================================================================

check_collision:
    LDRI R7, 0x00100262       ; R7 = COLLISION_STATUS

    LI R6, 0x0001
    AND R7, R6                ; isoler bit 0

    LI R6, 0
    CMP R7, R6
    JZ col_none               ; 0 → pas de collision

; --- Collision active ---
col_hit:
    LDRI R6, 0x00000100       ; R6 = sound_flag
    LI R5, 1
    CMP R6, R5
    JNZ col_start_sound       ; flag != 1 → démarrer CH0

    RET                       ; flag == 1 → CH0 déjà actif, rien à faire

col_start_sound:
    LI R0, 0x0003             ; play=1 (bit0), loop=1 (bit1)
    STRI 0x00100910, R0       ; CH0 CTRL = play + loop
    LI R0, 1
    STRI 0x00000100, R0       ; sound_flag = 1
    RET

; --- Pas de collision ---
col_none:
    LDRI R6, 0x00000100       ; R6 = sound_flag
    LI R5, 0
    CMP R6, R5
    JZ col_done               ; flag déjà 0 → son déjà arrêté

    LI R0, 0x0000             ; play=0 → stop
    STRI 0x00100910, R0       ; Arrêter CH0
    LI R0, 0
    STRI 0x00000100, R0       ; sound_flag = 0

col_done:
    RET

;=========================================================================
; FIN
;=========================================================================