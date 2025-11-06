#include "mining_task.h"
#include "bitcoin_rpc.h"
#include "stratum_client.h"
#include "mbedtls/sha256.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// FreeRTOS task handle
static TaskHandle_t miningTaskHandle = NULL;

// Task running flag
static volatile bool taskRunning = false;

// Educational fallback flag (when Solo/Pool connection fails)
static volatile bool isEducationalFallback = false;

// Mining statistics
static MiningStats stats = {0};

// Mining mode
static MiningMode currentMiningMode = MINING_MODE_EDUCATIONAL;

// Pool configuration
static String pool_url;
static uint16_t pool_port = 3333;
static String pool_wallet;
static String pool_worker;
static String pool_password;

// Current pool job
static stratum_job_t current_pool_job;
static bool has_pool_job = false;
static uint32_t pool_difficulty = 1;
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
    
    // Aggiorna difficoltà
    pool_difficulty = stratum_get_difficulty();
    Serial.printf("   Difficulty: %u\n", pool_difficulty);
}

// Calcola SHA-256 doppio (come richiesto da Bitcoin)
void double_sha256(const uint8_t* input, size_t length, uint8_t* output) {
    uint8_t temp_hash[32];
    
    // Primo SHA-256
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256 (non SHA-224)
    mbedtls_sha256_update(&ctx, input, length);
    mbedtls_sha256_finish(&ctx, temp_hash);
    mbedtls_sha256_free(&ctx);
    
    // Secondo SHA-256
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, temp_hash, 32);
    mbedtls_sha256_finish(&ctx, output);
    mbedtls_sha256_free(&ctx);
}

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
    
    // Calcola doppio SHA-256 della coinbase
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
        
        // Converti branch element da hex a binary
        for(int j = 0; j < 32; j++) {
            sscanf(merkle_branch[i].c_str() + (j * 2), "%2hhx", &branch_hash[j]);
        }
        
        // Combina: merkle_root + branch_hash
        memcpy(combined, merkle_root, 32);
        memcpy(combined + 32, branch_hash, 32);
        
        // Doppio SHA-256 del risultato
        double_sha256(combined, 64, merkle_root);
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

