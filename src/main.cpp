#include <Arduino.h>
#include <WiFi.h>
#include "display.h"
#include "wifi_config.h"
#include "mining_task.h"
#include "duino_task.h"

// Button pins (from pins_config.h)
#define PIN_BUTTON_1 0
#define PIN_BUTTON_2 21

// Button debouncing
#define DEBOUNCE_DELAY 50
#define LONG_PRESS_DELAY 1000  // 1 second for long press

// Current page state
Page currentPage = PAGE_LOGO;
bool wifiEnabled = false;
bool miningActive = false;
bool isApMode = false;  // Track if AP mode is active
bool isSoloMode = false;  // Track if solo mining mode is configured
bool isDuinoCoinMode = false;  // Track if Duino-Coin mode is configured

// Button state tracking
struct ButtonState {
    bool lastReading;
    bool pressed;
    unsigned long lastChangeTime;
    unsigned long pressStartTime;  // When button was first pressed
    bool longPressHandled;  // Prevent multiple long press triggers
};

ButtonState button1State = {HIGH, false, 0, 0, false};
ButtonState button2State = {HIGH, false, 0, 0, false};

void setup()
{
    Serial.begin(115200);
    delay(100);
    
    Serial.println("\n\nStarting TzBtcMiner Application...");
    
    // Initialize buttons
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
    
    Serial.printf("Button 1 (GPIO %d) initial state: %s\n", PIN_BUTTON_1, digitalRead(PIN_BUTTON_1) ? "HIGH" : "LOW");
    Serial.printf("Button 2 (GPIO %d) initial state: %s\n", PIN_BUTTON_2, digitalRead(PIN_BUTTON_2) ? "HIGH" : "LOW");
    
    // Initialize the display hardware
    display_init();
    
    // Initialize WiFi system
    wifi_init();
    
    // Configure mining based on saved settings
    WifiConfig config;
    if (wifi_load_config(config) && config.isConfigured) {
        isSoloMode = config.soloMode;  // Save solo mode state
        isDuinoCoinMode = config.useDuinoCoin;  // Save Duino-Coin mode state
        
        Serial.printf("Loaded config: DuinoCoin=%d, BCH=%d, Solo=%d\n", 
                     config.useDuinoCoin, config.useBitcoinCash, config.soloMode);
        
        if (config.useDuinoCoin) {
            // Configura Duino-Coin mining
            Serial.printf("Configuring Duino-Coin mining for user: %s\n", config.ducoUsername);
            duino_set_credentials(config.ducoUsername, "ESP32_TzMiner", config.ducoMiningKey);
            
            // Auto start Duino-Coin mining if enabled and WiFi is connected
            if (config.autoStartMining && wifi_get_status() == WIFI_CONNECTED) {
                Serial.println("Auto Start Mining enabled - starting Duino-Coin task...");
                miningActive = true;
                duino_task_start();
            } else if (config.autoStartMining) {
                Serial.println("Auto Start Mining enabled but WiFi not connected - mining will not start");
            }
        } else {
            // Configura Bitcoin/BCH mining
            if (config.soloMode) {
                // Configura nodo Bitcoin RPC per Solo mining
                Serial.println("Solo mining mode - configuring Bitcoin RPC node...");
                mining_set_bitcoin_node(config.rpcHost, config.rpcPort, 
                                       config.rpcUser, config.rpcPassword);
                mining_set_mode(MINING_MODE_SOLO);
            } else {
                // Configura pool mining
                if (config.useBitcoinCash) {
                    Serial.println("Configuring Bitcoin Cash pool mining...");
                    // BCH solo pool - eu2.solopool.org:8002
                    const char* wallet = (strlen(config.bchWallet) > 0) ? config.bchWallet : config.btcWallet;
                    mining_set_pool("eu2.solopool.org", 8002, wallet, 
                                  "esp32miner", "x");
                } else {
                    Serial.println("Configuring Bitcoin pool mining...");
                    mining_set_pool(config.poolUrl, config.poolPort, config.btcWallet, 
                                  "esp32miner", config.poolPassword);
                }
                mining_set_mode(MINING_MODE_POOL);
            }
            
            // Auto start Bitcoin/BCH mining if enabled and WiFi is connected
            if (config.autoStartMining && wifi_get_status() == WIFI_CONNECTED) {
                Serial.println("Auto Start Mining enabled - starting mining task...");
                miningActive = true;
                mining_task_start();
            } else if (config.autoStartMining) {
                Serial.println("Auto Start Mining enabled but WiFi not connected - mining will not start");
            }
        }
    } else {
        // Nessuna configurazione, usa modalità educational
        Serial.println("No configuration found, using Bitcoin educational mode");
        mining_set_mode(MINING_MODE_EDUCATIONAL);
        isSoloMode = false;  // Default to pool mode display
        isDuinoCoinMode = false;
    }
    
    // Show startup screen (logo page)
    bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
    String timeString = wifi_get_time_string();
    display_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
    
    Serial.println("System initialization complete");
    Serial.println("Ready for operations");
    Serial.println("\n=== BUTTON CONTROLS ===");
    Serial.println("Button 1 (BOOT): Switch pages");
    Serial.println("Button 2 (GPIO21): Page actions");
    Serial.println("=======================\n");
}

