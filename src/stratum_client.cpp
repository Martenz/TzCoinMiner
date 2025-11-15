#include "stratum_client.h"
#include "wifi_config.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "esp_log.h"
#include <mbedtls/sha256.h>

static const char* TAG = "STRATUM";

static WiFiClient stratum_tcp_client;
static bool stratum_connected = false;
static String stratum_host;
static uint16_t stratum_port;
static String stratum_wallet;
static String stratum_worker;
static String stratum_password;

static uint32_t stratum_subscription_id = 0;
static String stratum_session_id;
static String stratum_extranonce1;
static int stratum_extranonce2_size = 0;

static String stratum_job_id;
static String stratum_prev_hash;
static String stratum_coinb1;
static String stratum_coinb2;
static std::vector<String> stratum_merkle_branch;
static String stratum_version;
static String stratum_nbits;
static String stratum_ntime;
static bool stratum_clean_jobs = false;

static uint32_t stratum_difficulty = 0;
static bool difficulty_was_set = false;

// Keepalive per mantenere connessione attiva
static unsigned long last_keepalive_time = 0;
static unsigned long last_activity_time = 0;
static const unsigned long KEEPALIVE_INTERVAL = 60000;  // 60 secondi
static const unsigned long CONNECTION_TIMEOUT = 300000;  // 5 minuti senza attività = timeout

// Callback per mining task
static stratum_job_callback_t job_callback = nullptr;
static stratum_share_response_callback_t share_response_callback = nullptr;

// Helper: converti hex string a bytes
static void hex_to_bytes(const String& hex, uint8_t* bytes, size_t len) {
    for (size_t i = 0; i < len; i++) {
        sscanf(hex.substring(i * 2, i * 2 + 2).c_str(), "%2hhx", &bytes[i]);
    }
}

// Helper: converti bytes a hex string
static String bytes_to_hex(const uint8_t* bytes, size_t len) {
    String hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        sprintf(buf, "%02x", bytes[i]);
        hex += buf;
    }
    return hex;
}

// Invia messaggio JSON-RPC
static bool stratum_send_message(JsonDocument& doc) {
    String msg;
    serializeJson(doc, msg);
    msg += "\n";
    
    ESP_LOGI(TAG, "Sending: %s", msg.c_str());
    
    if (!stratum_tcp_client.connected()) {
        ESP_LOGE(TAG, "Not connected");
        return false;
    }
    
    size_t sent = stratum_tcp_client.print(msg);
    return sent == msg.length();
}

// Leggi e processa risposta
static bool stratum_read_response(JsonDocument& doc) {
    if (!stratum_tcp_client.available()) {
        return false;
    }
    
    String line = stratum_tcp_client.readStringUntil('\n');
    line.trim();
    
    if (line.length() == 0) {
        return false;
    }
    
    // Log RAW per debug
    Serial.print("[STRATUM RX] ");
    Serial.println(line);
    ESP_LOGI(TAG, "Received: %s", line.c_str());
    
    DeserializationError error = deserializeJson(doc, line);
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return false;
    }
    
    return true;
}

