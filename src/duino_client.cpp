#include "duino_client.h"
#include <mbedtls/md.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Client state
static WiFiClient client;
static DuinoState currentState = DUCO_DISCONNECTED;
static String username = "";
static String rigId = "";
static String miningKey = "";
static String poolHost = "";
static int poolPort = 0;

// Statistics
static uint32_t acceptedShares = 0;
static uint32_t rejectedShares = 0;
static uint32_t totalHashes = 0;
static unsigned long lastHashTime = 0;
static uint32_t currentHashrate = 0;
static float currentDifficulty = 0;

// Protocol version
static const char* DUCO_DIFFICULTY = "ESP32";  // Use ESP32 difficulty level
static const char* MINER_BANNER = "Official ESP32 Miner";
static const char* DUCO_VERSION = "4.2";  // Version identifier

bool duino_fetch_pool(String &host, int &port) {
    HTTPClient http;
    http.begin(DUCO_POOL_PICKER_URL);
    http.addHeader("Accept", "*/*");
    http.setTimeout(5000);
    
    Serial.println("📡 Fetching best Duino-Coin pool...");
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.printf("   Pool picker response: %s\n", payload.c_str());
        
        // Parse JSON response
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            Serial.printf("   JSON parse error: %s\n", error.c_str());
            http.end();
            return false;
        }
        
        if (doc["success"] == true) {
            host = doc["ip"].as<String>();
            port = doc["port"].as<int>();
            String poolName = doc["name"].as<String>();
            
            Serial.printf("   ✅ Best pool: %s (%s:%d)\n", poolName.c_str(), host.c_str(), port);
            http.end();
            return true;
        }
    }
    
    Serial.printf("   ⚠️  Pool picker failed (HTTP %d), using fallback\n", httpCode);
    http.end();
    return false;
}

void duino_init(const char* user, const char* rigIdentifier, const char* key) {
    username = String(user);
    rigId = String(rigIdentifier);
    miningKey = String(key);
    currentState = DUCO_DISCONNECTED;
    acceptedShares = 0;
    rejectedShares = 0;
    totalHashes = 0;
    currentHashrate = 0;
    currentDifficulty = 0;
    
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║           DUINO-COIN CLIENT INITIALIZED               ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.printf("Username: %s\n", user);
    Serial.printf("Rig ID: %s\n", rigIdentifier);
    Serial.printf("Mining Key: %s\n", key && strlen(key) > 0 ? "***" : "None");
    Serial.println();
}

bool duino_connect(void) {
    // Check WiFi status first
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi not connected!");
        currentState = DUCO_ERROR;
        return false;
    }
    
    // Try to fetch best pool
    if (!duino_fetch_pool(poolHost, poolPort)) {
        // Fallback to default server
        poolHost = DUCO_SERVER_FALLBACK;
        poolPort = DUCO_PORT_FALLBACK;
        Serial.printf("   Using fallback: %s:%d\n", poolHost.c_str(), poolPort);
    }
    
    Serial.println("🪙 Connecting to Duino-Coin pool...");
    Serial.printf("   Server: %s:%d\n", poolHost.c_str(), poolPort);
    
    currentState = DUCO_CONNECTING;
    
    // Try to connect with timeout
    Serial.printf("   Connecting to %s:%d...\n", poolHost.c_str(), poolPort);
    if (!client.connect(poolHost.c_str(), poolPort)) {
        Serial.println("❌ Connection failed!");
        Serial.printf("   WiFi Status: %d\n", WiFi.status());
        Serial.printf("   Local IP: %s\n", WiFi.localIP().toString().c_str());
        currentState = DUCO_ERROR;
        return false;
    }
    
    Serial.println("   TCP connection established!");
    
    // Wait for server version
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 5000) {
            Serial.println("❌ Timeout waiting for server response");
            client.stop();
            currentState = DUCO_ERROR;
            return false;
        }
        delay(10);
    }
    
    String serverVersion = client.readStringUntil('\n');
    Serial.printf("   Server version: %s\n", serverVersion.c_str());
    
    currentState = DUCO_CONNECTED;
    Serial.println("✅ Connected to Duino-Coin pool!");
    Serial.println();
    
    return true;
}

void duino_disconnect(void) {
    if (client.connected()) {
        client.stop();
    }
    currentState = DUCO_DISCONNECTED;
    Serial.println("Disconnected from Duino-Coin pool");
}

bool duino_is_connected(void) {
    return client.connected() && (currentState == DUCO_CONNECTED || currentState == DUCO_MINING);
}

DuinoState duino_get_state(void) {
    return currentState;
}

