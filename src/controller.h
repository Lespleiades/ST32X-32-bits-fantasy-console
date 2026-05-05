/* =========================================================
Copyright (C) 2026 - Peneaux Benjamin 
This program is free software; 
you may redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; 
either version 3 of the license.
- GNU GENERAL PUBLIC LICENSE V3 -
========================================================= */

#ifndef PB010381_CONTROLLER_H
#define PB010381_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

/* =========================================================
   ST32X CONTROLLER - USB Gamepad Support
   =========================================================
   
   Up to 4 SNES-style controllers:
   - 8 buttons: A, B, X, Y, L, R, SELECT, START
   - 8-way D-PAD
   
   MMIO mapping: 0x00100100 - 0x001001FF
========================================================= */

// Controller MMIO (defined in cpu.h but repeated here for clarity)
#define CONTROLLER_MMIO_START  0x00100100
#define CONTROLLER_MMIO_END    0x001001FF

#define MAX_CONTROLLERS  4

/* Button masks */
#define BTN_A       (1 << 0)
#define BTN_B       (1 << 1)
#define BTN_X       (1 << 2)
#define BTN_Y       (1 << 3)
#define BTN_L       (1 << 4)
#define BTN_R       (1 << 5)
#define BTN_SELECT  (1 << 6)
#define BTN_START   (1 << 7)

/* D-PAD directions */
#define DPAD_UP     (1 << 0)
#define DPAD_DOWN   (1 << 1)
#define DPAD_LEFT   (1 << 2)
#define DPAD_RIGHT  (1 << 3)

/* =========================================================
   CONTROLLER STRUCTURE
========================================================= */
typedef struct {
    // Button state (bitfield)
    uint16_t buttons;       // Currently pressed buttons
    uint16_t buttons_prev;  // Buttons from previous frame
    
    // D-PAD (bitfield)
    uint8_t dpad;
    
    // SDL
    SDL_GameController *sdl_controller;
    SDL_Joystick *sdl_joystick;
    
    // State
    bool connected;
    int device_index;
} Controller;

/* =========================================================
   CONTROLLER MANAGER STRUCTURE
========================================================= */
typedef struct {
    Controller controllers[MAX_CONTROLLERS];
    
    // Statistics
    int connected_count;
} PB010381_Controllers;

/* =========================================================
   PUBLIC FUNCTIONS
========================================================= */

/* Initialization */
void controllers_init(PB010381_Controllers *ctrl);
void controllers_shutdown(PB010381_Controllers *ctrl);

/* Update (called each frame) */
void controllers_update(PB010381_Controllers *ctrl);

/* Connection/disconnection */
void controller_connect(PB010381_Controllers *ctrl, int device_index);
void controller_disconnect(PB010381_Controllers *ctrl, int controller_index);

/* MMIO access */
uint16_t controllers_read16(PB010381_Controllers *ctrl, uint32_t addr);
void controllers_write16(PB010381_Controllers *ctrl, uint32_t addr, uint16_t value);

/* Utilities */
bool controller_button_pressed(Controller *ctrl, uint16_t button_mask);
bool controller_button_just_pressed(Controller *ctrl, uint16_t button_mask);
bool controller_button_just_released(Controller *ctrl, uint16_t button_mask);

/* Debug */
void controllers_debug_dump(PB010381_Controllers *ctrl);

#endif // PB010381_CONTROLLER_H
