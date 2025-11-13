#ifndef MINING_TASK_H
#define MINING_TASK_H

#include <Arduino.h>

// Mining statistics structure
struct MiningStats {
    uint32_t hashes_per_second;
    uint32_t total_hashes;
    uint32_t best_difficulty;
    char best_hash[65]; // SHA256 hash as hex string
    uint32_t shares_submitted;  // Total shares submitted to pool
    uint32_t shares_accepted;   // Shares accepted by pool
    uint32_t shares_rejected;   // Shares rejected by pool
    uint32_t blocks_found;  // Number of blocks found
    uint32_t block_height;  // Current block height being mined
};

// Mining modes
enum MiningMode {
    MINING_MODE_EDUCATIONAL,  // Difficoltà semplificata per demo
    MINING_MODE_SOLO,         // Mining solo con nodo Bitcoin
    MINING_MODE_POOL          // Mining pool con Stratum
};

// Mining task management
void mining_task_start(void);
void mining_task_stop(void);
bool mining_task_is_running(void);
MiningStats mining_get_stats(void);
bool mining_has_found_block(void);  // Check if a block has been found

// Configurazione nodo Bitcoin per mining solo
void mining_set_bitcoin_node(const char* host, uint16_t port, const char* user, const char* pass);

// Configurazione pool per mining pool
void mining_set_pool(const char* pool_url, uint16_t port, const char* wallet_address, 
                     const char* worker_name = nullptr, const char* password = nullptr);

// Imposta modalità di mining
void mining_set_mode(MiningMode mode);
MiningMode mining_get_mode(void);

// Check if in educational fallback mode (when Solo/Pool connection failed)
bool mining_is_educational_fallback(void);

#endif // MINING_TASK_H
