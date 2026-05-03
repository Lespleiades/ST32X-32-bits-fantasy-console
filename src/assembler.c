#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

/* Fonction strdup portable */
char* my_strdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* d = malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}
#define strdup my_strdup

/* ================= TABLE DES OPCODES ================= */

typedef struct {
    char mnem[16];   // Mnémonique de l'instruction
    uint8_t op;      // Code opération
    int type;        // 0: Simple, 1: Imm16, 2: Imm32
} Opcode;


Opcode ISA[] = {
    // Contrôle
    {"HALT",   0x00, 0}, 
    {"NOP",    0xFF, 0}, 
    {"CLC",    0xFC, 0}, 
    {"SEC",    0xFB, 0},
    
    // Obsolète (bank switching supprimé)
    {"SETBNK", 0x45, 0},
    
    // Chargement/Sauvegarde 16-bit avec adresses 32 bits
    {"LDRI",   0x01, 2},  // LDRI Rd, imm32  - Load 16-bit from [imm32]
    {"STRI",   0x02, 2},  // STRI imm32, Rs  - Store 16-bit to [imm32]
    
    // Chargement/Sauvegarde 32-bit avec registres
    {"LDR",    0x03, 0},  // LDR Rd, Rs      - Load 32-bit from [Rs]
    {"STR",    0x04, 0},  // STR Rs, Rd      - Store 32-bit to [Rd]
    
    // NOUVEAU: Chargement/Sauvegarde 8-bit (pour pixels VRAM)
    {"LDRI8",  0x62, 2},  // LDRI8 Rd, imm32 - Load 8-bit from [imm32]
    {"STRI8",  0x63, 2},  // STRI8 imm32, Rs - Store 8-bit to [imm32]
    {"LDR8",   0x64, 0},  // LDR8 Rd, Rs     - Load 8-bit from [Rs]
    {"STR8",   0x65, 0},  // STR8 Rs, Rd     - Store 8-bit to [Rd]
    
    // Transfert & Immédiat
    {"MOV",    0x05, 0},  // MOV Rd, Rs
    {"LI",     0x06, 1},  // LI Rd, imm16
    {"LIL",    0x60, 1},  // LIL Rd, imm16   - Load Low word (bits 0-15)
    {"LIH",    0x61, 1},  // LIH Rd, imm16   - Load High word (bits 16-31)
    
    // Arithmétique
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
    
    // Logique
    {"AND",    0x1B, 0},
    {"OR",     0x1C, 0},
    {"XOR",    0x1D, 0},
    {"NOT",    0x1E, 0},
    {"SHL",    0x1F, 0},
    {"SHR",    0x21, 0},
    
    // Sauts (avec adresses 32 bits)
    {"JMP",    0x10, 2},  // JMP imm32
    {"JZ",     0x11, 2},  // JZ imm32
    {"JNZ",    0x12, 2},  // JNZ imm32
    {"CALL",   0x17, 2},  // CALL imm32
    {"RET",    0x18, 0},  // RET
    
    // Pile
    {"PUSH",   0x19, 0},
    {"POP",    0x1A, 0},
    
    // GPU
    {"VSYNC",  0x51, 0},
    
    // Mémoire
    {"MCPY",   0x40, 0},  // MCPY (R1=src, R2=dst, R3=len)
    {"MSET",   0x42, 0},  // MSET (R1=val, R2=addr, R3=len)
};

#define ISA_COUNT (sizeof(ISA) / sizeof(Opcode))

/* ================= GESTION DES LABELS ================= */

typedef struct {
    char name[64];
    uint32_t addr;
} Label;

Label labels[1024];
int label_count = 0;

/**
 * Résoudre une valeur (label ou immédiat)
 */
uint32_t resolve_val(char* s) {
    if (!s || strlen(s) == 0) return 0;
    
    // Essayer de trouver un label
    for (int i = 0; i < label_count; i++) {
        if (strcmp(s, labels[i].name) == 0) {
            printf("      [LABEL] '%s' -> 0x%08X\n", s, labels[i].addr);
            return labels[i].addr;
        }
    }
    
    // Sinon, parser comme nombre
    if (strstr(s, "0x") || strstr(s, "0X")) {
        return (uint32_t)strtol(s, NULL, 16);
    }
    return (uint32_t)atoi(s);
}

