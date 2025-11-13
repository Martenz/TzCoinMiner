/**
 * Mining Utilities - Helper functions
 */

#ifndef MINING_UTILS_H
#define MINING_UTILS_H

#include <Arduino.h>

// Verifica target difficulty (come NerdMiner)
inline bool check_valid(uint8_t* hash, uint8_t* target) {
    // Confronta hash con target (little-endian)
    for (int i = 31; i >= 0; i--) {
        if (hash[i] > target[i]) return false;
        if (hash[i] < target[i]) return true;
    }
    return true;
}

// Converti difficulty in target (formato compatto Bitcoin)
inline void difficulty_to_target(uint32_t difficulty, uint8_t* target) {
    memset(target, 0xFF, 32);
    
    if (difficulty == 0) difficulty = 1;
    
    // Target = 0x00000000FFFF0000000000000000000000000000000000000000000000000000 / difficulty
    // Approssimazione semplificata per ESP32
    uint64_t base_target = 0xFFFF000000000000ULL;
    uint64_t new_target = base_target / difficulty;
    
    // Scrivi in little-endian negli ultimi 8 byte
    for (int i = 0; i < 8; i++) {
        target[31 - i] = (new_target >> (i * 8)) & 0xFF;
    }
}

// Log formattato per debug shares
inline void log_share_debug(const char* job_id, uint32_t nonce, uint32_t ntime, 
                            uint32_t extranonce2, const uint8_t* hash) {
    Serial.println("─────────────────────────────────────────");
    Serial.printf("📋 SHARE DEBUG:\n");
    Serial.printf("   Job ID: %s\n", job_id);
    Serial.printf("   Nonce: 0x%08x\n", nonce);
    Serial.printf("   NTime: 0x%08x\n", ntime);
    Serial.printf("   Extranonce2: 0x%08x\n", extranonce2);
    
    Serial.print("   Hash: ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", hash[i]);
    }
    Serial.println();
    Serial.println("─────────────────────────────────────────");
}

#endif // MINING_UTILS_H
