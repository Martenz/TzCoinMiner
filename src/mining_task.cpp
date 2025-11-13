#include "mining_task.h"
#include "bitcoin_rpc.h"
#include "stratum_client.h"
#include "wifi_config.h"
#include "shaLib/sha256_hard.h"     // Wrapper con midstate support
#include "shaLib/nerdSHA256plus.h"  // NerdMiner optimized SHA
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>  // For explicit watchdog reset

// Forward declarations
int difficulty_to_zeros(uint32_t difficulty);

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
static uint32_t extranonce2 = 0;  // Counter for extranonce2

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
    Serial.println("📬 Nuovo job dal pool!");
    Serial.printf("   Job ID: %s\n", job->job_id.c_str());
    Serial.printf("   Clean: %s\n", job->clean_jobs ? "YES" : "NO");
    
    // Salva il job corrente
    current_pool_job = *job;
    has_pool_job = true;
    need_rebuild_header = true;  // Segnala che serve ricostruire header
    
    // Aggiorna difficoltà dal pool
    pool_difficulty = stratum_get_difficulty();
    
    // Mostra difficulty effettiva (usa minDifficulty se pool_difficulty è 0)
    uint32_t effective = pool_difficulty;
    extern WifiConfig config;
    if(effective == 0 && config.minDifficulty > 0) {
        effective = config.minDifficulty;
    }
    if(effective == 0) {
        effective = 1; // Fallback minimo
    }
    
    Serial.printf("   Pool Difficulty: %u (effettiva: %u, richiede %d zeros)\n", 
                 pool_difficulty, effective, difficulty_to_zeros(effective));
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
    // usa minDifficulty configurato come fallback (o default 1)
    if (pool_difficulty == 0) {
        uint32_t fallback = (config.minDifficulty > 0) ? config.minDifficulty : 1;
        return fallback;
    }
    
    // Se il pool ha inviato una difficulty, USALA SEMPRE (il pool sa cosa fa)
    // NON applicare floor hardware, lascia che sia il pool a decidere
    return pool_difficulty;
}

// Approssimazione: converte difficoltà in zeri richiesti
// Basato su: difficulty ≈ 2^(zeros * 4) / 65536
// Diff 1 = ~8 zeros, Diff 4 = ~9 zeros, Diff 256 = ~12 zeros, Diff 65536 = ~16 zeros
// TEMPORANEO: difficulty abbassate DRASTICAMENTE per TEST FINALE
// Converte difficulty in numero di zeri richiesti nell'hash
int difficulty_to_zeros(uint32_t difficulty) {
    // Bitcoin difficulty to leading zero BITS (not bytes!)
    // Difficulty 1 = target with 32 leading zero bits = 8 leading zero NIBBLES (hex digits)
    // Each nibble is 4 bits, so 32 bits = 8 hex digits
    // count_leading_zeros counts NIBBLES (hex digits), not bytes
    
    if(difficulty <= 1) return 8;          // diff 1 = 32 zero bits = 8 hex digits
    if(difficulty <= 4) return 10;         // diff 4 = ~40 zero bits
    if(difficulty <= 16) return 11;        // diff 16 
    if(difficulty <= 64) return 12;        // diff 64
    if(difficulty <= 256) return 13;       // diff 256
    if(difficulty <= 1024) return 14;      // diff 1K
    if(difficulty <= 4096) return 15;      // diff 4K
    if(difficulty <= 16384) return 16;     // diff 16K
    if(difficulty <= 65536) return 17;     // diff 64K
    if(difficulty <= 262144) return 18;    // diff 256K
    if(difficulty <= 1048576) return 19;   // diff 1M
    return 20; // Qualsiasi altra difficoltà
}

// NerdMiner-style difficulty calculation from hash
// Bitcoin difficulty 1 target
const double truediffone = 26959535291011309493156476344723991336010898738574164086137773096960.0;

// Convert little-endian 256-bit value to double
static double le256todouble(const void* target) {
    uint64_t v64;
    const uint8_t* data = (const uint8_t*)target;
    
    // Find first non-zero byte from the end (big-endian perspective)
    int i;
    for (i = 31; i >= 0; i--) {
        if (data[i] != 0)
            break;
    }
    
    if (i < 0)
        return 0.0;
    
    // Take up to 8 bytes for double precision
    int start = (i >= 7) ? (i - 7) : 0;
    v64 = 0;
    
    for (int j = i; j >= start; j--) {
        v64 = (v64 << 8) | data[j];
    }
    
    // Adjust for position
    int shift = start * 8;
    double d = (double)v64;
    
    // Scale by position
    for (int j = 0; j < shift; j += 8) {
        d *= 256.0;
    }
    
    return d;
}

