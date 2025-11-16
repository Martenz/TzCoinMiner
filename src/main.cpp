#include <Arduino.h>
#include <WiFi.h>

// Include appropriate display header based on build flag
#ifdef DISPLAY_TYPE_M5PAPER
    #include "display_m5paper.h"
    #define CURRENT_PAGE_TYPE Page_M5Paper
    #define PAGE_LOGO_TYPE PAGE_LOGO_M5
    #define PAGE_MINING_TYPE PAGE_MINING_M5
    #define PAGE_SETUP_TYPE PAGE_SETUP_M5
    #define PAGE_COUNT_TYPE PAGE_COUNT_M5
#else
    #include "display.h"
    #define CURRENT_PAGE_TYPE Page
    #define PAGE_LOGO_TYPE PAGE_LOGO
    #define PAGE_MINING_TYPE PAGE_MINING
    #define PAGE_SETUP_TYPE PAGE_SETUP
    #define PAGE_COUNT_TYPE PAGE_COUNT
#endif

#include "wifi_config.h"
#include "mining_task.h"
#include "duino_task.h"

// Forward declarations
void handleButton1();
void handleButton2();
void handleButton2LongPress();
#ifdef DISPLAY_TYPE_M5PAPER
    void handleButtonUp();
    void handleButtonDown();
#endif

// Button pins - configurable per board
#ifdef DISPLAY_TYPE_M5PAPER
    #define PIN_BUTTON_CLICK 38   // M5Paper wheel button click
    #define PIN_BUTTON_UP 37      // M5Paper wheel button up (previous page)
    #define PIN_BUTTON_DOWN 39    // M5Paper wheel button down (next page)
    #define PIN_BUTTON_1 PIN_BUTTON_CLICK  // Alias for compatibility
    #define PIN_BUTTON_2 PIN_BUTTON_UP     // Alias for compatibility
#else
    #define PIN_BUTTON_1 0   // T-Display S3 boot button
    #define PIN_BUTTON_2 21  // T-Display S3 GPIO21
#endif

// Button debouncing
#define DEBOUNCE_DELAY 50
#define LONG_PRESS_DELAY 1000  // 1 second for long press

// Current page state
CURRENT_PAGE_TYPE currentPage = PAGE_LOGO_TYPE;
bool wifiEnabled = false;
bool miningActive = false;
bool isApMode = false;  // Track if AP mode is active
bool isSoloMode = false;  // Track if solo mining mode is configured
bool isDuinoCoinMode = false;  // Track if Duino-Coin mode is configured

// Monitor task handle
TaskHandle_t monitorTaskHandle = NULL;

// Display update queue - for requesting page changes from buttons without blocking
QueueHandle_t displayQueue = NULL;

enum DisplayCommand {
    DISPLAY_CMD_REFRESH,        // Force refresh current page
    DISPLAY_CMD_NEXT_PAGE,      // Next page
    DISPLAY_CMD_PREV_PAGE,      // Previous page
    DISPLAY_CMD_GOTO_LOGO,      // Go directly to logo page
    DISPLAY_CMD_GOTO_MINING,    // Go directly to mining page
    DISPLAY_CMD_GOTO_SETUP,     // Go directly to setup page
    DISPLAY_CMD_TOGGLE_WIFI,    // Toggle WiFi (setup page action)
    DISPLAY_CMD_TOGGLE_MINING   // Toggle mining (mining page action)
};

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
#ifdef DISPLAY_TYPE_M5PAPER
    ButtonState buttonDownState = {HIGH, false, 0, 0, false};  // For GPIO39
#endif