/**
 * Extraire le numéro d'un registre (ex: "R5" -> 5)
 */
int get_reg(char* s) {
    if (!s || s[0] != 'R') return 0;
    return atoi(s + 1);
}

/**
 * Supprimer les espaces en début et fin de chaîne
 */
char* trim(char* s) {
    while (isspace(*s)) s++;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace(*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* ================= ASSEMBLEUR ================= */

/**
 * Assembler un fichier source en binaire
 * @param infile Fichier source (.asm)
 * @param outfile Fichier de sortie (.bin)
 */
void assemble(const char* infile, const char* outfile) {
    FILE* in = fopen(infile, "r");
    if (!in) {
        perror("[ASM_ERROR] Impossible d'ouvrir le fichier source");
        return;
    }
    
    char line[512];
    uint32_t current_pc = 0x00200000;  // Adresse de départ par défaut (ROM)
    
    printf("========================================\n");
    printf("ASSEMBLEUR ST32X - MODE 32 BITS\n");
    printf("========================================\n\n");
    
    /* ===== PASSE 1 : COLLECTE DES LABELS ===== */
    printf("[PASSE 1] Collecte des labels...\n\n");
    
    while (fgets(line, sizeof(line), in)) {
        char* ptr = trim(line);
        
        // Ignorer les lignes vides et commentaires
        if (*ptr == '\0' || *ptr == ';') continue;
        
        // Sauvegarder la ligne pour parsing
        char line_copy[512];
        strcpy(line_copy, ptr);
        
        // Tokeniser
        char* token = strtok(line_copy, " \t\n\r,");
        if (!token) continue;
        
        // Détection de label (se termine par ':')
        if (token[strlen(token) - 1] == ':') {
            token[strlen(token) - 1] = '\0';  // Retirer le ':'
            strcpy(labels[label_count].name, token);
            labels[label_count].addr = current_pc;
            printf("   Label '%s' @ 0x%08X\n", token, current_pc);
            label_count++;
            continue;
        }
        
        // Directive .org
        if (strcmp(token, ".org") == 0) {
            token = strtok(NULL, " \t\n\r,");
            current_pc = resolve_val(token);
            printf("   .org 0x%08X\n", current_pc);
            continue;
        }
        
        // Rechercher l'instruction dans l'ISA
        bool found = false;
        for (int i = 0; i < ISA_COUNT; i++) {
            if (strcmp(token, ISA[i].mnem) == 0) {
                current_pc += 2;  // Header toujours 2 octets
                if (ISA[i].type == 1) current_pc += 2;  // Imm16
                if (ISA[i].type == 2) current_pc += 4;  // Imm32
                found = true;
                break;
            }
        }
        
        if (!found) {
            printf("   [WARN] Instruction inconnue: %s\n", token);
        }
    }
    
    printf("\n[PASSE 1] Terminée - %d labels collectés\n\n", label_count);
    
    /* ===== PASSE 2 : GÉNÉRATION DU CODE ===== */
    printf("[PASSE 2] Génération du binaire...\n\n");
    
    rewind(in);
    FILE* out = fopen(outfile, "wb");
    if (!out) {
        perror("[ASM_ERROR] Impossible de créer le fichier de sortie");
        fclose(in);
        return;
    }
    
    current_pc = 0x00200000;
    uint32_t bytes_written = 0;
    uint32_t line_number = 0;
    
    while (fgets(line, sizeof(line), in)) {
        line_number++;
        char* ptr = trim(line);
        
        // Ignorer les lignes vides et commentaires
        if (*ptr == '\0' || *ptr == ';') continue;
        
        // Tokeniser (max 8 tokens par ligne)
        char* tokens[16];
        int token_count = 0;
        
        char line_copy[512];
        strcpy(line_copy, ptr);
        
        char* t = strtok(line_copy, " \t\n\r,");
        while (t && token_count < 16) {
            if (t[0] == ';') break;  // Commentaire inline
            tokens[token_count++] = strdup(t);
            t = strtok(NULL, " \t\n\r,");
        }
        
        if (token_count == 0) continue;
        
        // Ignorer les labels (déjà traités)
        if (tokens[0][strlen(tokens[0]) - 1] == ':') {
            for (int i = 0; i < token_count; i++) free(tokens[i]);
            continue;
        }
        
        // Directive .org
        if (strcmp(tokens[0], ".org") == 0) {
            if (token_count < 2) {
                printf("   [ERROR] .org sans adresse (ligne %d)\n", line_number);
                continue;
            }
            
            uint32_t target_pc = resolve_val(tokens[1]);
            
            // Padding avec des 0x00 pour atteindre l'adresse cible
            while (current_pc < target_pc) {
                fputc(0x00, out);
                current_pc++;
                bytes_written++;
            }
            
            printf("   .org 0x%08X (padding: %d bytes)\n", 
                   target_pc, target_pc - (current_pc - (target_pc - current_pc)));
            
            for (int i = 0; i < token_count; i++) free(tokens[i]);
            continue;
        }
        
        // Rechercher l'opcode
        Opcode* opcode = NULL;
        for (int i = 0; i < ISA_COUNT; i++) {
            if (strcmp(tokens[0], ISA[i].mnem) == 0) {
                opcode = &ISA[i];
                break;
            }
        }
        
        if (!opcode) {
            printf("   [ERROR] Instruction inconnue '%s' (ligne %d)\n", 
                   tokens[0], line_number);
            for (int i = 0; i < token_count; i++) free(tokens[i]);
            continue;
        }
        
        // Extraction des registres
        int rd = 0, rs = 0;
        
        // Cas spécial : STRI imm32, Rs (Rs est le dernier argument)
        if (strcmp(opcode->mnem, "STRI") == 0 && token_count > 2) {
            rs = get_reg(tokens[2]);  // Le registre source
            rd = 0;
        }
        // Cas spécial : LDRI Rd, imm32 (Rd est le premier argument)
        else if (strcmp(opcode->mnem, "LDRI") == 0 && token_count > 1) {
            rd = get_reg(tokens[1]);  // Le registre destination
            rs = 0;
        }
        else if (token_count > 1 && tokens[1][0] == 'R') {
            // Instructions avec 1 seul registre
            if (token_count == 2) {
                // PUSH utilise Rs (source)
                if (strcmp(opcode->mnem, "PUSH") == 0) {
                    rs = get_reg(tokens[1]);
                    rd = 0;
                }
                // POP, INC, DEC, NOT utilisent Rd (destination)
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
        
        // Deuxième registre si présent (sauf pour STRI/LDRI déjà gérés)
        if (token_count > 2 && tokens[2][0] == 'R' && 
            strcmp(opcode->mnem, "STRI") != 0) {
            rs = get_reg(tokens[2]);
        }
        
        // Assemblage du Header : [Opcode:8 | Rd:4 | Rs:4]
        uint16_t header = (opcode->op << 8) | ((rd & 0x0F) << 4) | (rs & 0x0F);
        
        // Écriture du header
        fputc((header >> 8) & 0xFF, out);
        fputc(header & 0xFF, out);
        current_pc += 2;
        bytes_written += 2;
        
        printf("   0x%08X: %s ", current_pc - 2, opcode->mnem);
        
        // Écriture de l'immédiat si nécessaire
        if (opcode->type == 1) {  // Imm16
            uint16_t imm = (uint16_t)resolve_val(tokens[token_count - 1]);
            fputc((imm >> 8) & 0xFF, out);
            fputc(imm & 0xFF, out);
            current_pc += 2;
            bytes_written += 2;
            printf("(imm16=0x%04X)", imm);
        }
        else if (opcode->type == 2) {  // Imm32
            // Pour STRI, l'adresse est le token 1, pas le dernier
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
        
        // Libérer la mémoire des tokens
        for (int i = 0; i < token_count; i++) {
            free(tokens[i]);
        }
    }
    
    fclose(in);
    fclose(out);
    
    printf("\n========================================\n");
    printf("ASSEMBLAGE TERMINÉ\n");
    printf("========================================\n");
    printf("Fichier de sortie : %s\n", outfile);
    printf("Octets écrits     : %u (0x%X)\n", bytes_written, bytes_written);
    printf("Adresse finale    : 0x%08X\n\n", current_pc);
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