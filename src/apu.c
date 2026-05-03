#include "apu.h"
#include "cpu.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* =========================================================
   INITIALISATION
========================================================= */

void apu_init(PB010381_APU *apu) {
    memset(apu, 0, sizeof(PB010381_APU));
    
    apu->enabled = false;
    apu->master_volume = 255;
    
    // Initialiser tous les canaux
    for (int i = 0; i < APU_CHANNELS; i++) {
        apu->channels[i].volume = 255;
        apu->channels[i].pan = 128;  // Centre
        apu->channels[i].pitch = APU_SAMPLE_RATE;
    }
    
    printf("[APU] Initialisé\n");
    printf("      - Canaux: %d\n", APU_CHANNELS);
    printf("      - Sample Rate: %d Hz\n", APU_SAMPLE_RATE);
    printf("      - Buffer Size: %d samples\n", APU_BUFFER_SIZE);
    printf("      - Formats: PCM 8-bit / 16-bit\n");
    printf("      - Sortie: Stéréo 16-bit signé\n");
}

/* =========================================================
   GÉNÉRATION DE SAMPLES
========================================================= */

void apu_generate_samples(PB010381_APU *apu, int16_t *stream, int len) {
    if (!apu->enabled || !apu->system_ram) {
        // Silence
        memset(stream, 0, len * sizeof(int16_t) * 2);
        return;
    }
    
    // Effacer le buffer de mixage
    memset(stream, 0, len * sizeof(int16_t) * 2);
    
    // Mixer tous les canaux actifs
    for (int ch = 0; ch < APU_CHANNELS; ch++) {
        APU_Channel *chan = &apu->channels[ch];
        
        // Skip si le canal n'est pas en lecture
        if (!chan->play || !(chan->status & 0x01)) continue;
        if (chan->length == 0) continue;
        
        // Générer les samples pour ce canal
        for (int i = 0; i < len; i++) {
            // Position actuelle (partie entière)
            uint32_t pos = chan->position >> 16;
            
            // Vérifier si on a dépassé la fin
            if (pos >= chan->length) {
                if (chan->loop) {
                    // Revenir au début du loop
                    chan->position = chan->loop_start << 16;
                    pos = chan->loop_start;
                } else {
                    // Arrêter la lecture
                    chan->play = 0;
                    chan->status = 0;
                    break;
                }
            }
            
            // Lire le sample - CORRECTION REMARQUE 12: Utiliser offset
            int32_t sample = 0;
            uint32_t sample_addr = chan->addr + pos;
            
            // Vérifier que l'adresse est dans la RAM
            if (sample_addr >= RAM_START && sample_addr < RAM_END) {
                uint32_t offset = sample_addr - RAM_START;
                
                if (chan->bit16) {
                    // 16-bit signé (big-endian)
                    if (offset + 1 < RAM_SIZE) {
                        int16_t s16 = (apu->system_ram[offset] << 8) | 
                                      apu->system_ram[offset + 1];
                        sample = s16;
                    }
                } else {
                    // 8-bit non-signé -> converti en signé
                    if (offset < RAM_SIZE) {
                        uint8_t s8 = apu->system_ram[offset];
                        sample = (s8 - 128) << 8;  // Centrer et amplifier
                    }
                }
            }
            
            // Appliquer le volume du canal
            sample = (sample * chan->volume) / 255;
            
            // Appliquer le volume global
            sample = (sample * apu->master_volume) / 255;
            
            // Calculer les gains panoramique
            int32_t pan_left = (255 - chan->pan);
            int32_t pan_right = chan->pan;
            
            // Mixer dans le stream stéréo
            int32_t left = (sample * pan_left) / 255;
            int32_t right = (sample * pan_right) / 255;
            
            // Accumuler (avec clipping)
            int32_t current_left = stream[i * 2 + 0];
            int32_t current_right = stream[i * 2 + 1];
            
            current_left += left;
            current_right += right;
            
            // Clipper à [-32768, 32767]
            if (current_left > 32767) current_left = 32767;
            if (current_left < -32768) current_left = -32768;
            if (current_right > 32767) current_right = 32767;
            if (current_right < -32768) current_right = -32768;
            
            stream[i * 2 + 0] = (int16_t)current_left;
            stream[i * 2 + 1] = (int16_t)current_right;
            
            // Avancer la position (pitch-shifting)
            chan->position += chan->increment;
        }
    }
    
    apu->samples_generated += len;
}

/* =========================================================
   ACCÈS MMIO - LECTURE
========================================================= */

uint16_t apu_read16(PB010381_APU *apu, uint32_t addr) {
    uint32_t off = addr - APU_MMIO_START;
    
    // Registres globaux
    if (off == 0x00) return apu->master_ctrl;
    if (off == 0x02) return apu->master_volume;
    
    // Registres des canaux (à partir de 0x100)
    if (off >= 0x100 && off < 0x100 + (APU_CHANNELS * 32)) {
        int ch = (off - 0x100) / 32;
        int reg = (off - 0x100) % 32;
        
        APU_Channel *chan = &apu->channels[ch];
        
        switch (reg) {
            case 0x00: return (chan->addr >> 16) & 0xFFFF;
            case 0x02: return chan->addr & 0xFFFF;
            case 0x04: return (chan->length >> 16) & 0xFFFF;
            case 0x06: return chan->length & 0xFFFF;
            case 0x08: return (chan->loop_start >> 16) & 0xFFFF;
            case 0x0A: return chan->loop_start & 0xFFFF;
            case 0x0C: return chan->pitch;
            case 0x0E: return (chan->volume << 8) | chan->pan;
            case 0x10: return chan->ctrl;
            case 0x12: return chan->status;
            default: return 0;
        }
    }
    
    return 0;
}

