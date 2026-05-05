/* =========================================================
Copyright (C) 2026 - Peneaux Benjamin 
This program is free software; 
you may redistribute and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; 
either version 3 of the license or (at your discretion) any later version.
========================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

/* Portable strdup function */
char* my_strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* d = malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}
#define strdup my_strdup

/* ================= OPCODE TABLE ================= */

typedef struct {
    char mnem[16];   // Instruction mnemonic
    uint8_t op;      // Operation code
    int type;        // 0: Simple, 1: Imm16, 2: Imm32
} Opcode;


Opcode ISA[] = {
    // Control
    {"HALT",   0x00, 0}, 
    {"NOP",    0xFF, 0}, 
    {"CLC",    0xFC, 0}, 
    {"SEC",    0xFB, 0},
	{"RTI",    0xF8, 0},   // Return from Interrupt
	{"CLI",    0xF9, 0},   // Clear Interrupt Enable (disable IRQ)
	{"SEI",    0xFA, 0},   // Set   Interrupt Enable (enable IRQ) 
	
    // Obsolete (bank switching removed)
    {"SETBNK", 0x45, 0},
    
    // 16-bit Load/Store with 32-bit addresses
    {"LDRI",   0x01, 2},  // LDRI Rd, imm32  - Load 16-bit from [imm32]
    {"STRI",   0x02, 2},  // STRI imm32, Rs  - Store 16-bit to [imm32]
    
    // 32-bit Load/Store with registers
    {"LDR",    0x03, 0},  // LDR Rd, Rs      - Load 32-bit from [Rs]
    {"STR",    0x04, 0},  // STR Rs, Rd      - Store 32-bit to [Rd]
    
    // NEW: 8-bit Load/Store (for VRAM pixels)
    {"LDRI8",  0x62, 2},  // LDRI8 Rd, imm32 - Load 8-bit from [imm32]
    {"STRI8",  0x63, 2},  // STRI8 imm32, Rs - Store 8-bit to [imm32]
    {"LDR8",   0x64, 0},  // LDR8 Rd, Rs     - Load 8-bit from [Rs]
    {"STR8",   0x65, 0},  // STR8 Rs, Rd     - Store 8-bit to [Rd]
    
    // Transfer & Immediate
    {"MOV",    0x05, 0},  // MOV Rd, Rs
    {"LI",     0x06, 1},  // LI Rd, imm16
    {"LIL",    0x60, 1},  // LIL Rd, imm16   - Load Low word (bits 0-15)
    {"LIH",    0x61, 1},  // LIH Rd, imm16   - Load High word (bits 16-31)
    
    // Arithmetic
    {"ADD",    0x07, 0},  // ADD Rd, Rs
    {"SUB",    0x08, 0},  // SUB Rd, Rs
    {"MUL",    0x09, 0},  // MUL Rd, Rs
    {"DIV",    0x0A, 0},  // DIV Rd, Rs
    {"MOD",    0x0B, 0},  // MOD Rd, Rs
    {"INC",    0x0C, 0},  // INC Rd
    {"DEC",    0x0D, 0},  // DEC Rd
    {"CMP",    0x0E, 0},  // CMP Rd, Rs
    {"ADDI",   0x0F, 1},  // ADDI Rd, imm16
    {"SUBI",   0x14, 1},  // SUBI Rd, imm16
    
    // Logic
    {"AND",    0x1B, 0},
    {"OR",     0x1C, 0},
    {"XOR",    0x1D, 0},
    {"NOT",    0x1E, 0},
    {"SHL",    0x1F, 0},
    {"SHR",    0x21, 0},
    
    // Jumps (with 32-bit addresses)
    {"JMP",    0x10, 2},  // JMP imm32
    {"JZ",     0x11, 2},  // JZ imm32
    {"JNZ",    0x12, 2},  // JNZ imm32
    {"CALL",   0x17, 2},  // CALL imm32
    {"RET",    0x18, 0},  // RET
    
    // Stack
    {"PUSH",   0x19, 0},
    {"POP",    0x1A, 0},
    
    // GPU
    {"VSYNC",  0x51, 0},
    
    // Memory
    {"MCPY",   0x40, 0},  // MCPY (R1=src, R2=dst, R3=len)
    {"MSET",   0x42, 0},  // MSET (R1=val, R2=addr, R3=len)
};

#define ISA_COUNT (sizeof(ISA) / sizeof(Opcode))

/* ================= LABEL MANAGEMENT ================= */

typedef struct {
    char name[64];
    uint32_t addr;
} Label;

Label labels[1024];
int label_count = 0;

/**
 * Resolve a value (label or immediate)
 */
uint32_t resolve_val(char* s) {
    if (!s || strlen(s) == 0) return 0;
    
    // Try to find a label
    for (int i = 0; i < label_count; i++) {
        if (strcmp(s, labels[i].name) == 0) {
            printf("      [LABEL] '%s' -> 0x%08X\n", s, labels[i].addr);
            return labels[i].addr;
        }
    }
    
    // Otherwise, parse as number
    if (strstr(s, "0x") || strstr(s, "0X")) {
        return (uint32_t)strtol(s, NULL, 16);
    }
    return (uint32_t)atoi(s);
}

