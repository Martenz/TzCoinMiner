#include "mining_task.h"
#include "bitcoin_rpc.h"
#include "stratum_client.h"
#include "wifi_config.h"
#include "shaLib/sha256_hard.h"      // SHA-256 wrapper functions
#include "shaLib/nerdSHA256plus.h"  // NerdMiner optimized SHA-256
#include "mining_utils.h"            // NerdMiner utility functions
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>  // For explicit watchdog reset

// Forward declarations
int difficulty_to_zeros(uint32_t difficulty);
void save_pending_share(uint32_t nonce, double difficulty, int zeros, const uint8_t* hash, 
                       const char* job_id, const char* ntime, uint32_t extranonce2);
void check_pending_shares(const char* current_job_id, uint32_t current_difficulty);
void cleanup_stale_shares(const char* new_job_id);

// Helper: Reverse bytes in place (for endianness conversion)
static void reverse_bytes(uint8_t* data, size_t len) {
    for (size_t i = 0; i < len / 2; ++i) {
        uint8_t temp = data[i];
        data[i] = data[len - 1 - i];
        data[len - 1 - i] = temp;
    }
}

// Calculate target from nbits (Bitcoin compact format)
// Returns true if target was calculated successfully
static bool calculate_target_from_nbits(const char* nbits_hex, uint8_t* target_out) {
    if (!nbits_hex || strlen(nbits_hex) < 6) return false;
    
    // Parse nbits (compact format): AABBCCDD where AA is exponent, BBCCDD is coefficient
    uint32_t nbits;
    sscanf(nbits_hex, "%8x", &nbits);
    
    // Extract exponent and coefficient
    uint8_t exponent = (nbits >> 24) & 0xFF;
    uint32_t coefficient = nbits & 0x00FFFFFF;
    
    // Build target: coefficient shifted by (exponent - 3) bytes
    memset(target_out, 0, 32);
    
    if (exponent >= 3) {
        int shift_bytes = exponent - 3;
        if (shift_bytes < 29) {  // Sanity check
            target_out[shift_bytes + 2] = (coefficient >> 16) & 0xFF;
            target_out[shift_bytes + 1] = (coefficient >> 8) & 0xFF;
            target_out[shift_bytes + 0] = coefficient & 0xFF;
        }
    }
    
    return true;
}

// Check if hash meets target (NerdMiner method)
// Hash from nerd_sha256d_baked is in BIG-ENDIAN (PUT_UINT32_BE)
// Target from nbits is in BIG-ENDIAN
// Compare directly byte-by-byte from MSB to LSB
static bool check_hash_meets_target(const uint8_t* hash, const uint8_t* target) {
    // Compare from byte 0 (MSB) to byte 31 (LSB) for big-endian
    for (int i = 0; i < 32; i++) {
        if (hash[i] > target[i]) {
            return false;  // Hash too large
        } else if (hash[i] < target[i]) {
            return true;   // Hash smaller than target
        }
        // If equal, continue to next byte
    }
    
    return true;  // Exactly equal = valid
}

// Job caching structure for performance (NerdMiner optimization)
struct JobCache {
    uint32_t extranonce2;
    String job_id;
    uint8_t merkle_root[32];
    uint32_t midstate[8];
    uint32_t bake[16];
    uint8_t header_template[128];  // CRITICAL: Store header for consistent baking/hashing
    uint8_t target[32];            // Target calculated from nbits for share validation
    bool valid;
};
static JobCache g_job_cache = {0};

// Pending shares queue - stores valuable hashes that don't meet current difficulty
// but might be valid if pool lowers difficulty or for future jobs
struct PendingShare {
    uint32_t nonce;
    double difficulty;
    int zeros;
    uint8_t hash[32];
    String job_id;           // Critical: only submit for matching job
    String ntime;
    uint32_t extranonce2;
    uint32_t timestamp;      // When found (to expire old shares)
};

#define MAX_PENDING_SHARES 20
static PendingShare pending_shares[MAX_PENDING_SHARES];
static int pending_count = 0;

// FreeRTOS task handles
static TaskHandle_t miningTaskHandle = NULL;
static TaskHandle_t miningTaskHandle2 = NULL;  // Second worker for dual-core mining
static TaskHandle_t stratumTaskHandle = NULL;

// Task running flags
static volatile bool taskRunning = false;
static volatile bool stratumTaskRunning = false;

// Educational fallback flag (when Solo/Pool connection fails)
static volatile bool isEducationalFallback = false;

// Mining statistics - separate for each worker
static MiningStats stats_worker0 = {0};
static MiningStats stats_worker1 = {0};

// Mining mode
static MiningMode currentMiningMode = MINING_MODE_EDUCATIONAL;

// Pool configuration
static String pool_url;
static uint16_t pool_port = 3333;
static String pool_wallet;
static String pool_worker;
static String pool_password;

// Job corrente dal pool
static stratum_job_t current_pool_job;
static bool has_pool_job = false;
static bool need_rebuild_header = false;  // Flag per ricostruire header solo su nuovo job
static uint32_t pool_difficulty = 0;
static uint32_t extranonce2 = 1;  // Fixed extranonce2 value (NerdMiner uses 1)
static uint32_t last_heartbeat_share_time = 0;  // Timestamp dell'ultima heartbeat share

// Bitcoin block header structure (80 bytes)
struct BlockHeader {
    uint32_t version;           // 4 bytes - Versione del blocco
    uint8_t prevBlockHash[32];  // 32 bytes - Hash del blocco precedente
    uint8_t merkleRoot[32];     // 32 bytes - Merkle root delle transazioni
    uint32_t timestamp;         // 4 bytes - Unix timestamp
    uint32_t bits;              // 4 bytes - Target di difficoltà (formato compatto)
    uint32_t nonce;             // 4 bytes - Numero da variare per trovare soluzione
} __attribute__((packed));

// Converte hash binario in stringa esadecimale
void hash_to_hex(const uint8_t* hash, char* hex_string) {
    for(int i = 0; i < 32; i++) {
        sprintf(hex_string + (i * 2), "%02x", hash[i]);
    }
    hex_string[64] = '\0';
}

// Helper: converte hex string in binary
void hex_to_bin(const char* hex, uint8_t* bin, size_t bin_len)
{
    for(size_t i = 0; i < bin_len; i++) {
        sscanf(hex + (i * 2), "%2hhx", &bin[i]);
    }
}

// Helper: converte binary in hex string
void bin_to_hex(const uint8_t* bin, size_t bin_len, char* hex)
{
    for(size_t i = 0; i < bin_len; i++) {
        sprintf(hex + (i * 2), "%02x", bin[i]);
    }
    hex[bin_len * 2] = '\0';
}

