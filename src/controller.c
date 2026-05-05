/* =========================================================
Copyright (C) 2026 - Peneaux Benjamin 
This program is free software; 
you may redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; 
either version 3 of the license.
- GNU GENERAL PUBLIC LICENSE V3 -
========================================================= */

#include "controller.h"
#include <stdio.h>
#include <string.h>

/* =========================================================
   INITIALIZATION
========================================================= */

void controllers_init(PB010381_Controllers *ctrl) {
    memset(ctrl, 0, sizeof(PB010381_Controllers));
    
    // Initialize SDL GameController subsystem
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
        printf("[CTRL][WARN] Cannot initialize SDL GameController: %s\n", 
               SDL_GetError());
        return;
    }
    
    printf("[CTRL] Subsystem initialized\n");
    
    // Detect already connected controllers
    int num_joysticks = SDL_NumJoysticks();
    printf("[CTRL] %d controller(s) detected\n", num_joysticks);
    
    for (int i = 0; i < num_joysticks && i < MAX_CONTROLLERS; i++) {
        if (SDL_IsGameController(i)) {
            controller_connect(ctrl, i);
        }
    }
}

void controllers_shutdown(PB010381_Controllers *ctrl) {
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (ctrl->controllers[i].connected) {
            controller_disconnect(ctrl, i);
        }
    }
    
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    printf("[CTRL] Clean shutdown\n");
}

/* =========================================================
   CONNECTION/DISCONNECTION
========================================================= */

void controller_connect(PB010381_Controllers *ctrl, int device_index) {
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (!ctrl->controllers[i].connected) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        printf("[CTRL] Cannot connect: all slots occupied\n");
        return;
    }
    
    // Open controller
    SDL_GameController *gc = SDL_GameControllerOpen(device_index);
    if (!gc) {
        printf("[CTRL][WARN] Cannot open controller %d: %s\n",
               device_index, SDL_GetError());
        return;
    }
    
    Controller *c = &ctrl->controllers[slot];
    c->sdl_controller = gc;
    c->sdl_joystick = SDL_GameControllerGetJoystick(gc);
    c->connected = true;
    c->device_index = device_index;
    c->buttons = 0;
    c->buttons_prev = 0;
    c->dpad = 0;
    
    ctrl->connected_count++;
    
    const char *name = SDL_GameControllerName(gc);
    printf("[CTRL] Controller %d connected (Slot %d): %s\n", 
           device_index, slot, name ? name : "Unknown");
}

void controller_disconnect(PB010381_Controllers *ctrl, int controller_index) {
    if (controller_index < 0 || controller_index >= MAX_CONTROLLERS) return;
    
    Controller *c = &ctrl->controllers[controller_index];
    if (!c->connected) return;
    
    if (c->sdl_controller) {
        SDL_GameControllerClose(c->sdl_controller);
    }
    
    c->connected = false;
    c->sdl_controller = NULL;
    c->sdl_joystick = NULL;
    
    ctrl->connected_count--;
    
    printf("[CTRL] Controller %d disconnected\n", controller_index);
}

/* =========================================================
   UPDATE
========================================================= */

void controllers_update(PB010381_Controllers *ctrl) {
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        Controller *c = &ctrl->controllers[i];
        if (!c->connected) continue;

        c->buttons_prev = c->buttons;
        c->buttons = 0;
        c->dpad = 0;

        // ── Reading via GameController API ────────────────────────
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_A))
            c->buttons |= BTN_A;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_B))
            c->buttons |= BTN_B;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_X))
            c->buttons |= BTN_X;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_Y))
            c->buttons |= BTN_Y;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))
            c->buttons |= BTN_L;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER))
            c->buttons |= BTN_R;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_BACK))
            c->buttons |= BTN_SELECT;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_START))
            c->buttons |= BTN_START;

        // ── Fallback: raw joystick reading (physical Xbox indices) ───
        // Covers cases where GameController API doesn't map all buttons
        // (some xinput / Bluetooth / Xbox Series drivers on SDL2)
        if (c->sdl_joystick) {
            int nb = SDL_JoystickNumButtons(c->sdl_joystick);
            if (nb > 0 && SDL_JoystickGetButton(c->sdl_joystick, 0)) c->buttons |= BTN_A;
            if (nb > 1 && SDL_JoystickGetButton(c->sdl_joystick, 1)) c->buttons |= BTN_B;
            if (nb > 2 && SDL_JoystickGetButton(c->sdl_joystick, 2)) c->buttons |= BTN_X;
            if (nb > 3 && SDL_JoystickGetButton(c->sdl_joystick, 3)) c->buttons |= BTN_Y;
            if (nb > 4 && SDL_JoystickGetButton(c->sdl_joystick, 4)) c->buttons |= BTN_L;
            if (nb > 5 && SDL_JoystickGetButton(c->sdl_joystick, 5)) c->buttons |= BTN_R;
            if (nb > 6 && SDL_JoystickGetButton(c->sdl_joystick, 6)) c->buttons |= BTN_SELECT;
            if (nb > 7 && SDL_JoystickGetButton(c->sdl_joystick, 7)) c->buttons |= BTN_START;
        }

        // ── D-PAD ────────────────────────────────────────────────
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_UP))
            c->dpad |= DPAD_UP;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
            c->dpad |= DPAD_DOWN;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
            c->dpad |= DPAD_LEFT;
        if (SDL_GameControllerGetButton(c->sdl_controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
            c->dpad |= DPAD_RIGHT;
    }
}