void handleButton1() 
{
    // Button 1: Switch pages (rotate through pages)
    currentPage = (Page)((currentPage + 1) % PAGE_COUNT);
    
    // Reset animation when changing pages
    display_reset_animation();
    
    // Reset WiFi state when leaving setup page
    if (currentPage != PAGE_SETUP) {
        wifiEnabled = false;
        if (isApMode) {
            wifi_stop_ap();
            isApMode = false;
        }
    }
    
    // Get current WiFi status and time
    bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
    String timeString = wifi_get_time_string();
    
    // Show the new page with appropriate state
    switch(currentPage) {
        case PAGE_LOGO:
            display_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
            break;
        case PAGE_MINING:
            display_page_mining(miningActive, wifiConnected, timeString.c_str(), isSoloMode, isDuinoCoinMode);
            break;
        case PAGE_SETUP:
            display_page_setup(wifiEnabled, wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
            break;
    }
    
    Serial.printf("Switched to page %d\n", currentPage + 1);
}

void handleButton2LongPress()
{
    // Toggle between Solo and Pool mode (session only, not persistent)
    // This only works on PAGE_MINING and only for Bitcoin mode
    
    if (isDuinoCoinMode) {
        Serial.println("Long press ignored - Duino-Coin mode has no Solo/Pool distinction");
        return;
    }
    
    Serial.println("=== TOGGLE MINING MODE (Session Only) ===");
    
    bool wasActive = miningActive;
    
    // Stop mining if active
    if (miningActive) {
        Serial.println("Stopping current mining task...");
        mining_task_stop();
        miningActive = false;
        delay(100);  // Small delay to ensure task stops
    }
    
    // Toggle mode
    isSoloMode = !isSoloMode;
    
    // Load saved configuration to get pool/RPC settings
    WifiConfig config;
    wifi_load_config(config);
    
    if (isSoloMode) {
        // Switch to SOLO mode
        Serial.println("Switching to SOLO MINING mode (session only)");
        mining_set_bitcoin_node(config.rpcHost, config.rpcPort, 
                               config.rpcUser, config.rpcPassword);
        mining_set_mode(MINING_MODE_SOLO);
    } else {
        // Switch to POOL mode
        Serial.println("Switching to POOL MINING mode (session only)");
        if (config.useBitcoinCash) {
            Serial.println("Using Bitcoin Cash pool (eu2.solopool.org)...");
            const char* wallet = (strlen(config.bchWallet) > 0) ? config.bchWallet : config.btcWallet;
            mining_set_pool("eu2.solopool.org", 8002, wallet, 
                          "esp32miner", "x");
        } else {
            Serial.println("Using Bitcoin pool...");
            mining_set_pool(config.poolUrl, config.poolPort, config.btcWallet, 
                          "esp32miner", config.poolPassword);
        }
        mining_set_mode(MINING_MODE_POOL);
    }
    
    // Restart mining if it was active
    if (wasActive) {
        Serial.println("Restarting mining task...");
        miningActive = true;
        mining_task_start();
    }
    
    // Update display
    bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
    String timeString = wifi_get_time_string();
    display_page_mining(miningActive, wifiConnected, timeString.c_str(), isSoloMode, isDuinoCoinMode);
    
    Serial.printf("Mode switched to: %s\n", isSoloMode ? "SOLO" : "POOL");
    Serial.println("NOTE: This change is temporary and will reset on reboot");
}

void handleButton2() 
{
    // Get current WiFi status and time
    bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
    String timeString = wifi_get_time_string();
    
    // Button 2: Perform page-specific actions
    switch(currentPage) {
        case PAGE_LOGO:
            // Refresh logo colors
            display_refresh_logo_colors();
            display_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
            Serial.println("Action: Refreshed logo colors");
            break;
            
        case PAGE_MINING:
            // Check if WiFi is connected before allowing mining toggle
            if (!wifiConnected) {
                // No WiFi - don't toggle mining, just update display to show "no wifi" status
                Serial.println("Action: Cannot start mining - No WiFi connection");
                display_page_mining(false, wifiConnected, timeString.c_str(), isSoloMode, isDuinoCoinMode);
                break;
            }
            
            // Toggle mining state (only if WiFi is connected)
            miningActive = !miningActive;
            
            if (miningActive) {
                // Start appropriate mining task based on coin mode
                if (isDuinoCoinMode) {
                    Serial.println("Starting Duino-Coin mining task...");
                    duino_task_start();
                } else {
                    Serial.println("Starting Bitcoin mining task...");
                    mining_task_start();
                }
            } else {
                // Stop appropriate mining task
                if (isDuinoCoinMode) {
                    Serial.println("Stopping Duino-Coin mining task...");
                    duino_task_stop();
                } else {
                    Serial.println("Stopping Bitcoin mining task...");
                    mining_task_stop();
                }
            }
            
            display_page_mining(miningActive, wifiConnected, timeString.c_str(), isSoloMode, isDuinoCoinMode);
            Serial.printf("Action: %s Mining %s\n", 
                         isDuinoCoinMode ? "Duino-Coin" : "Bitcoin",
                         miningActive ? "STARTED" : "STOPPED");
            break;
            
        case PAGE_SETUP:
            // Toggle WiFi configuration
            wifiEnabled = !wifiEnabled;
            
            if (wifiEnabled) {
                // Start WiFi AP mode
                Serial.println("Starting WiFi AP mode...");
                wifi_start_ap();
                isApMode = true;
            } else {
                // Stop WiFi AP mode and try to reconnect to saved WiFi
                Serial.println("Stopping WiFi AP mode...");
                wifi_stop_ap();
                isApMode = false;
                
                // Try to reconnect to saved WiFi
                Serial.println("Attempting to reconnect to saved WiFi...");
                if (wifi_connect_saved()) {
                    Serial.println("Reconnected to WiFi successfully!");
                } else {
                    Serial.println("Failed to reconnect to WiFi");
                }
            }
            
            // Get updated WiFi status after the operation
            wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
            timeString = wifi_get_time_string();
            display_page_setup(wifiEnabled, wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
            Serial.printf("Action: WiFi %s\n", wifiEnabled ? "ON (AP Mode)" : "OFF");
            break;
    }
}

// Returns: 0 = no press, 1 = short press, 2 = long press
int readButton(int pin, ButtonState &state) {
    bool reading = digitalRead(pin);
    unsigned long currentTime = millis();
    
    // Detect state change
    if (reading != state.lastReading) {
        state.lastChangeTime = currentTime;
        state.lastReading = reading;
    }
    
    // Check if state has been stable for debounce time
    if ((currentTime - state.lastChangeTime) > DEBOUNCE_DELAY) {
        // Check for button press (transition from HIGH to LOW)
        if (reading == LOW && !state.pressed) {
            state.pressed = true;
            state.pressStartTime = currentTime;
            state.longPressHandled = false;
            return 0; // Button just pressed, wait to see if it's long or short
        } 
        else if (reading == LOW && state.pressed && !state.longPressHandled) {
            // Button is still held down, check if it's a long press
            if ((currentTime - state.pressStartTime) >= LONG_PRESS_DELAY) {
                state.longPressHandled = true;
                return 2; // Long press detected
            }
        }
        else if (reading == HIGH && state.pressed) {
            // Button released
            state.pressed = false;
            // Check if it was a short press (released before long press threshold)
            if (!state.longPressHandled) {
                return 1; // Short press detected
            }
        }
    }
    
    return 0; // No press detected
}

void checkButtons()
{
    // Check Button 1
    int button1Press = readButton(PIN_BUTTON_1, button1State);
    if (button1Press == 1) {
        Serial.println(">>> Button 1 short pressed!");
        handleButton1();
    }
    
    // Check Button 2
    int button2Press = readButton(PIN_BUTTON_2, button2State);
    if (button2Press == 1) {
        Serial.println(">>> Button 2 short pressed!");
        handleButton2();
    } else if (button2Press == 2 && currentPage == PAGE_MINING) {
        Serial.println(">>> Button 2 LONG pressed on Mining page!");
        handleButton2LongPress();
    }
}

void loop()
{
    unsigned long currentMillis = millis();
    
    // Check for button presses
    checkButtons();
    
    // Handle web server requests if in AP mode
    if (isApMode) {
        wifi_handle_client();
    }
    
    // Update display every second when WiFi is connected to show time
    // BUT update more frequently during logo page animation
    static unsigned long lastTimeUpdate = 0;
    static unsigned long lastDisplayUpdate = 0;
    
    // Update display at 50fps during logo page, 1fps otherwise
    unsigned long displayUpdateInterval = (currentPage == PAGE_LOGO) ? 20 : 1000;
    
    if (currentMillis - lastDisplayUpdate >= displayUpdateInterval) {
        lastDisplayUpdate = currentMillis;
        
        bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
        String timeString = wifi_get_time_string();
        
        // Refresh current page
        switch(currentPage) {
            case PAGE_LOGO:
                display_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
                break;
            case PAGE_MINING:
                // Only update mining page every second
                if (currentMillis - lastTimeUpdate >= 1000) {
                    display_page_mining(miningActive, wifiConnected, timeString.c_str(), isSoloMode, isDuinoCoinMode);
                }
                break;
            case PAGE_SETUP:
                // Only update setup page every second
                if (currentMillis - lastTimeUpdate >= 1000) {
                    display_page_setup(wifiEnabled, wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
                }
                break;
        }
        
        // Update time counter
        if (currentMillis - lastTimeUpdate >= 1000) {
            lastTimeUpdate = currentMillis;
        }
    }
    
    // Main application loop - keep system running
    static unsigned long lastHeartbeat = 0;
    
    // Print heartbeat every 10 seconds
    if (currentMillis - lastHeartbeat >= 10000) {
        lastHeartbeat = currentMillis;
        Serial.println("TzCoinMiner - System running normally");
        Serial.printf("Uptime: %lu seconds\n", currentMillis / 1000);
        
        // Print WiFi status
        WifiStatus status = wifi_get_status();
        if (status == WIFI_CONNECTED) {
            Serial.printf("WiFi: Connected to %s\n", WiFi.SSID().c_str());
        } else if (status == WIFI_AP_MODE) {
            Serial.println("WiFi: AP Mode Active (192.168.4.1)");
        } else {
            Serial.println("WiFi: Disconnected");
        }
        
        // Update mining active state based on actual task status
        if (isDuinoCoinMode) {
            miningActive = duino_task_is_running();
        } else {
            miningActive = mining_task_is_running();
        }
    }
    
    delay(10);  // Small delay for responsiveness
}