// Monitor task - handles display updates
// Runs on Core 1 with priority 5 (like NerdMiner)
void runMonitor(void* parameter) {
    Serial.println("[MONITOR] Task started on core " + String(xPortGetCoreID()));
    
    unsigned long lastDisplayUpdate = 0;
    unsigned long lastTimeUpdate = 0;
    
    while (true) {
        unsigned long currentMillis = millis();
        
        // Check for display commands from the queue (non-blocking)
        DisplayCommand cmd;
        if (xQueueReceive(displayQueue, &cmd, 0) == pdTRUE) {
            // Process display command
            bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
            String timeString = wifi_get_time_string();
            
            switch (cmd) {
                case DISPLAY_CMD_NEXT_PAGE:
                    currentPage = (CURRENT_PAGE_TYPE)((currentPage + 1) % PAGE_COUNT_TYPE);
                    Serial.printf("[MONITOR] Next page: %d\n", currentPage + 1);
                    
                    // Reset WiFi state when leaving setup page
                    if (currentPage != PAGE_SETUP_TYPE) {
                        wifiEnabled = false;
                        if (isApMode) {
                            wifi_stop_ap();
                            isApMode = false;
                        }
                    }
                    
                    // Force immediate display update
                    lastDisplayUpdate = 0;
                    break;
                    
                case DISPLAY_CMD_PREV_PAGE:
                    currentPage = (CURRENT_PAGE_TYPE)((currentPage + PAGE_COUNT_TYPE - 1) % PAGE_COUNT_TYPE);
                    Serial.printf("[MONITOR] Previous page: %d\n", currentPage + 1);
                    
                    // Reset WiFi state when leaving setup page
                    if (currentPage != PAGE_SETUP_TYPE) {
                        wifiEnabled = false;
                        if (isApMode) {
                            wifi_stop_ap();
                            isApMode = false;
                        }
                    }
                    
                    // Force immediate display update
                    lastDisplayUpdate = 0;
                    break;
                    
                case DISPLAY_CMD_GOTO_LOGO:
                    currentPage = PAGE_LOGO_TYPE;
                    Serial.println("[MONITOR] Going to logo page");
                    
                    // Reset WiFi state when leaving setup page
                    wifiEnabled = false;
                    if (isApMode) {
                        wifi_stop_ap();
                        isApMode = false;
                    }
                    
                    // Force immediate display update
                    lastDisplayUpdate = 0;
                    break;
                    
                case DISPLAY_CMD_GOTO_MINING:
                    currentPage = PAGE_MINING_TYPE;
                    Serial.println("[MONITOR] Going to mining page");
                    
                    // Reset WiFi state when leaving setup page
                    wifiEnabled = false;
                    if (isApMode) {
                        wifi_stop_ap();
                        isApMode = false;
                    }
                    
                    // Force immediate display update
                    lastDisplayUpdate = 0;
                    break;
                    
                case DISPLAY_CMD_GOTO_SETUP:
                    currentPage = PAGE_SETUP_TYPE;
                    Serial.println("[MONITOR] Going to setup page");
                    
                    // Force immediate display update
                    lastDisplayUpdate = 0;
                    break;
                    
                case DISPLAY_CMD_TOGGLE_WIFI:
                    // Only valid on setup page
                    if (currentPage == PAGE_SETUP_TYPE) {
                        wifiEnabled = !wifiEnabled;
                        
                        if (wifiEnabled) {
                            Serial.println("[MONITOR] Starting WiFi AP mode...");
                            wifi_start_ap();
                            isApMode = true;
                        } else {
                            Serial.println("[MONITOR] Stopping WiFi AP mode...");
                            wifi_stop_ap();
                            isApMode = false;
                            
                            Serial.println("[MONITOR] Reconnecting to saved WiFi...");
                            if (wifi_connect_saved()) {
                                Serial.println("[MONITOR] Reconnected to WiFi successfully!");
                            }
                        }
                        
                        // Force immediate display update
                        lastDisplayUpdate = 0;
                    }
                    break;
                    
                case DISPLAY_CMD_TOGGLE_MINING:
                    // Only valid on mining page
                    if (currentPage == PAGE_MINING_TYPE) {
                        miningActive = !miningActive;
                        
                        if (miningActive) {
                            if (isDuinoCoinMode) {
                                Serial.println("[MONITOR] Starting Duino-Coin mining...");
                                duino_task_start();
                            } else {
                                Serial.println("[MONITOR] Starting Bitcoin mining...");
                                mining_task_start();
                            }
                        } else {
                            if (isDuinoCoinMode) {
                                Serial.println("[MONITOR] Stopping Duino-Coin mining...");
                                duino_task_stop();
                            } else {
                                Serial.println("[MONITOR] Stopping Bitcoin mining...");
                                mining_task_stop();
                            }
                        }
                        
                        // Force immediate display update
                        lastDisplayUpdate = 0;
                    }
                    break;
                    
                case DISPLAY_CMD_REFRESH:
                    // Just force immediate refresh
                    lastDisplayUpdate = 0;
                    break;
            }
        }
        
        // Update display interval based on display type and page
        unsigned long displayUpdateInterval;
        
        #ifdef DISPLAY_TYPE_M5PAPER
            // E-ink displays update slowly - check every 5 seconds
            // (the display function itself will decide whether to actually update)
            displayUpdateInterval = 5000;
        #else
            // AMOLED: 50fps during logo page, 1fps otherwise
            displayUpdateInterval = (currentPage == PAGE_LOGO_TYPE) ? 20 : 1000;
        #endif
        
        if (currentMillis - lastDisplayUpdate >= displayUpdateInterval) {
            lastDisplayUpdate = currentMillis;
            
            bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
            String timeString = wifi_get_time_string();
            
            // Refresh current page based on display type
            #ifdef DISPLAY_TYPE_M5PAPER
                switch(currentPage) {
                    case PAGE_LOGO_M5:
                        display_m5paper_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
                        break;
                    case PAGE_MINING_M5:
                        if (currentMillis - lastTimeUpdate >= 1000) {
                            display_m5paper_page_mining(miningActive, wifiConnected, timeString.c_str(), isSoloMode, isDuinoCoinMode);
                        }
                        break;
                    case PAGE_SETUP_M5:
                        if (currentMillis - lastTimeUpdate >= 1000) {
                            display_m5paper_page_setup(wifiEnabled, wifiConnected, isApMode, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
                        }
                        break;
                }
            #else
                switch(currentPage) {
                    case PAGE_LOGO:
                        display_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
                        break;
                    case PAGE_MINING:
                        if (currentMillis - lastTimeUpdate >= 1000) {
                            display_page_mining(miningActive, wifiConnected, timeString.c_str(), isSoloMode, isDuinoCoinMode);
                        }
                        break;
                    case PAGE_SETUP:
                        if (currentMillis - lastTimeUpdate >= 1000) {
                            display_page_setup(wifiEnabled, wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
                        }
                        break;
                }
            #endif
            
            // Update time counter
            if (currentMillis - lastTimeUpdate >= 1000) {
                lastTimeUpdate = currentMillis;
            }
        }
        
        // Yield every 50ms (like NerdMiner)
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}


void setup()
{
    Serial.begin(115200);
    delay(100);
    
    Serial.println("\n\nStarting TzBtcMiner Application...");
    
    // Initialize buttons
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
    #ifdef DISPLAY_TYPE_M5PAPER
        pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);
        Serial.printf("M5Paper Wheel Buttons initialized:\n");
        Serial.printf("  Click (GPIO %d): %s\n", PIN_BUTTON_CLICK, digitalRead(PIN_BUTTON_CLICK) ? "HIGH" : "LOW");
        Serial.printf("  Up (GPIO %d): %s\n", PIN_BUTTON_UP, digitalRead(PIN_BUTTON_UP) ? "HIGH" : "LOW");
        Serial.printf("  Down (GPIO %d): %s\n", PIN_BUTTON_DOWN, digitalRead(PIN_BUTTON_DOWN) ? "HIGH" : "LOW");
    #else
        Serial.printf("Button 1 (GPIO %d) initial state: %s\n", PIN_BUTTON_1, digitalRead(PIN_BUTTON_1) ? "HIGH" : "LOW");
        Serial.printf("Button 2 (GPIO %d) initial state: %s\n", PIN_BUTTON_2, digitalRead(PIN_BUTTON_2) ? "HIGH" : "LOW");
    #endif
    
    // Initialize the display hardware based on build flag
    #ifdef DISPLAY_TYPE_M5PAPER
        display_m5paper_init();
    #else
        display_init();
    #endif
    
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
    
    #ifdef DISPLAY_TYPE_M5PAPER
        display_m5paper_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
    #else
        display_page_logo(wifiConnected, timeString.c_str(), miningActive, isSoloMode, isDuinoCoinMode);
    #endif
    
    Serial.println("System initialization complete");
    Serial.println("Ready for operations");
    Serial.println("\n=== BUTTON CONTROLS ===");
    Serial.println("Button 1 (BOOT): Switch pages");
    Serial.println("Button 2 (GPIO21): Page actions");
    Serial.println("=======================\n");
    
    // Create display queue (10 commands deep)
    displayQueue = xQueueCreate(10, sizeof(DisplayCommand));
    if (displayQueue == NULL) {
        Serial.println("ERROR: Failed to create display queue!");
    } else {
        Serial.println("Display queue created successfully");
    }
    
    // Create monitor task on Core 1 with priority 5 (like NerdMiner)
    Serial.println("Creating Monitor task...");
    BaseType_t monitorResult = xTaskCreatePinnedToCore(
        runMonitor,           // Task function
        "Monitor",            // Task name
        10000,                // Stack size (10KB like NerdMiner)
        NULL,                 // Parameters
        5,                    // Priority (5 like NerdMiner)
        &monitorTaskHandle,   // Task handle
        1                     // Core 1
    );
    
    if (monitorResult == pdPASS) {
        Serial.println("Monitor task created successfully on Core 1");
    } else {
        Serial.println("ERROR: Failed to create Monitor task!");
    }
}

void handleButton1() 
{
    #ifdef DISPLAY_TYPE_M5PAPER
        // M5Paper: Click button now advances to next page (like up/down)
        DisplayCommand cmd = DISPLAY_CMD_NEXT_PAGE;
        xQueueSend(displayQueue, &cmd, 0);
        Serial.printf("Requested next page via click\n");
    #else
        // T-Display S3: Button 1 switches pages (rotate through pages)
        DisplayCommand cmd = DISPLAY_CMD_NEXT_PAGE;
        xQueueSend(displayQueue, &cmd, 0);
        Serial.printf("Requested page switch\n");
    #endif
}

#ifdef DISPLAY_TYPE_M5PAPER
// M5Paper wheel button: Navigate to previous page
void handleButtonUp() 
{
    DisplayCommand cmd = DISPLAY_CMD_PREV_PAGE;
    xQueueSend(displayQueue, &cmd, 0);
}

// M5Paper wheel button: Navigate to next page
void handleButtonDown() 
{
    DisplayCommand cmd = DISPLAY_CMD_NEXT_PAGE;
    xQueueSend(displayQueue, &cmd, 0);
}
#endif

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
    
    // Request display refresh (Monitor task will handle it)
    DisplayCommand cmd = DISPLAY_CMD_REFRESH;
    xQueueSend(displayQueue, &cmd, 0);
    
    Serial.printf("Mode switched to: %s\n", isSoloMode ? "SOLO" : "POOL");
    Serial.println("NOTE: This change is temporary and will reset on reboot");
}

void handleButton2() 
{
    // Get current WiFi status and time
    bool wifiConnected = (wifi_get_status() == WIFI_CONNECTED);
    DisplayCommand cmd;
    
    // Button 2: Perform page-specific actions
    switch(currentPage) {
        case PAGE_LOGO_TYPE:
            // Refresh logo colors (only for AMOLED)
            #ifndef DISPLAY_TYPE_M5PAPER
                display_refresh_logo_colors();
            #endif
            // Request display refresh
            cmd = DISPLAY_CMD_REFRESH;
            xQueueSend(displayQueue, &cmd, 0);
            Serial.println("Action: Refreshed logo page");
            break;
            
        case PAGE_MINING_TYPE:
            // Check if WiFi is connected before allowing mining toggle
            if (!wifiConnected) {
                // No WiFi - don't toggle mining
                Serial.println("Action: Cannot start mining - No WiFi connection");
                cmd = DISPLAY_CMD_REFRESH;
                xQueueSend(displayQueue, &cmd, 0);
                break;
            }
            
            // Toggle mining via queue (Monitor task will handle the actual start/stop)
            cmd = DISPLAY_CMD_TOGGLE_MINING;
            xQueueSend(displayQueue, &cmd, 0);
            
            Serial.printf("Action: Requested mining %s\n", miningActive ? "STOP" : "START");
            break;
            
        case PAGE_SETUP_TYPE:
            // Toggle WiFi via queue (Monitor task will handle it)
            cmd = DISPLAY_CMD_TOGGLE_WIFI;
            xQueueSend(displayQueue, &cmd, 0);
            
            Serial.printf("Action: Requested WiFi %s\n", wifiEnabled ? "OFF" : "ON");
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
    #ifdef DISPLAY_TYPE_M5PAPER
        // M5Paper wheel button controls
        // UP button (GPIO37): Previous page
        int buttonUpPress = readButton(PIN_BUTTON_UP, button2State);
        if (buttonUpPress == 1) {
            Serial.println(">>> M5Paper wheel UP pressed!");
            handleButtonUp();
        }
        
        // DOWN button (GPIO39): Next page
        int buttonDownPress = readButton(PIN_BUTTON_DOWN, buttonDownState);
        if (buttonDownPress == 1) {
            Serial.println(">>> M5Paper wheel DOWN pressed!");
            handleButtonDown();
        }
        
        // CLICK button (GPIO38): Action on current page
        int buttonClickPress = readButton(PIN_BUTTON_CLICK, button1State);
        if (buttonClickPress == 1) {
            Serial.println(">>> M5Paper wheel CLICK pressed!");
            handleButton1();
        }
        
        // Check for touch screen input (bottom buttons)
        TouchState touchState = display_m5paper_check_touch(currentPage);
        
        static int pressedButton = 0;
        static int pressedPage = -1;
        
        // Handle touch released (finger lift)
        if (touchState.justReleased && pressedButton > 0) {
            Serial.printf("[TOUCH] Button %d released on page %d\n", pressedButton, currentPage);
            
            // Only process if released on the same page it was pressed
            if (pressedPage == currentPage) {
                // On logo page: buttons are [Stats][Settings]
                if (currentPage == PAGE_LOGO_M5) {
                    if (pressedButton == 1) {
                        // Stats button - go directly to mining page
                        Serial.println("[TOUCH] Stats button - going to mining page");
                        DisplayCommand cmd = DISPLAY_CMD_GOTO_MINING;
                        xQueueSend(displayQueue, &cmd, 0);
                    } else if (pressedButton == 2) {
                        // Settings button - go directly to setup page
                        Serial.println("[TOUCH] Settings button - going to setup page");
                        DisplayCommand cmd = DISPLAY_CMD_GOTO_SETUP;
                        xQueueSend(displayQueue, &cmd, 0);
                    }
                }
                // On mining page: [Back] button
                else if (currentPage == PAGE_MINING_M5) {
                    if (pressedButton == 1) {
                        // Back button - go to logo page
                        Serial.println("[TOUCH] Back button - going to logo page");
                        DisplayCommand cmd = DISPLAY_CMD_GOTO_LOGO;
                        xQueueSend(displayQueue, &cmd, 0);
                    }
                }
                // On setup page: [Back][AP MODE] buttons
                else if (currentPage == PAGE_SETUP_M5) {
                    if (pressedButton == 1) {
                        // Back button - go to logo page
                        Serial.println("[TOUCH] Back button - going to logo page");
                        DisplayCommand cmd = DISPLAY_CMD_GOTO_LOGO;
                        xQueueSend(displayQueue, &cmd, 0);
                    } else if (pressedButton == 2) {
                        // AP MODE button - toggle WiFi AP mode
                        Serial.println("[TOUCH] AP MODE button - toggling WiFi AP");
                        DisplayCommand cmd = DISPLAY_CMD_TOGGLE_WIFI;
                        xQueueSend(displayQueue, &cmd, 0);
                    }
                }
            }
            
            // Reset pressed button state
            pressedButton = 0;
            pressedPage = -1;
        }
        // Handle touch press (finger down)
        else if (touchState.isPressed && pressedButton == 0) {
            pressedButton = touchState.buttonNumber;
            pressedPage = currentPage;
            Serial.printf("[TOUCH] Button %d pressed on page %d\n", pressedButton, currentPage);
            
            // TODO: Draw highlighted button here for visual feedback
            // This would require adding a function to redraw just the button area
        }
    #else
        // T-Display S3 button controls
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
        } else if (button2Press == 2 && currentPage == PAGE_MINING_TYPE) {
            Serial.println(">>> Button 2 LONG pressed on Mining page!");
            handleButton2LongPress();
        }
    #endif
}

void loop()
{
    // Simplified loop - only handle buttons and web server
    // Display updates now handled by Monitor task on Core 1
    
    // Check for button presses
    checkButtons();
    
    // Handle web server requests if in AP mode
    if (isApMode) {
        wifi_handle_client();
    }
    
    // Update mining active state based on actual task status
    if (isDuinoCoinMode) {
        miningActive = duino_task_is_running();
    } else {
        miningActive = mining_task_is_running();
    }
    
    // Yield to other tasks (50ms like NerdMiner)
    vTaskDelay(50 / portTICK_PERIOD_MS);
}