// Callback quando arriva nuovo job dal pool
void on_stratum_job(stratum_job_t* job) {
    // Copy job fields individually (can't use memcpy with std::vector!)
    current_pool_job.job_id = job->job_id;
    current_pool_job.prev_hash = job->prev_hash;
    current_pool_job.coinb1 = job->coinb1;
    current_pool_job.coinb2 = job->coinb2;
    current_pool_job.merkle_branch = job->merkle_branch;  // This is a std::vector, proper assignment
    current_pool_job.version = job->version;
    current_pool_job.nbits = job->nbits;
    current_pool_job.ntime = job->ntime;
    current_pool_job.clean_jobs = job->clean_jobs;
    current_pool_job.extranonce1 = job->extranonce1;
    current_pool_job.extranonce2_size = job->extranonce2_size;
    
    // Update flags to trigger mining
    has_pool_job = true;
    need_rebuild_header = true;
    
    // Mark pool as connected (we received a job)
    stats_worker0.pool_connected = true;
    
    // Invalidate cache to force recalculation
    g_job_cache.valid = false;
    
    // 🔥 CRITICAL: Sync pool_difficulty from stratum client!
    uint32_t old_difficulty = pool_difficulty;
    pool_difficulty = stratum_get_difficulty();
    
    Serial.printf("\n📬 Nuovo job dal pool!\n");
    Serial.printf("   Job ID: %s\n", job->job_id.c_str());
    Serial.printf("   Clean: %s\n", job->clean_jobs ? "YES" : "NO");
    
    // Show both raw pool difficulty and effective difficulty being used
    extern WifiConfig config;
    uint32_t effective = (pool_difficulty > 0) ? pool_difficulty : 
                        ((config.minDifficulty > 0) ? config.minDifficulty : 1);
    
    Serial.printf("   Pool Difficulty: %u", pool_difficulty);
    if (pool_difficulty == 0) {
        Serial.printf(" (not set by pool, using default: %u)", effective);
    }
    Serial.printf("\n   Mining with difficulty: %u (requires %d zeros)\n", 
                 effective, difficulty_to_zeros(effective));
    
    // Clean up stale shares from previous jobs
    cleanup_stale_shares(job->job_id.c_str());
    
    // Check if difficulty dropped - submit any pending shares that now qualify
    if (pool_difficulty > 0 && pool_difficulty < old_difficulty) {
        Serial.printf("📉 Difficulty dropped from %u to %u - checking pending shares\n", 
                     old_difficulty, pool_difficulty);
        check_pending_shares(job->job_id.c_str(), pool_difficulty);
    }
}

// Callback quando arriva risposta dal pool per una share
void on_share_response(bool accepted) {
    // Update stats in worker0 (shares are submitted by stratum task, not worker-specific)
    if (accepted) {
        stats_worker0.shares_accepted++;
        // Serial.printf("   📊 Stats: %u accepted / %u submitted / %u rejected\n", 
        //              stats_worker0.shares_accepted, stats_worker0.shares_submitted, stats_worker0.shares_rejected);
    } else {
        stats_worker0.shares_rejected++;
        // Serial.printf("   📊 Stats: %u accepted / %u submitted / %u rejected\n", 
        //              stats_worker0.shares_accepted, stats_worker0.shares_submitted, stats_worker0.shares_rejected);
    }
}

// Nota: double_sha256 ora viene da sha256_hard.cpp (hardware accelerated)

// Conta gli zeri iniziali in un hash (in formato esadecimale)
int count_leading_zeros(const uint8_t* hash) {
    int leading_zeros = 0;
    for(int i = 0; i < 32; i++) {
        if(hash[i] == 0) {
            leading_zeros += 2;
        } else if(hash[i] < 0x10) {
            leading_zeros += 1;
            break;
        } else {
            break;
        }
    }
    return leading_zeros;
}

// Calcola la difficoltà effettiva da usare (max tra pool e minima configurata)
uint32_t get_effective_difficulty() {
    extern WifiConfig config;
    
    // Se pool_difficulty è 0 (pool non ha ancora inviato difficulty)
    // usa minDifficulty configurato come fallback (o default 32)
    if (pool_difficulty == 0) {
        uint32_t fallback = (config.minDifficulty > 0) ? config.minDifficulty : 32;
        return fallback;
    }
    
    // Se il pool ha inviato una difficulty, USALA SEMPRE (il pool sa cosa fa)
    // NON applicare floor hardware, lascia che sia il pool a decidere
    return pool_difficulty;
}

// Save a valuable hash for later submission if difficulty drops
void save_pending_share(uint32_t nonce, double difficulty, int zeros, const uint8_t* hash, 
                       const char* job_id, const char* ntime, uint32_t extranonce2) {
    // Only save shares with 5+ zeros (for heartbeat)
    if (zeros < 5) return;
    
    // Remove expired shares (older than 10 minutes = 600 seconds)
    uint32_t now = millis() / 1000;
    for (int i = 0; i < pending_count; ) {
        if (now - pending_shares[i].timestamp > 600) {
            // Expired - remove by shifting array
            for (int j = i; j < pending_count - 1; j++) {
                pending_shares[j] = pending_shares[j + 1];
            }
            pending_count--;
        } else {
            i++;
        }
    }
    
    // If queue is full, remove oldest (FIFO)
    if (pending_count >= MAX_PENDING_SHARES) {
        for (int i = 0; i < MAX_PENDING_SHARES - 1; i++) {
            pending_shares[i] = pending_shares[i + 1];
        }
        pending_count = MAX_PENDING_SHARES - 1;
    }
    
    // Add new pending share
    PendingShare* share = &pending_shares[pending_count];
    share->nonce = nonce;
    share->difficulty = difficulty;
    share->zeros = zeros;
    memcpy(share->hash, hash, 32);
    share->job_id = String(job_id);
    share->ntime = String(ntime);
    share->extranonce2 = extranonce2;
    share->timestamp = now;
    pending_count++;
    
    Serial.printf("💾 Saved pending share: %d zeros, diff %.0f, job %s (queue: %d/%d)\n", 
                 zeros, difficulty, job_id, pending_count, MAX_PENDING_SHARES);
}

// Check if any pending shares can now be submitted (difficulty dropped or matches job)
void check_pending_shares(const char* current_job_id, uint32_t current_difficulty) {
    if (pending_count == 0) return;
    
    uint32_t effective_diff = get_effective_difficulty();
    Serial.printf("🔍 Checking %d pending shares (current diff: %u, job: %s)\n", 
                 pending_count, effective_diff, current_job_id);
    
    for (int i = 0; i < pending_count; ) {
        PendingShare* share = &pending_shares[i];
        
        // Check if difficulty now allows this share AND job matches
        if (share->difficulty >= (double)effective_diff && 
            share->job_id.equals(current_job_id)) {
            
            Serial.printf("✅ Submitting pending share: %d zeros, diff %.0f\n", 
                         share->zeros, share->difficulty);
            
            // Format nonce as hex (BIG-ENDIAN like NerdMiner)
            char nonce_hex[9];
            snprintf(nonce_hex, sizeof(nonce_hex), "%08x", share->nonce);
            
            // Format extranonce2 as hex (big-endian)
            char extranonce2_hex[17];
            int hex_len = 4 * 2;  // Assuming 4 bytes extranonce2
            for(int k = 3; k >= 0; k--) {
                snprintf(extranonce2_hex + ((3 - k) * 2), 3, "%02x", 
                        (share->extranonce2 >> (k * 8)) & 0xFF);
            }
            extranonce2_hex[hex_len] = '\0';
            
            // Submit the share (job_id, extranonce2, ntime, nonce)
            stratum_submit_share(share->job_id.c_str(), extranonce2_hex, 
                               share->ntime.c_str(), nonce_hex);
            
            // Remove from queue (shift array)
            for (int j = i; j < pending_count - 1; j++) {
                pending_shares[j] = pending_shares[j + 1];
            }
            pending_count--;
            // Don't increment i - we shifted array
            
        } else {
            i++; // Move to next share
        }
    }
}

// Clean up pending shares when job becomes stale
void cleanup_stale_shares(const char* new_job_id) {
    if (pending_count == 0) return;
    
    int removed = 0;
    for (int i = 0; i < pending_count; ) {
        if (!pending_shares[i].job_id.equals(new_job_id)) {
            // Job changed - this share is now stale
            removed++;
            // Remove by shifting array
            for (int j = i; j < pending_count - 1; j++) {
                pending_shares[j] = pending_shares[j + 1];
            }
            pending_count--;
        } else {
            i++;
        }
    }
    
    if (removed > 0) {
        Serial.printf("🗑️  Removed %d stale shares (job changed to %s)\n", removed, new_job_id);
    }
}

