/**
 * SHA256 Hardware Acceleration per ESP32-S3
 * Uses NerdMiner's optimized pure-software SHA256
 * 
 * This provides ~20x performance over standard mbedtls
 */

#include "sha256_hard.h"
#include "nerdSHA256plus.h"
#include <Arduino.h>
#include <string.h>
#include "mbedtls/sha256.h"

// Midstate context (pre-calculated for block header first 64 bytes)
static nerdSHA256_context midstate_ctx;
static uint32_t midstate_digest[8];
static uint32_t bake[13];  // Pre-calculated intermediate values for ultra-fast mining
static bool midstate_valid = false;

/**
 * Pre-calcola midstate per i primi 64 bytes del block header
 * Chiamare quando cambia il job (non il nonce!)
 * NOTA: block_header_80bytes deve essere il header completo da 80 bytes
 */
void calc_midstate(const uint8_t* block_header_80bytes) {
    // Calculate midstate from first 64 bytes
    nerd_mids(midstate_digest, block_header_80bytes);
    memcpy(midstate_ctx.digest, midstate_digest, 32);
    
    // 🚀 CRITICAL: Pre-calculate "bake" values from bytes 64-79
    // This pre-computes SHA intermediate values that only change when job changes
    // NOT when only the nonce changes! This is the key to 250 KH/s performance!
    nerd_sha256_bake(midstate_digest, block_header_80bytes + 64, bake);
    
    midstate_valid = true;
    
    Serial.println("🚀 Midstate + Bake pre-calculated! Ultra-fast mining enabled!");
}

/**
 * SHA-256 doppio ottimizzato per mining (usa midstate + bake)
 * block_header DEVE essere 80 bytes
 */
void sha256_double_hash_80(const uint8_t* block_header, uint8_t* hash) {
    static bool first_call = true;
    
    // Se midstate non valido, calcola al volo (fallback lento)
    if(!midstate_valid) {
        if(first_call) {
            Serial.println("⚠️  WARNING: midstate NOT valid, using slow fallback!");
            first_call = false;
        }
        nerd_mids(midstate_digest, block_header);
        nerd_sha256_bake(midstate_digest, block_header + 64, bake);
    } else {
        if(first_call) {
            Serial.println("✅ Using optimized nerd_sha256d_baked path!");
            first_call = false;
        }
    }
    
    // 🚀 PERFORMANCE CRITICA: Usa nerd_sha256d_baked per massima velocità!
    // Questa funzione riusa midstate + bake pre-calcolati, cambia solo nonce!
    nerd_sha256d_baked(midstate_digest, block_header + 64, bake, hash);
}

/**
 * SHA-256 doppio per 64 bytes (merkle root calculation)
 */
void sha256_double_hash_64(const uint8_t data[64], uint8_t hash[32]) {
    nerdSHA256_context ctx;
    uint32_t digest[8];
    
    // Calculate midstate for first 64 bytes
    nerd_mids(digest, data);
    memcpy(ctx.digest, digest, 32);
    
    // Final hash with padding
    uint8_t padding[16] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02, 0x00};  // 512 bits = 0x200
    nerd_sha256d(&ctx, padding, hash);
}

/**
 * Generic double SHA-256
 */
void double_sha256(const uint8_t* payload, size_t payload_len, uint8_t* digest) {
    if(payload_len == 80) {
        sha256_double_hash_80(payload, digest);
    } else if(payload_len == 64) {
        sha256_double_hash_64(payload, digest);
    } else {
        // Fallback: use mbedtls for non-standard sizes
        uint8_t temp[32];
        mbedtls_sha256(payload, payload_len, temp, 0);
        mbedtls_sha256(temp, 32, digest, 0);
    }
}

// Legacy wrapper
void calc_sha_256(uint8_t *hash, const uint8_t *payload, size_t payload_len) {
    mbedtls_sha256(payload, payload_len, hash, 0);
}