// Calculate difficulty from hash (NerdMiner algorithm)
double diff_from_target(const uint8_t* hash) {
    double d64 = truediffone;
    double dcut64 = le256todouble(hash);
    
    if (dcut64 == 0.0)
        dcut64 = 1.0;
    
    return d64 / dcut64;
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
                
                // Nonce - inizia da 0 e incrementa
                pool_header.nonce = 0;
                
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
            
            // ⚠️ IMPORTANTE: Ricalcola merkle root quando necessario
            static uint32_t last_extranonce2 = 0xFFFFFFFF;
            static String last_job_id = "";
            bool need_recalc = false;
            
            // Ricalcola se:
            // 1. Extranonce2 cambiato (dopo submit share)
            // 2. Nuovo job ricevuto
            // 3. Prima volta in assoluto (last_job_id vuoto)
            if(extranonce2 != last_extranonce2 || 
               current_pool_job.job_id != last_job_id ||
               last_job_id.length() == 0) {
                need_recalc = true;
            }
            
            Serial.printf("DEBUG: job=%s, last_job=%s, ex2=%u, last_ex2=%u, need_recalc=%d\n", 
                         current_pool_job.job_id.c_str(), last_job_id.c_str(),
                         extranonce2, last_extranonce2, need_recalc);
            
            if(need_recalc) {
                Serial.printf("🔄 Ricalcolo merkle root... (extranonce2=%u, job=%s)\n", 
                             extranonce2, current_pool_job.job_id.c_str());
                uint8_t coinbase_hash[32];
                build_coinbase(&current_pool_job, extranonce2, coinbase_hash);
                calculate_merkle_root(coinbase_hash, current_pool_job.merkle_branch, pool_header.merkleRoot);
                
                // 🚀 PERFORMANCE CRITICA: Pre-calcola midstate + bake!
                calc_midstate((uint8_t*)&pool_header);
                
                last_extranonce2 = extranonce2;
                last_job_id = current_pool_job.job_id;
            } else {
                Serial.println("✅ Merkle/midstate già calcolati, riuso valori!");
            }
            
            // Loop continuo su TUTTI i nonces senza ricalcolare merkle!
            // Statistiche ogni NONCE_TEST_BATCH per monitoraggio
            const uint32_t NONCE_TEST_BATCH = 5000000;  // 5M nonces tra stats
            uint32_t batch_end = nonce + NONCE_TEST_BATCH;
            uint32_t batch_start_time = millis();
            uint32_t batch_hashes = 0;  // Conta hash in questo batch
            bool first_stats_sent = false;  // Flag per primo aggiornamento rapido stats
            
            // ⚡ OTTIMIZZAZIONE CRITICA: Pre-calcola valori fuori dal loop
            uint32_t effective_diff = get_effective_difficulty();
            int required_zeros = difficulty_to_zeros(effective_diff);
            const int MIN_ZEROS_THRESHOLD = 1;
            int effective_zeros_needed = (required_zeros > MIN_ZEROS_THRESHOLD) ? required_zeros : MIN_ZEROS_THRESHOLD;
            
            Serial.printf("⛏️  Worker %d batch: %u nonces, diff=%u, zeros=%d, starting nonce=%u\n", 
                         worker_id, NONCE_TEST_BATCH, effective_diff, effective_zeros_needed, nonce);
            
            // Salva job_id corrente per verificare se cambia durante il batch
            String current_job_id = current_pool_job.job_id;
            
            // 🚀 PERFORMANCE CRITICAL: Prepare data buffer for optimized mining
            // NerdMiner approach: modify only nonce bytes, keep rest constant
            uint8_t header_bytes[80];
            memcpy(header_bytes, &pool_header, 80);
            
            // Performance test: measure 10K hashes
            uint32_t perf_test_start = micros();
            const uint32_t PERF_TEST_COUNT = 10000;
            
            // 🔥 DUAL-WORKER OPTIMIZATION: Step by 2, each worker handles even/odd nonces
            // Worker 0: 0, 2, 4, 6, 8, ...
            // Worker 1: 1, 3, 5, 7, 9, ...
            const uint32_t nonce_step = 2;
            
            for(; nonce < batch_end && taskRunning; nonce += nonce_step) {
                // ⚡ OTTIMIZZAZIONE CRITICA: Modifica SOLO i 4 bytes del nonce (offset 76)
                ((uint32_t*)(header_bytes + 76))[0] = nonce;
                
                // ⚡ CHIAVE: Usa sha256_double_hash_80 con midstate pre-calcolato!
                sha256_double_hash_80(header_bytes, hash);
                
                // Performance measurement
                if(batch_hashes == PERF_TEST_COUNT) {
                    uint32_t perf_elapsed = micros() - perf_test_start;
                    uint32_t perf_khash = (PERF_TEST_COUNT * 1000000ULL) / perf_elapsed / 1000;
                    Serial.printf("⚡ PERF TEST: %u hashes in %u μs = %u KH/s\n", 
                                 PERF_TEST_COUNT, perf_elapsed, perf_khash);
                }
                
                batch_hashes++;
                stats->total_hashes++;
                
                // � EARLY STATS UPDATE: Calcola hashrate dopo primo milione per feedback rapido
                if(!first_stats_sent && batch_hashes >= 1000000) {
                    uint32_t early_elapsed = millis() - batch_start_time;
                    if(early_elapsed > 0) {
                        stats->hashes_per_second = ((uint64_t)batch_hashes * 1000ULL) / early_elapsed;
                        Serial.printf("📊 Early stats (1M hashes): %u H/s (%.1f KH/s)\n",
                                     stats->hashes_per_second, stats->hashes_per_second / 1000.0);
                    }
                    first_stats_sent = true;
                }
                
                // �🔧 WATCHDOG FIX: Yield every 8K hashes (safer for slower speeds)
                // At 33 KH/s: 8K hashes = ~242ms work
                // Light yield (1 tick ~10ms) = ~4% overhead
                if((batch_hashes % 4000) == 0) {
                    vTaskDelay(2);
                    esp_task_wdt_reset();
                }
                
                // ⚡ OTTIMIZZAZIONE 1: Quick check PRIMA di invertire byte!
                // Se hash[31] != 0, impossibile avere 1+ zero → skip tutto
                if(hash[31] != 0) continue;
                
                // ⚡ OTTIMIZZAZIONE 2: Reverse solo se hash promettente
                uint8_t hash_reversed[32];
                for(int j = 0; j < 32; j++) {
                    hash_reversed[j] = hash[31 - j];
                }
                
                // Conta zeri solo ora
                int zeros = count_leading_zeros(hash_reversed);
                
                // Aggiorna best assoluto solo se migliora (track by zeros for speed)
                if(zeros > absolute_best_zeros) {
                    absolute_best_zeros = zeros;
                    hash_to_hex(hash_reversed, hash_hex);
                    memcpy(absolute_best_hash, hash_hex, 65);
                    
                    // Calculate difficulty only when we find a new best
                    double hash_difficulty = diff_from_target(hash_reversed);
                    stats->best_difficulty = hash_difficulty;
                    stats->best_difficulty_zeros = zeros;
                    memcpy(stats->best_hash, hash_hex, 65);
                    
                    if(zeros >= 4) {
                        Serial.printf("🎯 New best: %d zeros (%.0f difficulty) - Hash: %s\n", 
                                     zeros, hash_difficulty, hash_hex);
                    }
                }
                
                // Aggiorna best periodo
                if(zeros > best_zeros) {
                    best_zeros = zeros;
                }
                
                // 🚀 NERDMINER SHARE VALIDATION: Only calculate difficulty for high-zero hashes
                // For pool difficulty validation, only check hashes with enough zeros
                if(zeros >= (required_zeros - 1)) {  // Check hashes close to requirement
                    double hash_difficulty = diff_from_target(hash_reversed);
                    
                    if(hash_difficulty >= (double)effective_diff) {
                        char share_hash_hex[65];
                        hash_to_hex(hash_reversed, share_hash_hex);
                        
                        Serial.println("⭐ VALID SHARE FOUND!");
                        Serial.printf("   Nonce: 0x%08x\n", pool_header.nonce);
                        Serial.printf("   Hash: %s\n", share_hash_hex);
                        Serial.printf("   Hash Difficulty: %.2f (zeros: %d)\n", hash_difficulty, zeros);
                        Serial.printf("   Pool Difficulty: %u\n", effective_diff);
                        Serial.printf("   Extranonce2: 0x%08x\n", extranonce2);
                    
                    // Prepara dati per submit (CORRETTO byte order come NerdMiner)
                    char nonce_hex[9];
                    // IMPORTANTE: Nonce in little-endian per Stratum
                    snprintf(nonce_hex, sizeof(nonce_hex), "%02x%02x%02x%02x",
                             (pool_header.nonce >> 0) & 0xFF,
                             (pool_header.nonce >> 8) & 0xFF,
                             (pool_header.nonce >> 16) & 0xFF,
                             (pool_header.nonce >> 24) & 0xFF);
                    
                    char ntime_hex[9];
                    // IMPORTANTE: Ntime in little-endian per Stratum
                    snprintf(ntime_hex, sizeof(ntime_hex), "%02x%02x%02x%02x",
                             (pool_header.timestamp >> 0) & 0xFF,
                             (pool_header.timestamp >> 8) & 0xFF,
                             (pool_header.timestamp >> 16) & 0xFF,
                             (pool_header.timestamp >> 24) & 0xFF);
                    
                    // Converte extranonce2 in hex string (little endian)
                    char extranonce2_hex[17];
                    int hex_len = current_pool_job.extranonce2_size * 2;
                    for(int i = 0; i < current_pool_job.extranonce2_size; i++) {
                        snprintf(extranonce2_hex + (i * 2), 3, "%02x", (extranonce2 >> (i * 8)) & 0xFF);
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
                    
                    // Incrementa extranonce2 per la prossima share e ricalcola merkle
                    extranonce2++;
                    break;  // Esci dal batch e ricalcola merkle root
                    }  // Fine if(hash_difficulty >= effective_diff)
                }  // Fine if(zeros >= required_zeros - 1)
            }  // Fine for nonce loop
            
            // Calcola hashrate dopo ogni batch
            uint32_t batch_elapsed = millis() - batch_start_time;
            if(batch_elapsed > 0) {
                // FIX: Use uint64_t to avoid overflow with large batch_hashes
                stats->hashes_per_second = ((uint64_t)batch_hashes * 1000ULL) / batch_elapsed;
            }
            
            Serial.printf("DEBUG HASHRATE: batch_hashes=%u, elapsed=%u, calculated=%u\n",
                         batch_hashes, batch_elapsed, stats->hashes_per_second);
            
            // Ogni batch completato (o overflow nonce), incrementa extranonce2
            if(nonce == 0) {  // Overflow dopo 4.3 miliardi
                extranonce2++;
            }
            
            // Log statistiche ogni 1 batch (ogni 5M nonces)
            static uint32_t total_batches = 0;
            total_batches++;
            
            Serial.println("\n╔══════════════════════════════════════════════════════════╗");
            Serial.println("║              📊 MINING STATISTICS                       ║");
            Serial.println("╚══════════════════════════════════════════════════════════╝");
            Serial.printf("  Batch #%u completed\n", total_batches);
            Serial.printf("  Current nonce:    %u (%.1f%%)\n", nonce, (nonce / 42949672.96));
            Serial.printf("  ⚡ Hashrate:       %u H/s (%.1f KH/s)\n", 
                         stats->hashes_per_second, stats->hashes_per_second / 1000.0);
            Serial.printf("  Batch time:       %u ms (%.1f sec)\n", batch_elapsed, batch_elapsed / 1000.0);
            Serial.printf("  Batch hashes:     %u\n", batch_hashes);
            Serial.printf("  Total hashes:     %u\n", stats->total_hashes);
            Serial.printf("  Absolute best:    %.0f difficulty (%u zeros)\n", 
                         stats->best_difficulty, absolute_best_zeros);
            if(absolute_best_zeros > 0) {
                Serial.printf("  Best hash:        %s\n", absolute_best_hash);
            }
            Serial.printf("  Shares submitted: %u\n", stats->shares_submitted);
            Serial.printf("  Shares accepted:  %u\n", stats->shares_accepted);
            Serial.printf("  Shares rejected:  %u\n", stats->shares_rejected);
            Serial.printf("  Pool difficulty:  %u (requires %d zeros)\n", 
                         get_effective_difficulty(), difficulty_to_zeros(get_effective_difficulty()));
            Serial.printf("  Uptime:           %u seconds\n", millis() / 1000);
            Serial.println("════════════════════════════════════════════════════════════\n");
            
            continue;  // Torna all'inizio del while loop
        }  // Fine if(currentMiningMode == MINING_MODE_POOL)
        
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
            vTaskDelay(2);
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
    
    return combined;
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

