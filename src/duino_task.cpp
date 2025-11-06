#include "duino_task.h"
#include "duino_client.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// FreeRTOS task handle
static TaskHandle_t duinoTaskHandle = NULL;

// Task running flag
static volatile bool taskRunning = false;

// Mining statistics
static DuinoStats stats = {0};

// Credentials
static String username = "";
static String rigId = "ESP32";
static String miningKey = "";

void duinoTask(void* parameter)
{
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║        DUINO-COIN MINING TASK STARTED                 ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.println();
    
    taskRunning = true;
    
    // Initialize client
    duino_init(username.c_str(), rigId.c_str(), miningKey.c_str());
    
    // Try to connect to pool with retries
    int connectAttempts = 0;
    const int maxAttempts = 5;
    bool connected = false;
    
    while (!connected && connectAttempts < maxAttempts && taskRunning) {
        connectAttempts++;
        Serial.printf("Connection attempt %d/%d...\n", connectAttempts, maxAttempts);
        
        if (duino_connect()) {
            connected = true;
            break;
        }
        
        if (connectAttempts < maxAttempts) {
            Serial.println("Retrying in 3 seconds...");
            delay(3000);
        }
    }
    
    if (!connected) {
        Serial.println("❌ Failed to connect to Duino-Coin pool after multiple attempts!");
        Serial.println("   Please check:");
        Serial.println("   1. WiFi connection is active");
        Serial.println("   2. Internet access is available");
        Serial.println("   3. server.duinocoin.com is reachable");
        taskRunning = false;
        vTaskDelete(NULL);
        return;
    }
    
    Serial.println("🚀 Starting mining loop...");
    Serial.println();
    
    unsigned long lastStatsUpdate = 0;
    
    // Mining loop
    while (taskRunning) {
        // Mine one job
        bool success = duino_mine_job();
        
        // Update statistics
        stats.hashes_per_second = duino_get_hashrate();
        stats.shares_accepted = duino_get_accepted_shares();
        stats.shares_rejected = duino_get_rejected_shares();
        stats.difficulty = duino_get_difficulty();
        
        // Print stats every 10 seconds
        if (millis() - lastStatsUpdate >= 10000) {
            lastStatsUpdate = millis();
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            Serial.println("📊 DUINO-COIN MINING STATISTICS");
            Serial.printf("   Hash Rate: %u H/s\n", stats.hashes_per_second);
            Serial.printf("   Difficulty: %.1f\n", stats.difficulty);
            Serial.printf("   Shares: %u accepted / %u rejected\n", 
                         stats.shares_accepted, stats.shares_rejected);
            if (stats.shares_accepted + stats.shares_rejected > 0) {
                float successRate = (stats.shares_accepted * 100.0) / 
                                   (stats.shares_accepted + stats.shares_rejected);
                Serial.printf("   Success Rate: %.1f%%\n", successRate);
            }
            Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
            Serial.println();
        }
        
        // Check connection
        if (!duino_is_connected()) {
            Serial.println("⚠️  Lost connection to pool, reconnecting...");
            delay(5000);
            if (!duino_connect()) {
                Serial.println("❌ Reconnection failed, stopping task");
                break;
            }
        }
        
        // Small delay between jobs
        delay(100);
    }
    
    // Cleanup
    duino_disconnect();
    taskRunning = false;
    
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║        DUINO-COIN MINING TASK STOPPED                 ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.println();
    
    vTaskDelete(NULL);
}

void duino_set_credentials(const char* user, const char* rig, const char* key) {
    username = String(user);
    rigId = String(rig);
    miningKey = String(key);
    
    Serial.println("╔════════════════════════════════════════════════════════╗");
    Serial.println("║      DUINO-COIN CREDENTIALS CONFIGURED                ║");
    Serial.println("╚════════════════════════════════════════════════════════╝");
    Serial.printf("Username: %s\n", user);
    Serial.printf("Rig ID: %s\n", rig);
    if (strlen(key) > 0) {
        Serial.printf("Mining Key: %s\n", key);
    }
    Serial.println();
}

void duino_task_start(void) {
    if (taskRunning) {
        Serial.println("⚠️  Duino-Coin mining task already running!");
        return;
    }
    
    // Reset statistics
    memset(&stats, 0, sizeof(stats));
    
    // Create task on core 1
    xTaskCreatePinnedToCore(
        duinoTask,           // Task function
        "DuinoTask",         // Task name
        8192,                // Stack size (8KB)
        NULL,                // Parameters
        1,                   // Priority
        &duinoTaskHandle,    // Task handle
        1                    // Core 1
    );
    
    Serial.println("✅ Duino-Coin mining task started on Core 1");
}

void duino_task_stop(void) {
    if (!taskRunning) {
        Serial.println("⚠️  Duino-Coin mining task not running!");
        return;
    }
    
    taskRunning = false;
    
    // Wait for task to finish
    unsigned long timeout = millis();
    while (duinoTaskHandle != NULL && (millis() - timeout < 5000)) {
        delay(100);
    }
    
    if (duinoTaskHandle != NULL) {
        vTaskDelete(duinoTaskHandle);
        duinoTaskHandle = NULL;
    }
    
    Serial.println("✅ Duino-Coin mining task stopped");
}

bool duino_task_is_running(void) {
    return taskRunning;
}

DuinoStats duino_get_stats(void) {
    return stats;
}

bool duino_has_found_share(void) {
    return stats.shares_accepted > 0;
}