/* =========================================================
   MMIO ACCESS - READ
========================================================= */

uint16_t controllers_read16(PB010381_Controllers *ctrl, uint32_t addr) {
    uint32_t off = addr - CONTROLLER_MMIO_START;
    
    // Global status (0x00)
    if (off == 0x00) {
        uint16_t status = 0;
        for (int i = 0; i < MAX_CONTROLLERS; i++) {
            if (ctrl->controllers[i].connected) {
                status |= (1 << i);
            }
        }
        return status;
    }
    
    // Controller registers (8 bytes per controller)
    // Offset 0x10 = Controller 0
    // Offset 0x18 = Controller 1, etc.
    if (off >= 0x10 && off < 0x10 + (MAX_CONTROLLERS * 8)) {
        int ctrl_idx = (off - 0x10) / 8;
        int reg = (off - 0x10) % 8;
        
        if (ctrl_idx >= MAX_CONTROLLERS) return 0;
        
        Controller *c = &ctrl->controllers[ctrl_idx];
        if (!c->connected) return 0;
        
        switch (reg) {
            case 0:  // BUTTONS (0x10, 0x18, 0x20, 0x28)
                return c->buttons;
            case 2:  // DPAD + Reserved (0x12, 0x1A, 0x22, 0x2A)
                return c->dpad;
            case 4:  // BUTTONS_PREV (0x14, 0x1C, 0x24, 0x2C)
                return c->buttons_prev;
            default:
                return 0;
        }
    }
    
    return 0;
}

/* =========================================================
   MMIO ACCESS - WRITE
========================================================= */

void controllers_write16(PB010381_Controllers *ctrl, uint32_t addr, uint16_t value) {
    // Controllers are read-only
    // (No rumble or LED support in this version)
    (void)ctrl;
    (void)addr;
    (void)value;
}

/* =========================================================
   UTILITIES
========================================================= */

bool controller_button_pressed(Controller *ctrl, uint16_t button_mask) {
    return (ctrl->buttons & button_mask) != 0;
}

bool controller_button_just_pressed(Controller *ctrl, uint16_t button_mask) {
    return (ctrl->buttons & button_mask) && !(ctrl->buttons_prev & button_mask);
}

bool controller_button_just_released(Controller *ctrl, uint16_t button_mask) {
    return !(ctrl->buttons & button_mask) && (ctrl->buttons_prev & button_mask);
}

/* =========================================================
   DEBUG
========================================================= */

void controllers_debug_dump(PB010381_Controllers *ctrl) {
    printf("\n========== CONTROLLERS DEBUG ==========");
    printf("Connected: %d / %d\n", ctrl->connected_count, MAX_CONTROLLERS);
    
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        Controller *c = &ctrl->controllers[i];
        if (!c->connected) continue;
        
        const char *name = SDL_GameControllerName(c->sdl_controller);
        printf("\n--- Controller %d: %s ---\n", i, name ? name : "Unknown");
        printf("Buttons: ");
        if (c->buttons & BTN_A) printf("A ");
        if (c->buttons & BTN_B) printf("B ");
        if (c->buttons & BTN_X) printf("X ");
        if (c->buttons & BTN_Y) printf("Y ");
        if (c->buttons & BTN_L) printf("L ");
        if (c->buttons & BTN_R) printf("R ");
        if (c->buttons & BTN_SELECT) printf("SELECT ");
        if (c->buttons & BTN_START) printf("START ");
        printf("\n");
        
        printf("D-PAD: ");
        if (c->dpad & DPAD_UP) printf("UP ");
        if (c->dpad & DPAD_DOWN) printf("DOWN ");
        if (c->dpad & DPAD_LEFT) printf("LEFT ");
        if (c->dpad & DPAD_RIGHT) printf("RIGHT ");
        printf("\n");
    }
    
    printf("===============================\n\n");
}