/* =========================================================
   ACCÈS MMIO - ÉCRITURE
========================================================= */

void apu_write16(PB010381_APU *apu, uint32_t addr, uint16_t value) {
    uint32_t off = addr - APU_MMIO_START;
    
    // Registres globaux
    if (off == 0x00) {
        apu->master_ctrl = value & 0xFF;
        apu->enabled = (value & 0x01) != 0;
        
        printf("[APU] MASTER_CTRL = 0x%02X (Enabled: %d, Mute: %d)\n",
               apu->master_ctrl, apu->enabled, (value >> 1) & 1);
        return;
    }
    
    if (off == 0x02) {
        apu->master_volume = value & 0xFF;
        printf("[APU] MASTER_VOLUME = %d\n", apu->master_volume);
        return;
    }
    
    // Registres des canaux
    if (off >= 0x100 && off < 0x100 + (APU_CHANNELS * 32)) {
        int ch = (off - 0x100) / 32;
        int reg = (off - 0x100) % 32;
        
        APU_Channel *chan = &apu->channels[ch];
        
        switch (reg) {
            case 0x00:  // ADDR (high word)
                chan->addr = (chan->addr & 0x0000FFFF) | (value << 16);
                break;
                
            case 0x02:  // ADDR (low word)
                chan->addr = (chan->addr & 0xFFFF0000) | value;
                printf("[APU] CH%d ADDR = 0x%08X\n", ch, chan->addr);
                break;
                
            case 0x04:  // LENGTH (high word)
                chan->length = (chan->length & 0x0000FFFF) | (value << 16);
                break;
                
            case 0x06:  // LENGTH (low word)
                chan->length = (chan->length & 0xFFFF0000) | value;
                printf("[APU] CH%d LENGTH = %d bytes\n", ch, chan->length);
                break;
                
            case 0x08:  // LOOP_START (high word)
                chan->loop_start = (chan->loop_start & 0x0000FFFF) | (value << 16);
                break;
                
            case 0x0A:  // LOOP_START (low word)
                chan->loop_start = (chan->loop_start & 0xFFFF0000) | value;
                break;
                
            case 0x0C:  // PITCH
                chan->pitch = value;
                // Calculer l'incrément (fixed-point 16.16)
                if (chan->pitch > 0) {
                    chan->increment = ((uint64_t)chan->pitch << 16) / APU_SAMPLE_RATE;
                }
                printf("[APU] CH%d PITCH = %d Hz (inc=0x%08X)\n", 
                       ch, chan->pitch, chan->increment);
                break;
                
            case 0x0E:  // VOLUME + PAN
                chan->volume = (value >> 8) & 0xFF;
                chan->pan = value & 0xFF;
                printf("[APU] CH%d VOLUME=%d PAN=%d\n", ch, chan->volume, chan->pan);
                break;
                
            case 0x10:  // CTRL
                chan->ctrl = value & 0xFF;
                
                // Démarrer/arrêter la lecture
                if (chan->play && !(chan->status & 0x01)) {
                    // Démarrer
                    chan->position = 0;
                    chan->status = 0x01;
                    printf("[APU] CH%d PLAY (Loop:%d, 16bit:%d)\n", 
                           ch, chan->loop, chan->bit16);
                } else if (!chan->play) {
                    // Arrêter
                    chan->status = 0;
                    printf("[APU] CH%d STOP\n", ch);
                }
                break;
                
            default:
                break;
        }
    }
}

/* =========================================================
   CONTRÔLE DES CANAUX
========================================================= */

void apu_channel_play(PB010381_APU *apu, int channel) {
    if (channel < 0 || channel >= APU_CHANNELS) return;
    
    APU_Channel *chan = &apu->channels[channel];
    chan->play = 1;
    chan->position = 0;
    chan->status = 0x01;
}

void apu_channel_stop(PB010381_APU *apu, int channel) {
    if (channel < 0 || channel >= APU_CHANNELS) return;
    
    APU_Channel *chan = &apu->channels[channel];
    chan->play = 0;
    chan->status = 0;
}

void apu_channel_reset(PB010381_APU *apu, int channel) {
    if (channel < 0 || channel >= APU_CHANNELS) return;
    
    memset(&apu->channels[channel], 0, sizeof(APU_Channel));
    apu->channels[channel].volume = 255;
    apu->channels[channel].pan = 128;
    apu->channels[channel].pitch = APU_SAMPLE_RATE;
}

/* =========================================================
   DEBUG
========================================================= */

void apu_debug_dump(PB010381_APU *apu) {
    printf("\n========== APU DEBUG ==========\n");
    printf("Master Ctrl: 0x%02X  Volume: %d\n", 
           apu->master_ctrl, apu->master_volume);
    printf("Enabled: %d  Samples Generated: %u\n", 
           apu->enabled, apu->samples_generated);
    
    printf("\n--- CANAUX ACTIFS ---\n");
    for (int i = 0; i < APU_CHANNELS; i++) {
        APU_Channel *ch = &apu->channels[i];
        if (ch->status & 0x01) {
            printf("CH%d: Addr=0x%08X Len=%d Pos=%d Pitch=%dHz Vol=%d Pan=%d Loop=%d\n",
                   i, ch->addr, ch->length, ch->position >> 16, 
                   ch->pitch, ch->volume, ch->pan, ch->loop);
        }
    }
    printf("===============================\n\n");
}