// SHA-1 hash function for DUCO-S1
String duino_sha1(String data) {
    uint8_t hash[20];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA1;
    
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const unsigned char*)data.c_str(), data.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    
    // Convert to hex string
    String result = "";
    for (int i = 0; i < 20; i++) {
        if (hash[i] < 16) result += "0";
        result += String(hash[i], HEX);
    }
    
    return result;
}

// DUCO-S1 mining algorithm
int duino_duco_s1(String lastBlockHash, String expectedHash, int difficulty) {
    unsigned long startTime = millis();
    
    for (int ducos1res = 0; ducos1res < 100 * difficulty + 1; ducos1res++) {
        String hash = duino_sha1(lastBlockHash + String(ducos1res));
        totalHashes++;
        
        if (hash == expectedHash) {
            // Calculate hashrate
            unsigned long elapsed = millis() - startTime;
            if (elapsed > 0) {
                currentHashrate = (ducos1res * 1000) / elapsed;
            }
            return ducos1res;
        }
    }
    
    return -1; // Not found (shouldn't happen with valid job)
}

bool duino_mine_job(void) {
    if (!client.connected()) {
        Serial.println("❌ Not connected to pool");
        currentState = DUCO_ERROR;
        return false;
    }
    
    currentState = DUCO_MINING;
    
    // Request job with format: JOB,username,difficulty,mining_key
    String jobRequest = "JOB," + username + "," + String(DUCO_DIFFICULTY);
    if (miningKey.length() > 0) {
        jobRequest += "," + miningKey;
    }
    client.println(jobRequest);
    Serial.printf("   → Job request: %s\n", jobRequest.c_str());
    
    // Wait for job response
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 10000) {
            Serial.println("❌ Timeout waiting for job");
            currentState = DUCO_ERROR;
            return false;
        }
        delay(10);
    }
    
    String job = client.readStringUntil('\n');
    
    // Parse job: lastblockhash,expectedhash,difficulty
    int comma1 = job.indexOf(',');
    int comma2 = job.indexOf(',', comma1 + 1);
    
    if (comma1 == -1 || comma2 == -1) {
        Serial.println("❌ Invalid job format");
        currentState = DUCO_ERROR;
        return false;
    }
    
    String lastBlockHash = job.substring(0, comma1);
    String expectedHash = job.substring(comma1 + 1, comma2);
    int difficulty = job.substring(comma2 + 1).toInt();
    
    currentDifficulty = difficulty;
    
    Serial.printf("📦 New job - Difficulty: %d\n", difficulty);
    
    // Mine!
    unsigned long mineStart = millis();
    int result = duino_duco_s1(lastBlockHash, expectedHash, difficulty);
    unsigned long mineTime = millis() - mineStart;
    
    if (result == -1) {
        Serial.println("❌ Job solution not found (invalid job?)");
        rejectedShares++;
        return false;
    }
    
    // Submit result
    String submitStr = String(result) + "," + String(currentHashrate) + "," + 
                       MINER_BANNER + " " + DUCO_VERSION + "," + rigId;
    client.println(submitStr);
    
    // Wait for response
    timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 5000) {
            Serial.println("❌ Timeout waiting for submit response");
            currentState = DUCO_ERROR;
            return false;
        }
        delay(10);
    }
    
    String response = client.readStringUntil('\n');
    response.trim();
    
    if (response.indexOf("GOOD") >= 0 || response.indexOf("BLOCK") >= 0) {
        acceptedShares++;
        Serial.printf("✅ Share accepted! (%ums, %u H/s)\n", mineTime, currentHashrate);
        
        // Parse feedback if available (e.g., "GOOD,23.5")
        int commaPos = response.indexOf(',');
        if (commaPos > 0) {
            String feedback = response.substring(commaPos + 1);
            Serial.printf("   Feedback: %s DUCO\n", feedback.c_str());
        }
        
        currentState = DUCO_CONNECTED;
        return true;
    } else if (response.indexOf("BAD") >= 0) {
        rejectedShares++;
        Serial.printf("❌ Share rejected: %s\n", response.c_str());
        currentState = DUCO_CONNECTED;
        return false;
    } else {
        Serial.printf("⚠️  Unknown response: %s\n", response.c_str());
        currentState = DUCO_CONNECTED;
        return false;
    }
}

uint32_t duino_get_accepted_shares(void) {
    return acceptedShares;
}

uint32_t duino_get_rejected_shares(void) {
    return rejectedShares;
}

uint32_t duino_get_hashrate(void) {
    return currentHashrate;
}

float duino_get_difficulty(void) {
    return currentDifficulty;
}
