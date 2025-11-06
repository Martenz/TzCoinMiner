#include "stratum_client.h"
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

// Callback per mining task
static stratum_job_callback_t job_callback = nullptr;

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
        return;
    }
    
    float diff = params[0].as<float>();
    stratum_difficulty = (uint32_t)diff;
    
    ESP_LOGI(TAG, "Difficulty set to: %u", stratum_difficulty);
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
    
    // Invia mining.subscribe
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
    ESP_LOGI(TAG, "Disconnected");
}

bool stratum_is_connected() {
    return stratum_connected && stratum_tcp_client.connected();
}

void stratum_loop() {
    if (!stratum_is_connected()) {
        return;
    }
    
    // Leggi eventuali messaggi dal pool
    JsonDocument doc;
    if (!stratum_read_response(doc)) {
        return;
    }
    
    // Risposta a una nostra richiesta
    if (!doc["id"].isNull()) {
        int id = doc["id"].as<int>();
        
        // Risposta a mining.subscribe
        if (id == 1) {
            if (!doc["error"].isNull()) {
                ESP_LOGE(TAG, "Subscribe error");
                stratum_disconnect();
                return;
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
                return;
            }
            
            bool authorized = doc["result"].as<bool>();
            if (authorized) {
                ESP_LOGI(TAG, "Authorized successfully");
            } else {
                ESP_LOGE(TAG, "Not authorized");
                stratum_disconnect();
            }
        }
        // Risposta a mining.submit
        else if (id == 3) {
            if (!doc["error"].isNull()) {
                ESP_LOGW(TAG, "Share rejected");
            } else {
                bool accepted = doc["result"].as<bool>();
                if (accepted) {
                    ESP_LOGI(TAG, "Share accepted!");
                } else {
                    ESP_LOGW(TAG, "Share not accepted");
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
}

bool stratum_submit_share(const char* job_id, const char* extranonce2, const char* ntime, const char* nonce) {
    if (!stratum_is_connected()) {
        ESP_LOGE(TAG, "Not connected");
        return false;
    }
    
    JsonDocument doc;
    doc["id"] = 3;
    doc["method"] = "mining.submit";
    JsonArray params = doc["params"].to<JsonArray>();
    params.add(stratum_wallet + "." + stratum_worker);
    params.add(job_id);
    params.add(extranonce2);
    params.add(ntime);
    params.add(nonce);
    
    return stratum_send_message(doc);
}

void stratum_set_job_callback(stratum_job_callback_t callback) {
    job_callback = callback;
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