// Processa mining.notify (nuovo job)
static void stratum_process_notify(JsonArray params) {
    if (params.size() < 8) {
        ESP_LOGE(TAG, "Invalid notify params");
        return;
    }
    
    stratum_job_id = params[0].as<String>();
    stratum_prev_hash = params[1].as<String>();
    stratum_coinb1 = params[2].as<String>();
    stratum_coinb2 = params[3].as<String>();
    
    stratum_merkle_branch.clear();
    JsonArray merkle = params[4].as<JsonArray>();
    for (JsonVariant v : merkle) {
        stratum_merkle_branch.push_back(v.as<String>());
    }
    
    stratum_version = params[5].as<String>();
    stratum_nbits = params[6].as<String>();
    stratum_ntime = params[7].as<String>();
    stratum_clean_jobs = params[8].as<bool>();
    
    ESP_LOGI(TAG, "New job: %s", stratum_job_id.c_str());
    
    // 🔥 Se il pool non ha mai inviato mining.set_difficulty, 
    // assumiamo che stia usando difficulty 1 (minima possibile)
    // Questo check avviene su OGNI job per gestire pool che non inviano mai difficulty
    if (stratum_difficulty == 0) {
        extern WifiConfig config;
        // Se il pool non specifica, proviamo a usare la minDifficulty configurata
        // oppure 1 come default (Bitcoin difficulty baseline)
        uint32_t default_diff = (config.minDifficulty > 0) ? config.minDifficulty : 1;
        stratum_difficulty = default_diff;
        difficulty_was_set = true;
        Serial.printf("⚠️  Pool non ha inviato difficulty - usando default: %u\n", stratum_difficulty);
        ESP_LOGW(TAG, "Pool didn't send difficulty - using default: %u", stratum_difficulty);
    }
    
    // Notifica il mining task se c'è un callback
    if (job_callback) {
        stratum_job_t job;
        job.job_id = stratum_job_id;
        job.prev_hash = stratum_prev_hash;
        job.coinb1 = stratum_coinb1;
        job.coinb2 = stratum_coinb2;
        job.merkle_branch = stratum_merkle_branch;
        job.version = stratum_version;
        job.nbits = stratum_nbits;
        job.ntime = stratum_ntime;
        job.clean_jobs = stratum_clean_jobs;
        job.extranonce1 = stratum_extranonce1;
        job.extranonce2_size = stratum_extranonce2_size;
        
        job_callback(&job);
    }
}

// Processa mining.set_difficulty
static void stratum_process_difficulty(JsonArray params) {
    if (params.size() < 1) {
        ESP_LOGE(TAG, "Invalid difficulty params");
        return;
    }
    
    float diff = params[0].as<float>();
    stratum_difficulty = (uint32_t)diff;
    difficulty_was_set = true;
    
    Serial.printf("🎚️  Pool set difficulty to: %u\n", stratum_difficulty);
    ESP_LOGI(TAG, "Pool set difficulty to: %u", stratum_difficulty);
}

void stratum_init(const char* pool_url, uint16_t port, const char* wallet_address, const char* worker_name, const char* password) {
    stratum_host = pool_url;
    stratum_port = port;
    stratum_wallet = wallet_address;
    stratum_worker = worker_name ? worker_name : "esp32";
    stratum_password = password ? password : "x";
    
    stratum_connected = false;
    stratum_subscription_id = 0;
    
    ESP_LOGI(TAG, "Initialized with pool: %s:%d", pool_url, port);
}

bool stratum_connect() {
    if (stratum_tcp_client.connected()) {
        stratum_tcp_client.stop();
    }
    
    ESP_LOGI(TAG, "Connecting to %s:%d...", stratum_host.c_str(), stratum_port);
    
    if (!stratum_tcp_client.connect(stratum_host.c_str(), stratum_port)) {
        ESP_LOGE(TAG, "Connection failed");
        return false;
    }
    
    ESP_LOGI(TAG, "Connected to pool");
    stratum_connected = true;
    
    // Inizializza timestamp per keepalive
    last_keepalive_time = millis();
    last_activity_time = millis();
    
    // Invia mining.subscribe (il suggest_difficulty si manda DOPO authorize)
    JsonDocument doc;
    doc["id"] = 1;
    doc["method"] = "mining.subscribe";
    JsonArray params = doc["params"].to<JsonArray>();
    params.add("TzBtcMiner/1.0");
    
    if (!stratum_send_message(doc)) {
        stratum_tcp_client.stop();
        stratum_connected = false;
        return false;
    }
    
    return true;
}

void stratum_disconnect() {
    if (stratum_tcp_client.connected()) {
        stratum_tcp_client.stop();
    }
    stratum_connected = false;
    stratum_difficulty = 0;
    difficulty_was_set = false;
    ESP_LOGI(TAG, "Disconnected");
}

bool stratum_is_connected() {
    return stratum_connected && stratum_tcp_client.connected();
}