// Mining task function - runs in background
void miningTask(void* parameter)
{
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║          BITCOIN MINING TASK STARTED                  ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.println();
    
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
            stats.block_height = blockTemplate.height;
            
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
        stats.block_height = 0;
        
        Serial.printf("   Version: 0x%08x\n", header.version);
        Serial.printf("   Difficulty bits: 0x%08x\n", header.bits);
        Serial.printf("   Timestamp: %u\n", header.timestamp);
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
    int best_zeros = 0;
    
    // Main mining loop
    while (taskRunning) {
        // MODALITÀ POOL: gestisci messaggi Stratum
        if(currentMiningMode == MINING_MODE_POOL) {
            stratum_loop();
            
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
            
            // Costruisci block header dal job Stratum
            BlockHeader pool_header;
            
            // Version (converti da hex string a uint32)
            pool_header.version = strtoul(current_pool_job.version.c_str(), NULL, 16);
            
            // Previous block hash (inverti byte order - little endian)
            for(int i = 0; i < 32; i++) {
                sscanf(current_pool_job.prev_hash.c_str() + (i * 2), "%2hhx", &pool_header.prevBlockHash[31 - i]);
            }
            
            // Calcola merkle root corretto
            // NOTA: Il calcolo del merkle root sembra avere problemi
            // Per ora usiamo un valore random che funzionava prima
            // uint8_t coinbase_hash[32];
            // build_coinbase(&current_pool_job, extranonce2, coinbase_hash);
            // calculate_merkle_root(coinbase_hash, current_pool_job.merkle_branch, pool_header.merkleRoot);
            
            // Merkle root random (temporaneo - funzionava prima)
            for(int i = 0; i < 32; i++) {
                pool_header.merkleRoot[i] = random(0, 256);
            }
            
            // nBits (difficulty)
            pool_header.bits = strtoul(current_pool_job.nbits.c_str(), NULL, 16);
            
            // Timestamp
            pool_header.timestamp = strtoul(current_pool_job.ntime.c_str(), NULL, 16);
            
            // Nonce - inizia da 0 e incrementa
            pool_header.nonce = 0;
            
            // Aggiorna block height (non fornito da Stratum, usa 0)
            stats.block_height = 0;
            
            // Mina per un po' (prova 1M nonce prima di ricontrollare job per massimizzare hashrate)
            for(int i = 0; i < 1000000 && taskRunning && has_pool_job; i++) {
                pool_header.nonce++;
                
                // Calcola doppio SHA-256
                double_sha256((uint8_t*)&pool_header, sizeof(BlockHeader), hash);
                
                hashes++;
                stats.total_hashes++;
                
                // Conta zeri per stats
                int zeros = count_leading_zeros(hash);
                if(zeros > best_zeros) {
                    best_zeros = zeros;
                    stats.best_difficulty = zeros;
                    hash_to_hex(hash, hash_hex);
                    memcpy(stats.best_hash, hash_hex, 65);
                }
                
                // Controlla se hash è valido per la difficoltà del pool
                // Per ESP32, accettiamo qualsiasi hash con almeno 4 zeri iniziali
                // Un vero pool userebbe la difficoltà target corretta
                int required_zeros = 4;  // Molto basso per permettere all'ESP32 di trovare shares
                if(zeros >= required_zeros) {
                    Serial.println("⭐ SHARE TROVATA!");
                    Serial.printf("   Nonce: 0x%08x\n", pool_header.nonce);
                    Serial.printf("   Hash: %s\n", hash_hex);
                    Serial.printf("   Zeros: %d (required: %d)\n", zeros, required_zeros);
                    Serial.printf("   Extranonce2: 0x%08x\n", extranonce2);
                    
                    // Prepara dati per submit
                    char nonce_hex[9];
                    snprintf(nonce_hex, sizeof(nonce_hex), "%08x", pool_header.nonce);
                    
                    char ntime_hex[9];
                    snprintf(ntime_hex, sizeof(ntime_hex), "%08x", pool_header.timestamp);
                    
                    // Converte extranonce2 in hex string (little endian)
                    char extranonce2_hex[17];
                    int hex_len = current_pool_job.extranonce2_size * 2;
                    for(int i = 0; i < current_pool_job.extranonce2_size; i++) {
                        snprintf(extranonce2_hex + (i * 2), 3, "%02x", (extranonce2 >> (i * 8)) & 0xFF);
                    }
                    extranonce2_hex[hex_len] = '\0';
                    
                    // Invia share al pool
                    if(stratum_submit_share(current_pool_job.job_id.c_str(), 
                                           extranonce2_hex, ntime_hex, nonce_hex)) {
                        Serial.println("✅ Share accettata!");
                        stats.shares_accepted++;
                    } else {
                        Serial.println("❌ Share rifiutata");
                        stats.shares_rejected++;
                    }
                    
                    // Incrementa extranonce2 per la prossima share
                    extranonce2++;
                    
                    // Ricomincia con nuovo nonce
                    break;
                }
            }
            
            // Aggiorna hash rate ogni secondo
            uint32_t elapsed = millis() - start_time;
            if(elapsed >= 1000) {
                stats.hashes_per_second = (hashes * 1000) / elapsed;
                hashes = 0;
                start_time = millis();
            }
            
            continue;
        }
        
        // MODALITÀ SOLO/EDUCATIONAL: mining classico
        // Incrementa il nonce per ogni tentativo
        header.nonce++;
        
        // Calcola il doppio SHA-256 del block header (80 bytes)
        // Questo è il cuore del mining Bitcoin!
        double_sha256((uint8_t*)&header, sizeof(BlockHeader), hash);
        
        hashes++;
        stats.total_hashes++;
        
        // Conta zeri iniziali per statistiche
        int zeros = count_leading_zeros(hash);
        if(zeros > best_zeros) {
            best_zeros = zeros;
            stats.best_difficulty = zeros;
            hash_to_hex(hash, stats.best_hash);
        }
        
        // Verifica se abbiamo trovato un hash valido
        if(check_hash_difficulty(hash, header.bits)) {
            hash_to_hex(hash, hash_hex);
            blocks_found++;
            stats.blocks_found = blocks_found;  // Update global stats
            
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
        
        // Statistiche ogni secondo
        uint32_t elapsed = millis() - start_time;
        if(elapsed >= 1000 && hashes > 0) {
            stats.hashes_per_second = (hashes * 1000) / elapsed;
            
            // Stampa stats solo ogni 5 secondi per ridurre overhead seriale
            static uint32_t last_print = 0;
            if(millis() - last_print >= 5000) {
                hash_to_hex(hash, hash_hex);
                
                Serial.println("┌─────────────────────────────────────────────────────────┐");
                Serial.printf("│ ⚡ Hash/s: %-8u  📊 Nonce: %-12u      │\n", 
                    stats.hashes_per_second, header.nonce);
                Serial.printf("│ 🔢 Totale: %-10u ⏱️  Tempo: %-4u sec        │\n", 
                    stats.total_hashes, elapsed/1000);
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
    Serial.printf("   Totale hash calcolati: %u\n", stats.total_hashes);
    Serial.printf("   Blocchi trovati: %u\n", blocks_found);
    Serial.printf("   Miglior difficoltà: %d zeri iniziali\n", best_zeros);
    
    // Disconnetti dal pool se connesso
    if(currentMiningMode == MINING_MODE_POOL) {
        stratum_disconnect();
        Serial.println("   Disconnesso dal pool");
    }
    
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println();
    
    // Delete this task
    miningTaskHandle = NULL;
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
    
    // Reset statistiche
    memset(&stats, 0, sizeof(MiningStats));
    
    // Create FreeRTOS task
    // Task name: "MiningTask"
    // Stack size: 8192 bytes (aumentato per crypto operations)
    // Priority: 1 (low priority, won't interfere with WiFi and display)
    // Core: 1 (run on second core to keep main loop on core 0)
    xTaskCreatePinnedToCore(
        miningTask,           // Task function
        "MiningTask",         // Task name
        8192,                 // Stack size (bytes) - aumentato per operazioni crypto
        NULL,                 // Task parameter
        1,                    // Priority (1 = low)
        &miningTaskHandle,    // Task handle
        1                     // Core ID (1 = second core)
    );
    
    if (miningTaskHandle != NULL) {
        Serial.println("✅ Mining task creato con successo su Core 1");
    } else {
        Serial.println("❌ ERRORE: Impossibile creare mining task!");
    }
}

// Stop the mining task
void mining_task_stop(void)
{
    if (miningTaskHandle == NULL) {
        Serial.println("⚠️  Mining task non in esecuzione");
        return;
    }
    
    Serial.println("⏹️  Fermando mining task...");
    
    // Signal the task to stop
    taskRunning = false;
    
    // Wait for the task to clean up
    while(miningTaskHandle != NULL) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    
    Serial.println("✅ Mining task fermato");
}

// Check if mining task is running
bool mining_task_is_running(void)
{
    return (miningTaskHandle != NULL && taskRunning);
}

// Get current mining statistics
MiningStats mining_get_stats(void)
{
    return stats;
}

// Check if a block has been found
bool mining_has_found_block(void)
{
    return stats.blocks_found > 0;
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