// Converte difficulty in numero di zeri esadecimali richiesti nell'hash
// Usa la formula matematica corretta: zeros = 8 + log₁₆(difficulty)
int difficulty_to_zeros(uint32_t difficulty) {
    if(difficulty == 0) return 0;  // Invalid/not set
    if(difficulty == 1) return 8;  // Bitcoin baseline difficulty
    
    // Correct formula: zeros = 8 + log₁₆(difficulty)
    // where log₁₆(x) = log(x) / log(16)
    // 
    // This matches the mathematical relationship:
    // - Each hex zero = target divided by 16
    // - difficulty × 16 = target ÷ 16 = +1 zero
    //
    // Examples verified with Claude:
    // - diff 1    → 8 zeros  (0x00000000FFFF0000...)
    // - diff 16   → 9 zeros  (8 + log₁₆(16) = 8 + 1)
    // - diff 64   → 10 zeros (8 + log₁₆(64) = 8 + 1.5 ≈ 10)
    // - diff 256  → 10 zeros (8 + log₁₆(256) = 8 + 2)
    // - diff 1000 → 11 zeros (8 + log₁₆(1000) = 8 + 2.5 ≈ 11)
    // - diff 4096 → 12 zeros (8 + log₁₆(4096) = 8 + 3)
    
    double log16_diff = log((double)difficulty) / log(16.0);
    int zeros = 8 + (int)round(log16_diff);
    
    // Sanity check: clamp to reasonable range
    if(zeros < 8) zeros = 8;
    if(zeros > 20) zeros = 20;  // Max practical limit
    
    return zeros;
}

// Costruisce la coinbase transaction da componenti Stratum
void build_coinbase(const stratum_job_t* job, uint32_t extranonce2_value, uint8_t* coinbase_hash) {
    // Lunghezza massima coinbase: coinb1 + extranonce1 + extranonce2 + coinb2
    uint8_t coinbase[1024];
    size_t coinbase_len = 0;
    
    // 1. Aggiungi coinb1
    size_t coinb1_len = job->coinb1.length() / 2;
    for(size_t i = 0; i < coinb1_len; i++) {
        sscanf(job->coinb1.c_str() + (i * 2), "%2hhx", &coinbase[coinbase_len++]);
    }
    
    // 2. Aggiungi extranonce1
    size_t extranonce1_len = job->extranonce1.length() / 2;
    for(size_t i = 0; i < extranonce1_len; i++) {
        sscanf(job->extranonce1.c_str() + (i * 2), "%2hhx", &coinbase[coinbase_len++]);
    }
    
    // 3. Aggiungi extranonce2 (in little endian)
    for(int i = 0; i < job->extranonce2_size; i++) {
        coinbase[coinbase_len++] = (extranonce2_value >> (i * 8)) & 0xFF;
    }
    
    // 4. Aggiungi coinb2
    size_t coinb2_len = job->coinb2.length() / 2;
    for(size_t i = 0; i < coinb2_len; i++) {
        sscanf(job->coinb2.c_str() + (i * 2), "%2hhx", &coinbase[coinbase_len++]);
    }
    
    // ⚡ Calcola doppio SHA-256 della coinbase con HW acceleration
    // Coinbase è quasi sempre >64 bytes, usa versione generica
    double_sha256(coinbase, coinbase_len, coinbase_hash);
}

// Calcola il merkle root da coinbase hash e merkle branch
void calculate_merkle_root(uint8_t* coinbase_hash, const std::vector<String>& merkle_branch, uint8_t* merkle_root) {
    // Inizia con l'hash della coinbase
    memcpy(merkle_root, coinbase_hash, 32);
    
    // Per ogni elemento nel merkle branch, combina e hash
    for(size_t i = 0; i < merkle_branch.size(); i++) {
        uint8_t branch_hash[32];
        uint8_t combined[64];
        
        // Converti branch element da hex a binary (come NerdMinerV2)
        // NON invertire - gli elementi vengono usati così come arrivano
        for(int j = 0; j < 32; j++) {
            sscanf(merkle_branch[i].c_str() + (j * 2), "%2hhx", &branch_hash[j]);
        }
        
        // Combina: merkle_root + branch_hash
        memcpy(combined, merkle_root, 32);
        memcpy(combined + 32, branch_hash, 32);
        
        // ⚡ Doppio SHA-256 del risultato con HW acceleration (ottimizzato per 64 byte)
        sha256_double_hash_64(combined, merkle_root);
    }
}

// Verifica se l'hash è sotto il target (difficoltà)
bool check_hash_difficulty(const uint8_t* hash, uint32_t difficulty_bits) {
    // In Bitcoin, l'hash deve essere MINORE del target
    // Per semplicità educativa, contiamo gli zeri iniziali
    int leading_zeros = count_leading_zeros(hash);
    
    // Calcoliamo la difficoltà richiesta dai bits
    // In Bitcoin reale, questo usa la formula del target compatto
    // Qui usiamo una versione semplificata: numero di zeri richiesti
    uint32_t required_zeros = (difficulty_bits >> 24) - 3;
    
    return leading_zeros >= required_zeros;
}

