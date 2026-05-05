#ifndef PB010381_APU_H
#define PB010381_APU_H

#include <stdint.h>
#include <stdbool.h>

/* =========================================================
   ST32X APU - Audio Processing Unit
   =========================================================
   
   16-Channel Wavetable Architecture:
   - PCM sample reading (8 or 16 bits)
   - Hardware pitch-shifting
   - Volume and stereo panning control
   - Automatic looping
   
   Each channel: 32 bytes control
   Output: Stereo signed 16-bit @ 44100 Hz
========================================================= */

// APU MMIO (defined in cpu.h but repeated here for clarity)
#define APU_MMIO_START   0x00100800
#define APU_MMIO_END     0x00100FFF

#define APU_CHANNELS     16
#define APU_SAMPLE_RATE  44100
#define APU_BUFFER_SIZE  2048

/* =========================================================
   CHANNEL STRUCTURE (32 bytes)
========================================================= */
typedef struct {
    uint32_t addr;         // 0x00: Sample address in RAM (32-bit)
    uint32_t length;       // 0x04: Total length in bytes
    uint32_t loop_start;   // 0x08: Loop start point
    uint16_t pitch;        // 0x0C: Frequency (Hz) - e.g., 44100
    uint8_t  volume;       // 0x0E: Volume (0-255)
    uint8_t  pan;          // 0x0F: Pan (0=L, 128=C, 255=R)
    
    // Control flags (0x10)
    union {
        uint8_t ctrl;
        struct {
            uint8_t play     : 1;  // Bit 0: Play/Stop
            uint8_t loop     : 1;  // Bit 1: Loop enable
            uint8_t bit16    : 1;  // Bit 2: 0=8bit, 1=16bit
            uint8_t reserved1 : 5;  // Bits 3-7: Reserved
        };
    };
    
    uint8_t  status;       // 0x11: Bit 0: Is_Playing
    uint8_t  reserved2[14]; // 0x12-0x1F: Reserved
    
    // Internal state (not MMIO mapped)
    uint32_t position;     // Current playback position (fixed-point 16.16)
    uint32_t increment;    // Position increment per sample
} APU_Channel;

/* =========================================================
   MAIN APU STRUCTURE
========================================================= */
typedef struct {
    // Global registers
    uint8_t  master_ctrl;   // 0x00: Bit 0=ON/OFF, Bit 1=Mute
    uint8_t  master_volume; // 0x01: Global volume (0-255)
    uint16_t reserved_glob; // 0x02-0x03: Reserved
    
    // 16 Channels (32 bytes each)
    APU_Channel channels[APU_CHANNELS];
    
    // Pointer to system RAM
    uint8_t *system_ram;
    
    // Audio buffer (interleaved stereo)
    int16_t buffer[APU_BUFFER_SIZE * 2];
    
    // State
    bool enabled;
    uint32_t samples_generated;
} PB010381_APU;

/* =========================================================
   PUBLIC FUNCTIONS
========================================================= */

/* Initialization */
void apu_init(PB010381_APU *apu);

/* Audio generation (called by SDL) */
void apu_generate_samples(PB010381_APU *apu, int16_t *stream, int len);

/* MMIO access */
uint16_t apu_read16(PB010381_APU *apu, uint32_t addr);
void apu_write16(PB010381_APU *apu, uint32_t addr, uint16_t value);

/* Channel control */
void apu_channel_play(PB010381_APU *apu, int channel);
void apu_channel_stop(PB010381_APU *apu, int channel);
void apu_channel_reset(PB010381_APU *apu, int channel);

/* Debug */
void apu_debug_dump(PB010381_APU *apu);

#endif // PB010381_APU_H
