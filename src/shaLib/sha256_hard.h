/**
 * SHA256 Optimized Mining per ESP32-S3
 * Uses NerdMiner's pure-software optimized SHA256
 * ~20x faster than standard mbedtls
 */

#ifndef SHA256_HARD_H
#define SHA256_HARD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Pre-calcola midstate + bake per block header completo (80 bytes)
 * CHIAMARE quando cambia il job (non quando cambia solo il nonce!)
 * Questo velocizza enormemente sha256_double_hash_80
 * NOTA: Passa l'intero header da 80 bytes, la funzione userà primi 64 per midstate
 */
void calc_midstate(const uint8_t* block_header_80bytes);

/**
 * SHA-256 doppio OTTIMIZZATO per block header (80 bytes)
 * USA MIDSTATE PRE-CALCOLATO per performance massima!
 * Funzione CRITICA per mining - ~250 KH/s con midstate
 */
void sha256_double_hash_80(const uint8_t data[80], uint8_t hash[32]);

/**
 * SHA-256 doppio per 64 byte (merkle root calculation)
 */
void sha256_double_hash_64(const uint8_t data[64], uint8_t hash[32]);

/**
 * SHA-256 doppio generico
 */
void double_sha256(const uint8_t *payload, size_t payload_len, uint8_t *digest);

/**
 * SHA-256 singolo (legacy wrapper)
 */
void calc_sha_256(uint8_t *hash, const uint8_t *payload, size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif // SHA256_HARD_H