void stratum_loop() {
    if (!stratum_is_connected()) {
        return;
    }
    
    unsigned long now = millis();
    
    // Verifica timeout connessione (nessuna attività da troppo tempo)
    if (now - last_activity_time > CONNECTION_TIMEOUT) {
        ESP_LOGW(TAG, "Connection timeout - no activity for %lu seconds", CONNECTION_TIMEOUT / 1000);
        Serial.println("⚠️  Timeout connessione - riconnessione...");
        stratum_disconnect();
        return;
    }
    
    // Invia keepalive se necessario (ogni 60 secondi)
    if (now - last_keepalive_time > KEEPALIVE_INTERVAL) {
        // Invia un messaggio mining.ping per mantenere la connessione attiva
        JsonDocument keepalive_doc;
        keepalive_doc["id"] = 999;  // ID speciale per keepalive
        keepalive_doc["method"] = "mining.ping";
        keepalive_doc["params"].to<JsonArray>();  // Array vuoto
        
        if (stratum_send_message(keepalive_doc)) {
            ESP_LOGI(TAG, "Keepalive sent");
            last_keepalive_time = now;
        } else {
            ESP_LOGW(TAG, "Keepalive failed - connection may be dead");
            stratum_disconnect();
            return;
        }
    }
    
    // Leggi messaggi dal pool (max 5 per chiamata per non bloccare mining)
    int messages_read = 0;
    const int MAX_MESSAGES_PER_CALL = 5;
    while (stratum_tcp_client.available() && messages_read < MAX_MESSAGES_PER_CALL) {
        messages_read++;
        JsonDocument doc;
        if (!stratum_read_response(doc)) {
            continue;  // Messaggio invalido, prova il prossimo
        }
        
        // Aggiorna timestamp ultima attività (abbiamo ricevuto qualcosa)
        last_activity_time = now;
        
        // Risposta a una nostra richiesta
        if (!doc["id"].isNull()) {
            int id = doc["id"].as<int>();
            // Serial.printf("[STRATUM] Response ID: %d\n", id);
            // ESP_LOGI(TAG, "Received response with ID: %d", id);
            
            // Risposta a keepalive (ignora)
            if (id == 999) {
                // ESP_LOGI(TAG, "Keepalive response received");
                continue;  // Processa prossimo messaggio
            }
            
            // Risposta a mining.subscribe
            if (id == 1) {
                if (!doc["error"].isNull()) {
                    ESP_LOGE(TAG, "Subscribe error");
                    stratum_disconnect();
                    return;  // Errore grave, esci
                }
                
                JsonArray result = doc["result"].as<JsonArray>();
                if (result.size() >= 2) {
                    stratum_extranonce1 = result[1].as<String>();
                    stratum_extranonce2_size = result[2].as<int>();
                    
                    ESP_LOGI(TAG, "Subscribed - extranonce1: %s, extranonce2_size: %d", 
                             stratum_extranonce1.c_str(), stratum_extranonce2_size);
                    
                    // Invia mining.authorize
                    JsonDocument auth_doc;
                    auth_doc["id"] = 2;
                    auth_doc["method"] = "mining.authorize";
                    JsonArray auth_params = auth_doc["params"].to<JsonArray>();
                    auth_params.add(stratum_wallet + "." + stratum_worker);
                    auth_params.add(stratum_password);
                    
                    stratum_send_message(auth_doc);
                }
            }
            // Risposta a mining.authorize
            else if (id == 2) {
                if (!doc["error"].isNull()) {
                    ESP_LOGE(TAG, "Authorization failed");
                    stratum_disconnect();
                    return;  // Errore grave, esci
                }
                
                bool authorized = doc["result"].as<bool>();
                if (authorized) {
                    Serial.println("[STRATUM] Authorized successfully!");
                    ESP_LOGI(TAG, "Authorized successfully");
                    
                    // Invia mining.suggest_difficulty per ESP32
                    extern WifiConfig config;
                    uint32_t suggest_diff = (config.minDifficulty > 0) ? config.minDifficulty : 64;
                    
                    Serial.printf("[STRATUM] Sending mining.suggest_difficulty: %u\n", suggest_diff);
                    ESP_LOGI(TAG, "Sending mining.suggest_difficulty with value: %u", suggest_diff);
                    
                    JsonDocument suggest_doc;
                    suggest_doc["id"] = 99;  // ID univoco (3 è usato per mining.submit)
                    suggest_doc["method"] = "mining.suggest_difficulty";
                    JsonArray suggest_params = suggest_doc["params"].to<JsonArray>();
                    suggest_params.add(suggest_diff);
                    
                    stratum_send_message(suggest_doc);
                    ESP_LOGI(TAG, "Suggested difficulty: %u", suggest_diff);
                } else {
                    ESP_LOGE(TAG, "Not authorized");
                    stratum_disconnect();
                    return;  // Non autorizzato, esci
                }
            }
            // Risposta a mining.submit
            else if (id == 3) {
                if (!doc["error"].isNull()) {
                    Serial.println("❌ SHARE REJECTED BY POOL!");
                    JsonVariant error = doc["error"];
                    String error_msg;
                    if (error.is<String>()) {
                        error_msg = error.as<String>();
                    } else if (error.is<JsonArray>()) {
                        JsonArray err_arr = error.as<JsonArray>();
                        if (err_arr.size() > 1) {
                            error_msg = err_arr[1].as<String>();
                        }
                    }
                    Serial.printf("   Error: %s\n", error_msg.c_str());
                    ESP_LOGW(TAG, "Share rejected: %s", error_msg.c_str());
                    // Notifica mining_task
                    if (share_response_callback) {
                        share_response_callback(false);  // rejected
                    }
                } else {
                    bool accepted = doc["result"].as<bool>();
                    if (accepted) {
                        Serial.println("✅ SHARE ACCEPTED BY POOL!");
                        ESP_LOGI(TAG, "Share accepted!");
                    } else {
                        Serial.println("⚠️  SHARE NOT ACCEPTED BY POOL!");
                        ESP_LOGW(TAG, "Share not accepted");
                    }
                    // Notifica mining_task
                    if (share_response_callback) {
                        share_response_callback(accepted);
                    }
                }
            }
        }
        // Notifica dal pool
        else if (!doc["method"].isNull()) {
            String method = doc["method"].as<String>();
            JsonArray params = doc["params"].as<JsonArray>();
            
            if (method == "mining.notify") {
                stratum_process_notify(params);
            }
            else if (method == "mining.set_difficulty") {
                stratum_process_difficulty(params);
            }
        }
    }  // Fine while (stratum_tcp_client.available())
}