/**
 * Extract register number from string (e.g., "R5" -> 5)
 */
int get_reg(char* s) {
    if (!s || s[0] != 'R') return 0;
    return atoi(s + 1);
}

/**
 * Remove leading and trailing spaces
 */
char* trim(char* s) {
    while (isspace(*s)) s++;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace(*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* ================= ASSEMBLER ================= */

/**
 * Assemble a source file into binary
 * @param infile Source file (.asm)
 * @param outfile Output file (.bin)
 */
void assemble(const char* infile, const char* outfile) {
    FILE* in = fopen(infile, "r");
    if (!in) {
        perror("[ASM_ERROR] Cannot open source file");
        return;
    }
    
    char line[512];
    uint32_t current_pc = 0x00200000;  // Default start address (ROM)
    
    printf("========================================\n");
    printf("ST16X ASSEMBLER - 32-BIT MODE\n");
    printf("========================================\n\n");
    
    /* ===== PASS 1: LABEL COLLECTION ===== */
    printf("[PASS 1] Collecting labels...\n\n");
    
    while (fgets(line, sizeof(line), in)) {
        char* ptr = trim(line);
        
        // Ignore empty lines and comments
        if (*ptr == '\0' || *ptr == ';') continue;
        
        // Save line for parsing
        char line_copy[512];
        strcpy(line_copy, ptr);
        
        // Tokenize
        char* token = strtok(line_copy, " \t\n\r,");
        if (!token) continue;
        
        // Detect label (ends with ':')
        if (token[strlen(token) - 1] == ':') {
            token[strlen(token) - 1] = '\0';  // Remove ':'
            strcpy(labels[label_count].name, token);
            labels[label_count].addr = current_pc;
            printf("   Label '%s' @ 0x%08X\n", token, current_pc);
            label_count++;
            continue;
        }
        
        // .org directive
        if (strcmp(token, ".org") == 0) {
            token = strtok(NULL, " \t\n\r,");
            current_pc = resolve_val(token);
            printf("   .org 0x%08X\n", current_pc);
            continue;
        }
        
        // Search for instruction in ISA
        bool found = false;
        for (int i = 0; i < ISA_COUNT; i++) {
            if (strcmp(token, ISA[i].mnem) == 0) {
                current_pc += 2;  // Header always 2 bytes
                if (ISA[i].type == 1) current_pc += 2;  // Imm16
                if (ISA[i].type == 2) current_pc += 4;  // Imm32
                found = true;
                break;
            }
        }
        
        if (!found) {
            printf("   [WARN] Unknown instruction: %s\n", token);
        }
    }
    
    printf("\n[PASS 1] Complete - %d labels collected\n\n", label_count);

    /* ===== SYMBOL FILE EMISSION ===== */
    // Derive .sym filename from outfile (replace extension or append .sym)
    char sym_file[512];
    strncpy(sym_file, outfile, sizeof(sym_file) - 5);
    sym_file[sizeof(sym_file) - 5] = '\0';
    {
        char* dot = strrchr(sym_file, '.');
        if (dot) *dot = '\0';
    }
    strcat(sym_file, ".sym");

    FILE* sym = fopen(sym_file, "w");
    if (sym) {
        fprintf(sym, "# ST32X Symbol Table\n");
        for (int i = 0; i < label_count; i++) {
            fprintf(sym, "%s 0x%08X\n", labels[i].name, labels[i].addr);
        }
        fclose(sym);
        printf("[SYMBOLS] Symbol table written to %s\n\n", sym_file);
    } else {
        printf("[SYMBOLS][WARN] Could not write symbol file %s\n\n", sym_file);
    }

    /* ===== PASS 2: BINARY GENERATION ===== */
    printf("[PASS 2] Generating binary...\n\n");
    
    rewind(in);
    FILE* out = fopen(outfile, "wb");
    if (!out) {
        perror("[ASM_ERROR] Cannot create output file");
        fclose(in);
        return;
    }
    
    current_pc = 0x00200000;
    uint32_t bytes_written = 0;
    uint32_t line_number = 0;
    
    while (fgets(line, sizeof(line), in)) {
        line_number++;
        char* ptr = trim(line);
        
        // Ignore empty lines and comments
        if (*ptr == '\0' || *ptr == ';') continue;
        
        // Tokenize (max 8 tokens per line)
        char* tokens[16];
        int token_count = 0;
        
        char line_copy[512];
        strcpy(line_copy, ptr);
        
        char* t = strtok(line_copy, " \t\n\r,");
        while (t && token_count < 16) {
            if (t[0] == ';') break;  // Inline comment
            tokens[token_count++] = strdup(t);
            t = strtok(NULL, " \t\n\r,");
        }
        
        if (token_count == 0) continue;
        
        // Ignore labels (already processed)
        if (tokens[0][strlen(tokens[0]) - 1] == ':') {
            for (int i = 0; i < token_count; i++) free(tokens[i]);
            continue;
        }
        
        // .org directive
        if (strcmp(tokens[0], ".org") == 0) {
            if (token_count < 2) {
                printf("   [ERROR] .org without address (line %d)\n", line_number);
                continue;
            }
            
            uint32_t target_pc = resolve_val(tokens[1]);
            
            // Padding with 0x00 to reach target address
            while (current_pc < target_pc) {
                fputc(0x00, out);
                current_pc++;
                bytes_written++;
            }
            
            printf("   .org 0x%08X (padding: %d bytes)\n", 
                   target_pc, target_pc - current_pc);
            
            for (int i = 0; i < token_count; i++) free(tokens[i]);
            continue;
        }
        
        // Search for opcode
        Opcode* opcode = NULL;
        for (int i = 0; i < ISA_COUNT; i++) {
            if (strcmp(tokens[0], ISA[i].mnem) == 0) {
                opcode = &ISA[i];
                break;
            }
        }
        
        if (!opcode) {
            printf("   [ERROR] Unknown instruction '%s' (line %d)\n", 
                   tokens[0], line_number);
            for (int i = 0; i < token_count; i++) free(tokens[i]);
            continue;
        }
        
        // Extract registers
        int rd = 0, rs = 0;
        
        // Special case: STRI imm32, Rs (Rs is the last argument)
        if (strcmp(opcode->mnem, "STRI") == 0 && token_count > 2) {
            rs = get_reg(tokens[2]);  // Source register
            rd = 0;
        }
        // Special case: LDRI Rd, imm32 (Rd is the first argument)
        else if (strcmp(opcode->mnem, "LDRI") == 0 && token_count > 1) {
            rd = get_reg(tokens[1]);  // Destination register
            rs = 0;
        }
        else if (token_count > 1 && tokens[1][0] == 'R') {
            // Instructions with 1 register
            if (token_count == 2) {
                // PUSH uses Rs (source)
                if (strcmp(opcode->mnem, "PUSH") == 0) {
                    rs = get_reg(tokens[1]);
                    rd = 0;
                }
                // POP, INC, DEC, NOT use Rd (destination)
                else if (strcmp(opcode->mnem, "POP") == 0 ||
                         strcmp(opcode->mnem, "INC") == 0 ||
                         strcmp(opcode->mnem, "DEC") == 0 ||
                         strcmp(opcode->mnem, "NOT") == 0) {
                    rd = get_reg(tokens[1]);
                    rs = 0;
                }
                else {
                    rd = get_reg(tokens[1]);
                }
            } else {
                rd = get_reg(tokens[1]);
            }
        }
        
        // Second register if present (except for STRI/LDRI already handled)
        if (token_count > 2 && tokens[2][0] == 'R' && 
            strcmp(opcode->mnem, "STRI") != 0) {
            rs = get_reg(tokens[2]);
        }
        
        // Assemble Header: [Opcode:8 | Rd:4 | Rs:4]
        uint16_t header = (opcode->op << 8) | ((rd & 0x0F) << 4) | (rs & 0x0F);
        
        // Write header
        fputc((header >> 8) & 0xFF, out);
        fputc(header & 0xFF, out);
        current_pc += 2;
        bytes_written += 2;
        
        printf("   0x%08X: %s ", current_pc - 2, opcode->mnem);
        
        // Write immediate if needed
        if (opcode->type == 1) {  // Imm16
            uint16_t imm = (uint16_t)resolve_val(tokens[token_count - 1]);
            fputc((imm >> 8) & 0xFF, out);
            fputc(imm & 0xFF, out);
            current_pc += 2;
            bytes_written += 2;
            printf("(imm16=0x%04X)", imm);
        }
        else if (opcode->type == 2) {  // Imm32
            // For STRI, address is token 1, not the last
            int addr_token_idx = (strcmp(opcode->mnem, "STRI") == 0) ? 1 : (token_count - 1);
            uint32_t imm = resolve_val(tokens[addr_token_idx]);
            fputc((imm >> 24) & 0xFF, out);
            fputc((imm >> 16) & 0xFF, out);
            fputc((imm >> 8) & 0xFF, out);
            fputc(imm & 0xFF, out);
            current_pc += 4;
            bytes_written += 4;
            printf("(imm32=0x%08X)", imm);
        }
        
        printf("\n");
        
        // Free token memory
        for (int i = 0; i < token_count; i++) {
            free(tokens[i]);
        }
    }
    
    fclose(in);
    fclose(out);
    
    printf("\n========================================\n");
    printf("ASSEMBLY COMPLETE\n");
    printf("========================================\n");
    printf("Output file: %s\n", outfile);
    printf("Bytes written: %u (0x%X)\n", bytes_written, bytes_written);
    printf("Final address: 0x%08X\n\n", current_pc);
}

/* ================= MAIN ================= */

int main(int argc, char* argv[]) {
    const char* input = "input.asm";
    const char* output = "output.bin";
    
    if (argc > 1) input = argv[1];
    if (argc > 2) output = argv[2];
    
    assemble(input, output);
    return 0;
}
