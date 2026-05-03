#ifndef PB010381_CONTROLLER_H
#define PB010381_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

/* =========================================================
   ST32X CONTROLLER - Support Manettes USB
   =========================================================
   
   Jusqu'à 4 manettes type SNES:
   - 8 boutons: A, B, X, Y, L, R, SELECT, START
   - D-PAD 8 directions
   
   Mapping MMIO: 0x00100100 - 0x001001FF
========================================================= */

// Controller MMIO (defined in cpu.h but repeated here for clarity)
#define CONTROLLER_MMIO_START  0x00100100
#define CONTROLLER_MMIO_END    0x001001FF

#define MAX_CONTROLLERS  4

/* Masques de boutons */
#define BTN_A       (1 << 0)
#define BTN_B       (1 << 1)
#define BTN_X       (1 << 2)
#define BTN_Y       (1 << 3)
#define BTN_L       (1 << 4)
#define BTN_R       (1 << 5)
#define BTN_SELECT  (1 << 6)
#define BTN_START   (1 << 7)

/* Directions D-PAD */
#define DPAD_UP     (1 << 0)
#define DPAD_DOWN   (1 << 1)
#define DPAD_LEFT   (1 << 2)
#define DPAD_RIGHT  (1 << 3)

/* =========================================================
   STRUCTURE CONTRÔLEUR
========================================================= */
typedef struct {
    // État des boutons (bitfield)
    uint16_t buttons;       // Boutons actuellement pressés
    uint16_t buttons_prev;  // Boutons du frame précédent
    
    // D-PAD (bitfield)
    uint8_t dpad;
    
    // SDL
    SDL_GameController *sdl_controller;
    SDL_Joystick *sdl_joystick;
    
    // État
    bool connected;
    int device_index;
} Controller;

/* =========================================================
   STRUCTURE GESTIONNAIRE DE CONTRÔLEURS
========================================================= */
typedef struct {
    Controller controllers[MAX_CONTROLLERS];
    
    // Statistiques
    int connected_count;
} PB010381_Controllers;

/* =========================================================
   FONCTIONS PUBLIQUES
========================================================= */

/* Initialisation */
void controllers_init(PB010381_Controllers *ctrl);
void controllers_shutdown(PB010381_Controllers *ctrl);

/* Mise à jour (appelé chaque frame) */
void controllers_update(PB010381_Controllers *ctrl);

/* Connexion/Déconnexion */
void controller_connect(PB010381_Controllers *ctrl, int device_index);
void controller_disconnect(PB010381_Controllers *ctrl, int controller_index);

/* Accès MMIO */
uint16_t controllers_read16(PB010381_Controllers *ctrl, uint32_t addr);
void controllers_write16(PB010381_Controllers *ctrl, uint32_t addr, uint16_t value);

/* Utilitaires */
bool controller_button_pressed(Controller *ctrl, uint16_t button_mask);
bool controller_button_just_pressed(Controller *ctrl, uint16_t button_mask);
bool controller_button_just_released(Controller *ctrl, uint16_t button_mask);

/* Debug */
void controllers_debug_dump(PB010381_Controllers *ctrl);

#endif // PB010381_CONTROLLER_H