bool stratum_submit_share(const char* job_id, const char* extranonce2, const char* ntime, const char* nonce) {
    // Verifica connessione TCP prima di tentare submit
    if (!stratum_tcp_client.connected()) {
        ESP_LOGE(TAG, "TCP connection lost before submit");
        Serial.println("❌ TCP connection lost!");
        stratum_connected = false;
        return false;
    }
    
    if (!stratum_is_connected()) {
        ESP_LOGE(TAG, "Not connected");
        Serial.println("❌ Stratum not connected!");
        return false;
    }
    
    Serial.println("📡 Submitting share to pool:");
    Serial.printf("   Job ID: %s\n", job_id);
    Serial.printf("   Extranonce2: %s\n", extranonce2);
    Serial.printf("   Ntime: %s\n", ntime);
    Serial.printf("   Nonce: %s\n", nonce);
    
    JsonDocument doc;
    doc["id"] = 3;
    doc["method"] = "mining.submit";
    JsonArray params = doc["params"].to<JsonArray>();
    params.add(stratum_wallet + "." + stratum_worker);
    params.add(job_id);
    params.add(extranonce2);
    params.add(ntime);
    params.add(nonce);
    
    // Aggiorna timestamp attività quando inviamo share
    last_activity_time = millis();
    
    bool sent = stratum_send_message(doc);
    if (sent) {
        Serial.println("✅ Share message sent to pool, waiting for response...");
    } else {
        Serial.println("❌ Failed to send share message!");
    }
    
    return sent;
}

void stratum_set_job_callback(stratum_job_callback_t callback) {
    job_callback = callback;
}

void stratum_set_share_response_callback(stratum_share_response_callback_t callback) {
    share_response_callback = callback;
}

uint32_t stratum_get_difficulty() {
    return stratum_difficulty;
}

stratum_job_t stratum_get_current_job() {
    stratum_job_t job;
    job.job_id = stratum_job_id;
    job.prev_hash = stratum_prev_hash;
    job.coinb1 = stratum_coinb1;
    job.coinb2 = stratum_coinb2;
    job.merkle_branch = stratum_merkle_branch;
    job.version = stratum_version;
    job.nbits = stratum_nbits;
    job.ntime = stratum_ntime;
    job.clean_jobs = stratum_clean_jobs;
    job.extranonce1 = stratum_extranonce1;
    job.extranonce2_size = stratum_extranonce2_size;
    return job;
}