// Stratum network task - gestisce comunicazione pool in background
void stratumTask(void* parameter)
{
    Serial.println("🌐 Stratum network task started on Core 0");
    
    stratumTaskRunning = true;
    
    while (stratumTaskRunning) {
        if (currentMiningMode == MINING_MODE_POOL && stratum_is_connected()) {
            stratum_loop();
        }
        
        // Breve delay per non saturare CPU (10ms)
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    Serial.println("Stratum network task stopped");
    stratumTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Mining task function - runs in background
void miningTask(void* parameter)
{
    // Get worker ID from parameter (0 or 1)
    int worker_id = (int)parameter;
    
    // Select the correct stats struct for this worker
    MiningStats* stats = (worker_id == 0) ? &stats_worker0 : &stats_worker1;
    
    Serial.printf("╔════════════════════════════════════════════════════════╗\n");
    Serial.printf("║    BITCOIN MINING WORKER %d STARTED (Core %d)         ║\n", worker_id, xPortGetCoreID());
    Serial.printf("╚════════════════════════════════════════════════════════╝\n");
    Serial.println();
    
    // Skip SHA-256 test until midstate is calculated (after pool job received)
    // Test will run automatically during first mining loop
    
    taskRunning = true;
    
    // Inizializza in base alla modalità
    if(currentMiningMode == MINING_MODE_POOL) {
        Serial.println("🏊 MODALITÀ POOL MINING");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.printf("   Pool: %s:%d\n", pool_url.c_str(), pool_port);
        Serial.printf("   Wallet: %s\n", pool_wallet.c_str());
        Serial.printf("   Worker: %s\n", pool_worker.c_str());
        Serial.println();
        
        // Inizializza client Stratum
        stratum_init(pool_url.c_str(), pool_port, pool_wallet.c_str(), 
                    pool_worker.c_str(), pool_password.c_str());
        stratum_set_job_callback(on_stratum_job);
        stratum_set_share_response_callback(on_share_response);
        
        // Connetti al pool
        if(!stratum_connect()) {
            Serial.println("❌ Impossibile connettersi al pool!");
            Serial.println("   Tornando a modalità educativa...");
            currentMiningMode = MINING_MODE_EDUCATIONAL;
            isEducationalFallback = true;  // Mark as fallback mode
        } else {
            Serial.println("✅ Connesso al pool!");
            Serial.println();
            isEducationalFallback = false;  // Successfully connected
        }
    } else if(currentMiningMode == MINING_MODE_SOLO) {
        Serial.println("🌐 MODALITÀ SOLO MINING - Recupero blocco reale...");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    } else {
        Serial.println("📚 MINING EDUCATIVO - Come funziona il Bitcoin Mining");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.println();
        Serial.println("⚠️  NOTA IMPORTANTE:");
        Serial.println("    ESP32 può fare ~5-10 KH/s");
        Serial.println("    Mining rig ASIC moderno fa ~100 TH/s");
        Serial.println("    Differenza: 10.000.000.000x più lento!");
        Serial.println();
        Serial.println("🎯 OBIETTIVO:");
        Serial.println("    Imparare come funziona il Proof of Work");
        Serial.println("    - Costruire Block Header (80 bytes)");
        Serial.println("    - Calcolare SHA-256 doppio");
        Serial.println("    - Variare il nonce per trovare hash valido");
        Serial.println("    - Verificare che hash < target difficoltà");
        Serial.println();
    }
    
    // Inizializza il block header
    BlockHeader header;
    bool usingRealBlock = false;
    
    if(currentMiningMode == MINING_MODE_SOLO) {
        // Modalità SOLO: recupera vero block template dalla blockchain
        BitcoinBlockTemplate blockTemplate;
        if(bitcoin_rpc_get_block_template(&blockTemplate)) {
            // Usa dati reali dal nodo
            header.version = blockTemplate.version;
            header.timestamp = blockTemplate.curtime;
            header.bits = blockTemplate.bits;
            header.nonce = 0;
            
            // Converte previous block hash da hex a binary
            hex_to_bin(blockTemplate.previousblockhash, header.prevBlockHash, 32);
            
            // Converte merkle root da hex a binary
            // NOTA: In un vero miner dovresti calcolare il merkle tree
            // da tutte le transazioni nel template
            hex_to_bin(blockTemplate.merkleroot, header.merkleRoot, 32);
            
            // Salva block height nelle statistiche
            stats->block_height = blockTemplate.height;
            
            usingRealBlock = true;
            
            Serial.println("✅ Blocco reale caricato!");
            Serial.printf("   Altezza: %u\n", blockTemplate.height);
            Serial.printf("   Transazioni: %d\n", blockTemplate.transactions_count);
            isEducationalFallback = false;  // Successfully got block template
        } else {
            Serial.println("❌ Impossibile ottenere block template!");
            Serial.println("   Tornando a modalità educativa...");
            currentMiningMode = MINING_MODE_EDUCATIONAL;
            isEducationalFallback = true;  // Mark as fallback mode
        }
    }
    
    if(currentMiningMode == MINING_MODE_EDUCATIONAL) {
        // Modalità EDUCATIVA: usa blocco di esempio
        if (!isEducationalFallback) {
            // Intentional educational mode, not fallback
            isEducationalFallback = false;
        }
        Serial.println("🎓 Modalità EDUCATIVA - Blocco di esempio");
        
        header.version = 0x20000000; // Version 2
        
        // Hash del blocco precedente (esempio)
        Serial.println("📦 Inizializzando Block Header...");
        memset(header.prevBlockHash, 0, 32);
        header.prevBlockHash[0] = 0x00;
        header.prevBlockHash[1] = 0x00;
        
        // Merkle root (esempio)
        memset(header.merkleRoot, 0, 32);
        for(int i = 0; i < 32; i++) {
            header.merkleRoot[i] = random(0, 256);
        }
        
        header.timestamp = millis() / 1000; // Unix timestamp simulato
        header.bits = 0x1d00ffff; // Difficoltà ridotta per demo
        header.nonce = 0;
        
        // Block height educativo (simulato)
        stats->block_height = 0;
        
        Serial.printf("   Version: 0x%08x\n", header.version);
        Serial.printf("   Difficulty bits: 0x%08x\n", header.bits);
        Serial.printf("   Timestamp: %u\n", header.timestamp);
        
        // 🚀 CRITICAL: Pre-calculate midstate for educational mode too!
        calc_midstate((uint8_t*)&header);
        
        Serial.println();
        Serial.println("⛏️  Iniziando mining loop...");
        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        Serial.println();
    }
    
    uint8_t hash[32];
    char hash_hex[65];
    uint32_t hashes = 0;
    uint32_t start_time = millis();
    uint32_t blocks_found = 0;
    int best_zeros = 0;  // Best del periodo corrente (si resetta ogni 1000 batches)
    
    // Best assoluto (globale, non si resetta mai)
    int absolute_best_zeros = 0;
    char absolute_best_hash[65] = {0};
    
    // Block header per pool mining (persistente tra iterazioni)
    BlockHeader pool_header;
    bool header_initialized = false;
    
    // 🔥 DUAL-WORKER: Each worker starts with different nonce
    // Worker 0: starts at 0 (even nonces: 0, 2, 4, 6, ...)
    // Worker 1: starts at 1 (odd nonces: 1, 3, 5, 7, ...)
    uint32_t nonce = worker_id;
    
    Serial.printf("⚡ Worker %d: Starting with nonce %u (will increment by 2)\n", worker_id, nonce);
    
    // Main mining loop
    while (taskRunning) {
        // MODALITÀ POOL: verifica connessione (stratum_loop ora gira in task separato!)
        if(currentMiningMode == MINING_MODE_POOL) {
            // Se non connesso, riprova
            if(!stratum_is_connected()) {
                Serial.println("⚠️  Connessione pool persa, riconnessione...");
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                if(!stratum_connect()) {
                    Serial.println("❌ Riconnessione fallita");
                    vTaskDelay(10000 / portTICK_PERIOD_MS);
                    continue;
                }
            }
            
            // Aspetta di avere un job dal pool
            if(!has_pool_job) {
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }
            
            // Costruisci block header SOLO se è nuovo job o primo avvio
            if(need_rebuild_header || !header_initialized) {
                need_rebuild_header = false;
                header_initialized = true;
                
                // Version (converti da hex string a uint32)
                pool_header.version = strtoul(current_pool_job.version.c_str(), NULL, 16);
                
                // Previous block hash (4-byte word swap come NerdMinerV2)
                // Il pool invia in formato RPC, dobbiamo convertire per Bitcoin
                // Prima leggi tutti i byte direttamente
                uint8_t prev_hash_temp[32];
                for(int i = 0; i < 32; i++) {
                    sscanf(current_pool_job.prev_hash.c_str() + (i * 2), "%2hhx", &prev_hash_temp[i]);
                }
                
                // Poi fai 4-byte word swap (8 parole da 4 byte)
                for(int i = 0; i < 8; i++) {
                    for(int j = 0; j < 4; j++) {
                        pool_header.prevBlockHash[i * 4 + j] = prev_hash_temp[i * 4 + (3 - j)];
                    }
                }
                
                // nBits (difficulty)
                pool_header.bits = strtoul(current_pool_job.nbits.c_str(), NULL, 16);
                
                // Timestamp
                pool_header.timestamp = strtoul(current_pool_job.ntime.c_str(), NULL, 16);
                
                // Nonce - reset to worker's unique starting point (0 or 1)
                // Do NOT reset to 0 for all workers - this causes duplicates!
                nonce = worker_id;
                pool_header.nonce = nonce;
                
                // Aggiorna block height (non fornito da Stratum, usa 0)
                stats->block_height = 0;
                
                // Debug: mostra block header completo solo al primo job
                static bool first_job = true;
                if(first_job) {
                    first_job = false;
                    Serial.printf("🔨 Mining job configurato (diff %u richiede %d zeros)\n", 
                                 get_effective_difficulty(), difficulty_to_zeros(get_effective_difficulty()));
                    char header_hex[161];
                    bin_to_hex((uint8_t*)&pool_header, 80, header_hex);
                    Serial.printf("   Header sample (80 bytes): %s\n", header_hex);
                }
            }  // Fine if(need_rebuild_header)
            
            // ═══════════════════════════════════════════════════════════════
            // 🚀 NERDMINER OPTIMIZATION 1: Smart Job Cache
            // Only recalculate when job/extranonce2 actually changes
            // ═══════════════════════════════════════════════════════════════
            
            bool need_recalc = false;
            
            if (!g_job_cache.valid ||
                extranonce2 != g_job_cache.extranonce2 ||
                current_pool_job.job_id != g_job_cache.job_id) {
                need_recalc = true;
            }
            
            if(need_recalc) {
                Serial.printf("🔄 Job update: ex2=%u, job=%s\n", 
                             extranonce2, current_pool_job.job_id.c_str());
                
                // Build coinbase and merkle root
                uint8_t coinbase_hash[32];
                build_coinbase(&current_pool_job, extranonce2, coinbase_hash);
                calculate_merkle_root(coinbase_hash, current_pool_job.merkle_branch, pool_header.merkleRoot);
                
                // ═══════════════════════════════════════════════════════════════
                // 🚀 NERDMINER OPTIMIZATION 2: Pre-calculate Midstate + Baking
                // This is the KEY to 3x speed improvement!
                // ═══════════════════════════════════════════════════════════════
                
                // CRITICAL FIX: Build header buffer in correct byte order (NerdMiner compatible)
                // NerdMiner reverses: version (full), prevhash (word swap), ntime (full), nbits (full)
                uint8_t* buf = g_job_cache.header_template;
                
                // Version (4 bytes) - FULL byte reverse (like NerdMiner)
                uint32_t version_val = strtoul(current_pool_job.version.c_str(), NULL, 16);
                buf[0] = version_val & 0xFF;
                buf[1] = (version_val >> 8) & 0xFF;
                buf[2] = (version_val >> 16) & 0xFF;
                buf[3] = (version_val >> 24) & 0xFF;
                
                // Previous block hash (32 bytes) - already correctly byte-swapped in pool_header
                memcpy(buf + 4, pool_header.prevBlockHash, 32);
                
                // Merkle root (32 bytes) - raw bytes, NO swapping
                memcpy(buf + 36, pool_header.merkleRoot, 32);
                
                // Timestamp (4 bytes) - Parse hex string byte-by-byte (Stratum sends as hex string)
                // "ff8c1669" -> bytes [0xff, 0x8c, 0x16, 0x69]
                for (int i = 0; i < 4; i++) {
                    char byte_str[3] = {current_pool_job.ntime[i*2], current_pool_job.ntime[i*2+1], 0};
                    buf[68 + i] = (uint8_t)strtol(byte_str, NULL, 16);
                }
                
                // Bits (4 bytes) - Parse hex string byte-by-byte (Stratum sends as hex string)
                // "36d90117" -> bytes [0x36, 0xd9, 0x01, 0x17]
                for (int i = 0; i < 4; i++) {
                    char byte_str[3] = {current_pool_job.nbits[i*2], current_pool_job.nbits[i*2+1], 0};
                    buf[72 + i] = (uint8_t)strtol(byte_str, NULL, 16);
                }
                
                // Nonce (4 bytes, will be updated in mining loop - initialize to 0)
                buf[76] = 0;
                buf[77] = 0;
                buf[78] = 0;
                buf[79] = 0;
                
                // SHA-256 padding (bytes 80-127)
                memset(buf + 80, 0, 48);
                buf[80] = 0x80;      // SHA-256 padding
                buf[126] = 0x02;     // Length (0x0280 = 640 bits)
                buf[127] = 0x80;
                
                // Calculate midstate (first 64 bytes) - ONCE PER JOB!
                nerd_mids(g_job_cache.midstate, g_job_cache.header_template);
                
                // Bake the second half (pre-compute constants) - ONCE PER JOB!
                nerd_sha256_bake(g_job_cache.midstate, g_job_cache.header_template + 64, g_job_cache.bake);
                
                // Calculate target from nbits for share validation (NerdMiner method)
                if (!calculate_target_from_nbits(current_pool_job.nbits.c_str(), g_job_cache.target)) {
                    Serial.printf("⚠️  Failed to calculate target from nbits: %s\n", current_pool_job.nbits.c_str());
                    memset(g_job_cache.target, 0xFF, 32);  // Set to max (accept nothing) on error
                }
                
                // Cache everything for reuse
                g_job_cache.extranonce2 = extranonce2;
                g_job_cache.job_id = current_pool_job.job_id;
                memcpy(g_job_cache.merkle_root, pool_header.merkleRoot, 32);
                g_job_cache.valid = true;
            }
            
            // ⚡⚡⚡ PERFORMANCE CRITICAL: Minimal overhead mining loop (NerdMiner style)
            // Process small batches (4K nonces like NerdMiner) for fast job switching
            const uint32_t NONCE_PER_BATCH = 4096;  // 4K nonces per batch (NerdMiner standard)
            uint32_t batch_end = nonce + NONCE_PER_BATCH;
            uint32_t batch_start_time = millis();
            uint32_t batch_hashes = 0;
            
            // Pre-calculate effective difficulty once
            uint32_t effective_diff = get_effective_difficulty();
            
            // Copy cached header template for this batch
            uint8_t header_bytes[128];
            memcpy(header_bytes, g_job_cache.header_template, 128);
            
            // Save current job ID to detect job changes
            String current_job_id = current_pool_job.job_id;
            
            // Dual-worker: step by 2 (worker 0 tests even, worker 1 tests odd)
            const uint32_t nonce_step = 2;
            
            // ⚡⚡⚡ ULTRA-FAST MINING LOOP - Minimal overhead! ⚡⚡⚡
            for(; nonce < batch_end && taskRunning; nonce += nonce_step) {
                // Update nonce as plain uint32_t (buffer already has correct byte order)
                ((uint32_t*)(header_bytes + 76))[0] = nonce;
                
                // Compute hash with early rejection filter
                if (nerd_sha256d_baked(g_job_cache.midstate, header_bytes + 64, 
                                      g_job_cache.bake, hash))
                {
                    // Hash passed filter - calculate difficulty
                    // NerdMiner passes hash DIRECTLY - le256todouble handles byte order internally
                    double hash_difficulty = diff_from_target(hash);
                    
                    // Calculate leading zeros from hash (big-endian, zeros at END)
                    int zeros = 0;
                    for(int i = 31; i >= 0; i--) {
                        if(hash[i] == 0) {
                            zeros += 2;  // 2 hex digits per byte
                        } else if((hash[i] & 0xF0) == 0) {
                            zeros += 1;  // High nibble is zero (e.g., 0x01, 0x0F)
                            break;
                        } else {
                            break;
                        }
                    }
                    
                    // Update best if improved
                    // NOTE: Filter at 0x32E7 means we only see hashes with diff >= ~1
                    // So best_difficulty_zeros will only update for high-quality hashes
                    if(hash_difficulty > stats->best_difficulty) {
                        stats->best_difficulty = hash_difficulty;
                        stats->best_difficulty_zeros = zeros;
                        
                        char hash_hex[65];
                        for(int i = 0; i < 32; i++) {
                            sprintf(hash_hex + (i * 2), "%02x", hash[i]);
                        }
                        hash_hex[64] = '\0';
                        memcpy(stats->best_hash, hash_hex, 65);
                        
                        // Debug: log new best
                        Serial.printf("🏆 Worker %d: New best! %dz (diff %.2f)\n", 
                                     worker_id, zeros, hash_difficulty);
                    }
                    
                    // Check if valid share
                    if(hash_difficulty >= (double)effective_diff) {
                        // Count leading zeros in hash (zeros at END = leading zeros in LE)
                        int share_zeros = 0;
                        for(int i = 31; i >= 0; i--) {
                            if(hash[i] == 0) {
                                share_zeros += 2;  // Entire byte is 00 = 2 hex zeros
                            } else if((hash[i] & 0xF0) == 0) {
                                share_zeros += 1;  // High nibble is 0 (e.g., 0x01, 0x0F)
                                break;
                            } else {
                                break;
                            }
                        }
                        
                        // Log ALL valuable hashes (5+ zeros) for visibility
                        if(share_zeros >= 5) {
                            // Format hash in little-endian (zeros at start) for display
                            // Hash is stored big-endian in memory, so print from byte 31 to 0
                            char hash_le[65];
                            for(int i = 31; i >= 0; i--) {
                                sprintf(hash_le + ((31-i) * 2), "%02x", hash[i]);
                            }
                            hash_le[64] = '\0';
                            Serial.printf("💎 Found %dz hash (diff %.0f): %s\n", 
                                         share_zeros, hash_difficulty, hash_le);
                        }
                        
                        // Submit strategy: Check if hash meets target (NerdMiner method)
                        // This is the CORRECT way - compare hash directly with target from nbits
                        // NOT by comparing difficulty numbers which have conversion issues
                        bool should_submit = check_hash_meets_target(hash, g_job_cache.target);
                        
                        // Debug: log comparison
                        if (share_zeros >= 5 && hash_difficulty > 10000) {
                            Serial.printf("📊 Share: %dz, diff=%.0f → %s (target-based validation)\n",
                                         share_zeros, hash_difficulty, 
                                         should_submit ? "SUBMIT ✓" : "SKIP ✗");
                        }
                        
                        // Heartbeat strategy: Save valuable shares for periodic submission
                        // The actual heartbeat submission happens outside the mining loop
                        // to ensure regular submissions even when no new hashes are found
                        uint32_t now = millis() / 1000;
                        bool is_heartbeat = false;
                        
                        // Calculate minimum zeros for heartbeat
                        int required_zeros = difficulty_to_zeros(effective_diff);
                        int heartbeat_threshold = (required_zeros > 2) ? (required_zeros - 2) : 5;
                        if (heartbeat_threshold < 5) heartbeat_threshold = 5;
                        
                        if(should_submit) {
                            // Format hash in little-endian (zeros at start) - same as "Found xz hash"
                            // Hash is stored big-endian in memory, so print from byte 31 to 0
                            char hash_le[65];
                            for(int i = 31; i >= 0; i--) {
                                sprintf(hash_le + ((31-i) * 2), "%02x", hash[i]);
                            }
                            hash_le[64] = '\0';
                            
                            Serial.println("\n⭐ VALID SHARE FOUND!");
                            Serial.printf("   Nonce: 0x%08x\n", nonce);
                            Serial.printf("   Difficulty: %.2f (%d zeros)\n", hash_difficulty, share_zeros);
                            Serial.printf("   Hash (LE): %s\n", hash_le);
                            
                            // Submit share to pool
                            // CRITICAL: Nonce must be in BIG-ENDIAN format (like NerdMiner String(nonce, HEX))
                            char nonce_hex[9];
                            snprintf(nonce_hex, sizeof(nonce_hex), "%08x", nonce);
                            
                            // IMPORTANTE: Use ORIGINAL ntime string from job (NerdMiner does this)
                            const char* ntime_hex = current_pool_job.ntime.c_str();
                            
                            // CRITICAL FIX: extranonce2 deve essere in BIG-ENDIAN per Stratum!
                            // NerdMiner usa "00000001" non "01000000"!
                            char extranonce2_hex[17];
                            int hex_len = current_pool_job.extranonce2_size * 2;
                            // Write in BIG-ENDIAN order (most significant byte first)
                            for(int i = current_pool_job.extranonce2_size - 1; i >= 0; i--) {
                                snprintf(extranonce2_hex + ((current_pool_job.extranonce2_size - 1 - i) * 2), 3, "%02x", (extranonce2 >> (i * 8)) & 0xFF);
                            }
                            extranonce2_hex[hex_len] = '\0';
                            
                            // Incrementa share submitted PRIMA di inviare
                            stats->shares_submitted++;
                            
                            // Invia share al pool
                            bool sent = stratum_submit_share(current_pool_job.job_id.c_str(), 
                                                   extranonce2_hex, ntime_hex, nonce_hex);
                            
                            if(sent) {
                                Serial.println("📤 Share inviata al pool (attendo conferma...)");
                            } else {
                                Serial.println("❌ Errore invio share (TCP failed)");
                                stats->shares_rejected++;
                            }
                        } else {
                            // Valuable hash but doesn't meet current pool requirements
                            // Save it for potential heartbeat submission or if pool lowers difficulty
                            if (share_zeros >= 5) {
                                save_pending_share(nonce, hash_difficulty, share_zeros, hash,
                                                 current_pool_job.job_id.c_str(),
                                                 current_pool_job.ntime.c_str(), 
                                                 extranonce2);
                            }
                        }  // End if(should_submit)
                    }  // End if(hash_difficulty >= effective_diff)
                    
                    // NerdMiner does NOT increment extranonce2 - it uses the same value for all shares!
                    // Each nonce already produces a different hash, no need to change extranonce2
                    // extranonce2++; // REMOVED - keep extranonce2 constant like NerdMiner
                    
                    // Don't break - continue mining the rest of the batch!
                    // Breaking would cause nonce to reset to the same value on next iteration
                    // Just continue to next nonce in the batch
                }  // End if (hash passed filter)
            }  // End mining loop
            
            // Update total hashes for this batch (all nonces tested)
            stats->total_hashes += (NONCE_PER_BATCH / nonce_step);
            
            // Calculate hashrate
            uint32_t batch_elapsed = millis() - batch_start_time;
            if(batch_elapsed > 0) {
                stats->hashes_per_second = ((uint64_t)(NONCE_PER_BATCH / nonce_step) * 1000ULL) / batch_elapsed;
            }
            
            // Check for job change every 256 nonces (NerdMiner style)
            if((nonce & 0xFF) == 0 && current_pool_job.job_id != current_job_id) {
                // Job changed, restart with new job
                continue;
            }
            
            // 💓 HEARTBEAT CHECK: Send pending shares periodically to keep worker visible
            // Check every batch if we should send a heartbeat from pending shares
            uint32_t now = millis() / 1000;
            if ((now - last_heartbeat_share_time) >= 90) {
                // Try to find a pending share to submit as heartbeat
                int best_pending_idx = -1;
                int best_pending_zeros = 0;
                
                for(int i = 0; i < MAX_PENDING_SHARES; i++) {
                    if(pending_shares[i].timestamp > 0 && 
                       pending_shares[i].job_id == current_pool_job.job_id &&
                       pending_shares[i].zeros > best_pending_zeros) {
                        best_pending_idx = i;
                        best_pending_zeros = pending_shares[i].zeros;
                    }
                }
                
                // Submit best pending share if found
                if(best_pending_idx >= 0 && best_pending_zeros >= 5) {
                    PendingShare* share = &pending_shares[best_pending_idx];
                    
                    Serial.printf("💓 Heartbeat: Submitting pending share (%.0f diff, %dz) to keep worker visible\n",
                                 share->difficulty, share->zeros);
                    
                    // Format nonce and extranonce2 for submission
                    char nonce_hex[9];
                    snprintf(nonce_hex, sizeof(nonce_hex), "%08x", share->nonce);
                    
                    char extranonce2_hex[17];
                    int hex_len = current_pool_job.extranonce2_size * 2;
                    for(int i = current_pool_job.extranonce2_size - 1; i >= 0; i--) {
                        snprintf(extranonce2_hex + ((current_pool_job.extranonce2_size - 1 - i) * 2), 3, 
                                "%02x", (share->extranonce2 >> (i * 8)) & 0xFF);
                    }
                    extranonce2_hex[hex_len] = '\0';
                    
                    // Submit heartbeat share
                    stats->shares_submitted++;
                    bool sent = stratum_submit_share(share->job_id.c_str(), 
                                                     extranonce2_hex, 
                                                     share->ntime.c_str(), 
                                                     nonce_hex);
                    
                    if(sent) {
                        Serial.println("📤 Heartbeat share sent to pool");
                    } else {
                        Serial.println("❌ Heartbeat send failed");
                        stats->shares_rejected++;
                    }
                    
                    last_heartbeat_share_time = now;
                } else {
                    // No pending shares available, reset timer to try again later
                    Serial.printf("⚠️  No pending shares available for heartbeat (need 5+ zeros)\n");
                    last_heartbeat_share_time = now;  // Reset to avoid spamming this message
                }
            }
            
            // Periodic statistics (every 10 batches ~40K nonces = ~1 second at 40 KH/s)
            static uint32_t batch_counter = 0;
            batch_counter++;
            
            // Print statistics only every 10M hashes (reduced spam)
            if((stats->total_hashes % 10000000) < 4096) {
                Serial.println("\n╔══════════════════════════════════════════════════════════╗");
                Serial.println("║              📊 MINING STATISTICS                       ║");
                Serial.println("╚══════════════════════════════════════════════════════════╝");
                Serial.printf("  Batch #%u completed\n", batch_counter);
                Serial.printf("  Current nonce:    %u (%.1f%%)\n", nonce, (nonce / 42949672.96));
                Serial.printf("  ⚡ Hashrate:       %u H/s (%.1f KH/s)\n", 
                             stats->hashes_per_second, stats->hashes_per_second / 1000.0);
                Serial.printf("  Total hashes:     %u\n", stats->total_hashes);
                Serial.printf("  Best difficulty:  %.0f (%d zeros)\n", 
                             stats->best_difficulty, stats->best_difficulty_zeros);
                Serial.printf("  Shares submitted: %u\n", stats->shares_submitted);
                Serial.printf("  Shares accepted:  %u\n", stats->shares_accepted);
                Serial.printf("  Shares rejected:  %u\n", stats->shares_rejected);
                Serial.printf("  Pool difficulty:  %u (requires %d zeros)\n", 
                             get_effective_difficulty(), difficulty_to_zeros(get_effective_difficulty()));
                Serial.printf("  Uptime:           %u seconds\n", millis() / 1000);
                Serial.println("════════════════════════════════════════════════════════════\n");
            }
            
            // Watchdog reset after batch
            esp_task_wdt_reset();
            vTaskDelay(1);  // Yield to other tasks
            
            continue;  // Next batch
        }  // End MINING_MODE_POOL
        
        // MODALITÀ SOLO/EDUCATIONAL: mining classico
        // Incrementa il nonce per ogni tentativo
        header.nonce++;
        
        // Calcola il doppio SHA-256 del block header (80 bytes)
        // Questo è il cuore del mining Bitcoin!
        double_sha256((uint8_t*)&header, sizeof(BlockHeader), hash);
        
        hashes++;
        stats->total_hashes++;
        
        // 🔧 WATCHDOG FIX: Yield every 4K hashes in educational mode too
        if((hashes % 4000) == 0) {
            vTaskDelay(1);
            esp_task_wdt_reset();
        }
        
        // Aggiorna hashrate ogni 100 hash per display reattivo
        if((stats->total_hashes % 100) == 0) {
            uint32_t elapsed = millis() - start_time;
            if(elapsed > 0) {
                stats->hashes_per_second = (hashes * 1000) / elapsed;
            }
        }
        
        // Conta zeri iniziali per statistiche
        int zeros = count_leading_zeros(hash);
        if(zeros > best_zeros) {
            best_zeros = zeros;
            stats->best_difficulty = zeros;
            hash_to_hex(hash, stats->best_hash);
        }
        
        // Verifica se abbiamo trovato un hash valido
        if(check_hash_difficulty(hash, header.bits)) {
            hash_to_hex(hash, hash_hex);
            blocks_found++;
            stats->blocks_found = blocks_found;  // Update global stats
            
            Serial.println();
            Serial.println("╔════════════════════════════════════════════════════════╗");
            Serial.println("║           🎉 BLOCCO VALIDO TROVATO! 🎉                ║");
            Serial.println("╚════════════════════════════════════════════════════════╝");
            Serial.printf("🏆 Blocco #%u trovato!\n", blocks_found);
            Serial.printf("   Nonce: %u (0x%08x)\n", header.nonce, header.nonce);
            Serial.printf("   Zeri iniziali: %d\n", zeros);
            Serial.printf("   Hash: %s\n", hash_hex);
            Serial.printf("   Tentativi necessari: %u\n", hashes);
            Serial.println();
            
            if(currentMiningMode == MINING_MODE_POOL) {
                Serial.println("💡 Inviando share al pool...");
                // TODO: Implementare submit della share
            } else {
                Serial.println("💡 In un vero miner, questo blocco verrebbe inviato!");
            }
            
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            Serial.println();
            
            // Reset per nuovo blocco (solo in modalità educational)
            if(currentMiningMode == MINING_MODE_EDUCATIONAL) {
                header.nonce = 0;
                header.timestamp = millis() / 1000;
                // Nuovo merkle root (simula nuove transazioni)
                for(int i = 0; i < 32; i++) {
                    header.merkleRoot[i] = random(0, 256);
                }
                hashes = 0;
                start_time = millis();
            }
        }
        
        // Reset contatori ogni secondo per evitare overflow
        uint32_t elapsed = millis() - start_time;
        if(elapsed >= 1000) {
            
            // Stampa stats solo ogni 5 secondi per ridurre overhead seriale
            static uint32_t last_print = 0;
            if(millis() - last_print >= 5000) {
                hash_to_hex(hash, hash_hex);
                
                Serial.println("┌─────────────────────────────────────────────────────────┐");
                Serial.printf("│ ⚡ Hash/s: %-8u  📊 Nonce: %-12u      │\n", 
                    stats->hashes_per_second, header.nonce);
                Serial.printf("│ 🔢 Totale: %-10u ⏱️  Tempo: %-4u sec        │\n", 
                    stats->total_hashes, elapsed/1000);
                Serial.printf("│ 🏆 Blocchi: %-2u        🎯 Miglior: %d zeri         │\n",
                    blocks_found, best_zeros);
                Serial.println("├─────────────────────────────────────────────────────────┤");
                Serial.printf("│ 🔍 Ultimo hash calcolato:                              │\n");
                Serial.printf("│ %.56s... │\n", hash_hex);
                Serial.println("└─────────────────────────────────────────────────────────┘");
                Serial.println();
                
                last_print = millis();
            }
            
            hashes = 0;
            start_time = millis();
        }
    }
    
    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║           MINING TASK STOPPED                         ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.printf("📊 Statistiche finali:\n");
    Serial.printf("   Totale hash calcolati: %u\n", stats->total_hashes);
    Serial.printf("   Blocchi trovati: %u\n", blocks_found);
    Serial.printf("   Miglior difficoltà: %d zeri iniziali\n", best_zeros);
    
    // Disconnetti dal pool se connesso
    if(currentMiningMode == MINING_MODE_POOL) {
        stratum_disconnect();
        Serial.println("   Disconnesso dal pool");
    }
    
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println();
    
    // Delete this task and clear handle based on worker ID
    if (worker_id == 0) {
        Serial.printf("Worker %d exiting, clearing handle\n", worker_id);
        miningTaskHandle = NULL;
    } else {
        Serial.printf("Worker %d exiting, clearing handle 2\n", worker_id);
        miningTaskHandle2 = NULL;
    }
    vTaskDelete(NULL);
}

// Start the mining task
void mining_task_start(void)
{
    // Don't start if already running
    if (miningTaskHandle != NULL) {
        Serial.println("⚠️  Mining task già in esecuzione!");
        return;
    }
    
    // Reset statistiche per entrambi i worker
    memset(&stats_worker0, 0, sizeof(MiningStats));
    memset(&stats_worker1, 0, sizeof(MiningStats));
    
    // Create FreeRTOS tasks with NerdMiner architecture:
    // 1. Stratum network task on Core 1, priority 4 (like NerdMiner)
    // 2. Two mining workers unpinned, priority 1 (like NerdMiner)
    
    // Crea task stratum se siamo in modalità pool
    if (currentMiningMode == MINING_MODE_POOL) {
        xTaskCreatePinnedToCore(
            stratumTask,           // Task function
            "Stratum",             // Task name
            12000,                 // Stack size (12KB like NerdMiner)
            NULL,                  // Task parameter
            4,                     // Priority 4 (like NerdMiner)
            &stratumTaskHandle,    // Task handle
            1                      // Core 1 (like NerdMiner, with Monitor)
        );
        
        if (stratumTaskHandle != NULL) {
            Serial.println("✅ Stratum task created on Core 1, priority 4");
        } else {
            Serial.println("❌ ERROR: Cannot create stratum task!");
        }
    }
    
    // 🚀 DUAL-THREADED MINING (NerdMiner architecture)
    // Create two mining workers, unpinned, priority 1
    // Worker 0: even nonces (0, 2, 4, 6, ...)
    // Worker 1: odd nonces (1, 3, 5, 7, ...)
    
    Serial.println("Creating dual mining workers (NerdMiner architecture)...");
    
    xTaskCreate(
        miningTask,           // Task function
        "MinerSw-0",          // Task name (like NerdMiner)
        8192,                 // Stack size (8KB)
        (void*)0,             // Worker ID = 0 (even nonces)
        1,                    // Priority 1 (like NerdMiner)
        &miningTaskHandle     // Task handle
    );
    
    if (miningTaskHandle != NULL) {
        Serial.println("✅ MinerSw-0 created (priority 1, unpinned)");
    } else {
        Serial.println("❌ ERROR: Cannot create MinerSw-0!");
        return;
    }
    
    xTaskCreate(
        miningTask,           // Task function
        "MinerSw-1",          // Task name (like NerdMiner)
        8192,                 // Stack size (8KB)
        (void*)1,             // Worker ID = 1 (odd nonces)
        1,                    // Priority 1 (like NerdMiner)
        &miningTaskHandle2    // Task handle
    );
    
    if (miningTaskHandle2 != NULL) {
        Serial.println("✅ MinerSw-1 created (priority 1, unpinned)");
    } else {
        Serial.println("❌ ERROR: Cannot create MinerSw-1!");
    }
}

// Stop the mining task
void mining_task_stop(void)
{
    if (miningTaskHandle == NULL && miningTaskHandle2 == NULL && stratumTaskHandle == NULL) {
        Serial.println("⚠️  Mining task non in esecuzione");
        return;
    }
    
    Serial.println("⏹️  Fermando mining tasks...");
    
    // Signal all tasks to stop
    taskRunning = false;
    stratumTaskRunning = false;
    
    // Wait for mining workers to clean up
    while(miningTaskHandle != NULL || miningTaskHandle2 != NULL) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    // Wait for stratum task to clean up
    while(stratumTaskHandle != NULL) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    Serial.println("✅ Mining tasks fermati");
}

// Check if mining task is running
bool mining_task_is_running(void)
{
    return ((miningTaskHandle != NULL || miningTaskHandle2 != NULL) && taskRunning);
}

// Get current mining statistics
MiningStats mining_get_stats(void)
{
    // Combine stats from both workers
    MiningStats combined = {0};
    
    // Sum hashrates from both workers
    combined.hashes_per_second = stats_worker0.hashes_per_second + stats_worker1.hashes_per_second;
    
    // Sum total hashes
    combined.total_hashes = stats_worker0.total_hashes + stats_worker1.total_hashes;
    
    // Use the best difficulty found across both workers
    if(stats_worker0.best_difficulty > stats_worker1.best_difficulty) {
        combined.best_difficulty = stats_worker0.best_difficulty;
        combined.best_difficulty_zeros = stats_worker0.best_difficulty_zeros;
        memcpy(combined.best_hash, stats_worker0.best_hash, 65);
    } else {
        combined.best_difficulty = stats_worker1.best_difficulty;
        combined.best_difficulty_zeros = stats_worker1.best_difficulty_zeros;
        memcpy(combined.best_hash, stats_worker1.best_hash, 65);
    }
    
    // Sum blocks found
    combined.blocks_found = stats_worker0.blocks_found + stats_worker1.blocks_found;
    
    // Sum shares
    combined.shares_submitted = stats_worker0.shares_submitted + stats_worker1.shares_submitted;
    combined.shares_accepted = stats_worker0.shares_accepted + stats_worker1.shares_accepted;
    combined.shares_rejected = stats_worker0.shares_rejected + stats_worker1.shares_rejected;
    
    // Copy pool connection status and block height from worker 0
    combined.pool_connected = stats_worker0.pool_connected;
    combined.block_height = stats_worker0.block_height;
    
    return combined;
}

// Get best zeros from both cores separately for display
void mining_get_dual_core_stats(int* core0_zeros, int* core1_zeros)
{
    if (core0_zeros) *core0_zeros = stats_worker0.best_difficulty_zeros;
    if (core1_zeros) *core1_zeros = stats_worker1.best_difficulty_zeros;
}

// Check if a block has been found
bool mining_has_found_block(void)
{
    return (stats_worker0.blocks_found + stats_worker1.blocks_found) > 0;
}

// Configura nodo Bitcoin per mining SOLO
void mining_set_bitcoin_node(const char* host, uint16_t port, const char* user, const char* pass)
{
    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║        CONFIGURAZIONE NODO BITCOIN                    ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    
    bitcoin_rpc_init(host, port, user, pass);
    
    // Test connessione
    if(bitcoin_rpc_test_connection()) {
        Serial.println("✅ Nodo Bitcoin configurato correttamente!");
        Serial.println("   Puoi ora usare mining_set_mode(MINING_MODE_SOLO)");
    } else {
        Serial.println("⚠️  Configurazione salvata ma connessione fallita");
        Serial.println("   Verifica configurazione e rete");
    }
    Serial.println();
}

// Configura pool per mining POOL
void mining_set_pool(const char* pool_url_str, uint16_t port, const char* wallet_address,
                     const char* worker_name, const char* password)
{
    Serial.println();
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║        CONFIGURAZIONE POOL MINING                     ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    
    pool_url = pool_url_str;
    pool_port = port;
    pool_wallet = wallet_address;
    pool_worker = worker_name ? worker_name : "esp32";
    pool_password = password ? password : "x";
    
    Serial.printf("✅ Pool configurato: %s:%d\n", pool_url.c_str(), pool_port);
    Serial.printf("   Wallet: %s\n", pool_wallet.c_str());
    Serial.printf("   Worker: %s\n", pool_worker.c_str());
    Serial.println("   Usa mining_set_mode(MINING_MODE_POOL) per attivare");
    Serial.println();
}

// Imposta modalità mining
void mining_set_mode(MiningMode mode)
{
    currentMiningMode = mode;
    
    switch(mode) {
        case MINING_MODE_EDUCATIONAL:
            Serial.println("🎓 Modalità MINING EDUCATIVO attivata");
            Serial.println("   Usa blocchi di esempio con difficoltà ridotta");
            break;
        case MINING_MODE_SOLO:
            Serial.println("🚀 Modalità SOLO MINING attivata!");
            Serial.println("   I blocchi verranno recuperati dal nodo Bitcoin");
            Serial.println("   ⚠️  Assicurati di aver configurato il nodo!");
            break;
        case MINING_MODE_POOL:
            Serial.println("� Modalità POOL MINING attivata!");
            Serial.println("   Connessione al pool Stratum");
            Serial.println("   ⚠️  Assicurati di aver configurato il pool!");
            break;
    }
}

// Ottieni modalità mining corrente
MiningMode mining_get_mode(void)
{
    return currentMiningMode;
}

bool mining_is_educational_fallback(void)
{
    return isEducationalFallback;
}

