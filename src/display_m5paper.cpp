#include "display_m5paper.h"
#include "mining_task.h"
#include "duino_task.h"
#include "wifi_config.h"
#include <M5EPD.h>
#include <Arduino.h>
#include <WiFi.h>

// M5Paper display dimensions
#define M5PAPER_WIDTH 540
#define M5PAPER_HEIGHT 960

// Canvas for full page (used for page changes only)
M5EPD_Canvas canvas_page(&M5.EPD);

// Small canvas for dynamic updates (hashrate, shares)
M5EPD_Canvas canvas_stats(&M5.EPD);

// Current page state
static int current_page = 0;
static bool needs_update = true;
static bool page_initialized = false;  // Track if static content is drawn
static int last_drawn_page = -1;       // Track which page was last drawn

// Change detection for mining page
static uint32_t last_hashrate = 0;
static uint32_t last_shares = 0;
static bool last_wifi_status = false;
static unsigned long last_full_refresh = 0;
static uint8_t update_counter = 0;

// Change detection for logo/setup pages
static bool last_mining_active = false;
static bool last_ap_mode = false;

#define FULL_REFRESH_INTERVAL_MS 60000  // Full refresh every 60 seconds to clear ghosting
#define STATS_CANVAS_WIDTH 400
#define STATS_CANVAS_HEIGHT 60

void display_m5paper_init(void)
{
    Serial.println("[M5PAPER] Initializing M5EPD library...");
    
    M5.begin();
    M5.EPD.SetRotation(90);  // Landscape
    M5.TP.SetRotation(90);   // Touch screen rotation must match display
    M5.EPD.Clear(true);      // Full clear on init
    M5.RTC.begin();
    
    // Create canvas for the full screen (page changes)
    canvas_page.createCanvas(M5PAPER_WIDTH, M5PAPER_HEIGHT);
    canvas_page.setTextSize(4);
    canvas_page.setTextColor(15);       // Black text (15 = black in M5EPD 4-bit grayscale)
    canvas_page.setTextDatum(TL_DATUM); // Top-left alignment
    
    // Create small canvas for stats updates (partial updates)
    canvas_stats.createCanvas(STATS_CANVAS_WIDTH, STATS_CANVAS_HEIGHT);
    canvas_stats.setTextSize(4);
    canvas_stats.setTextColor(15);
    canvas_stats.setTextDatum(TL_DATUM);
    
    Serial.printf("[M5PAPER] Display initialized: %dx%d (Touch enabled)\n", M5PAPER_WIDTH, M5PAPER_HEIGHT);
    
    // Draw initial page
    needs_update = true;
    page_initialized = false;
}

// Helper: get battery percentage from M5Paper
static int getBatteryPercentage()
{
    // M5Paper uses IP5306 power management IC
    // Battery voltage range: 3.0V (0%) to 4.2V (100%)
    uint32_t vol = M5.getBatteryVoltage();
    
    if (vol < 3000) return 0;
    if (vol > 4200) return 100;
    
    // Linear interpolation between 3.0V and 4.2V
    int percentage = ((vol - 3000) * 100) / 1200;
    return constrain(percentage, 0, 100);
}

// Helper: draw an outline-only rounded button and center a label inside it
static void draw_outline_button(M5EPD_Canvas &c, int x, int y, int w, int h, int r, const char *label)
{
    // Stroke thickness
    const int stroke = 2;
    for (int s = 0; s < stroke; s++) {
        c.drawRoundRect(x + s, y + s, w - 2 * s, h - 2 * s, r, 15);
    }

    // Draw label centered inside
    c.setTextDatum(MC_DATUM); // Middle-center
    // Choose a text size that fits roughly the button height
    c.setTextSize(2);
    int cx = x + w / 2;
    int cy = y + h / 2 - 2; // small vertical tweak
    c.drawCentreString(label, cx, cy, 1); // font 1
    c.setTextDatum(TL_DATUM);
}

// Helper: draw a filled (highlighted) rounded button with inverted colors
static void draw_filled_button(M5EPD_Canvas &c, int x, int y, int w, int h, int r, const char *label)
{
    // Fill the button with black
    c.fillRoundRect(x, y, w, h, r, 15);
    
    // Draw label centered inside with white text
    c.setTextDatum(MC_DATUM); // Middle-center
    c.setTextSize(2);
    c.setTextColor(0); // White text on black background
    int cx = x + w / 2;
    int cy = y + h / 2 - 2; // small vertical tweak
    c.drawCentreString(label, cx, cy, 1); // font 1
    c.setTextColor(15); // Reset to black
    c.setTextDatum(TL_DATUM);
}

void display_m5paper_clear(void)
{
    M5.EPD.Clear(true);
    canvas_page.fillCanvas(0);  // White background (0 = white in M5EPD)
}

void display_m5paper_refresh(void)
{
    if (!needs_update) {
        return;
    }
    
    // Use partial update for smooth refresh (UPDATE_MODE_DU4 is fast)
    canvas_page.pushCanvas(0, 0, UPDATE_MODE_DU4);
    needs_update = false;
}

void display_m5paper_page_logo(bool wifiConnected, const char* timeStr, bool miningActive, bool soloMode, bool isDuinoCoin)
{
    // Check if we need to redraw (page changed or data changed)
    bool page_changed = (last_drawn_page != 0);
    bool data_changed = (wifiConnected != last_wifi_status) || (miningActive != last_mining_active);
    
    // Force full redraw on page change
    if (page_changed) {
        page_initialized = false;
    }
    
    // Skip redraw if nothing changed and page already initialized
    if (!page_changed && !data_changed && page_initialized) {
        return;
    }
    
    Serial.println("[M5PAPER] Logo page: Redrawing");    // Full page redraw for logo (page change)
    canvas_page.fillCanvas(0);   // White background (0 = white in M5EPD)
    canvas_page.setTextColor(15); // Black text (15 = black in M5EPD)

    // Draw time/date and battery at top
    canvas_page.setTextSize(2);
    if (timeStr != nullptr) {
        canvas_page.drawString(timeStr, 10, 10);
    }
    
    // Battery percentage on the right
    int battPct = getBatteryPercentage();
    String battStr = "Batt: " + String(battPct) + "%";
    canvas_page.drawString(battStr.c_str(), M5PAPER_WIDTH - 140, 10);

    canvas_page.setTextSize(6);
    canvas_page.drawString("TzCoinMiner", 80, 200);

    canvas_page.setTextSize(3);
    canvas_page.drawString("M5Paper Edition", 140, 300);

    // Coin label under edition
    canvas_page.setTextSize(2);
    if (isDuinoCoin) {
        canvas_page.drawString("Duino-Coin", 200, 340);
    } else {
        canvas_page.drawString("Bitcoin (BTC)", 200, 340);
    }

    canvas_page.setTextSize(3);
    if (wifiConnected) {
        canvas_page.drawString("WiFi: Connected", 160, 420);
    } else {
        canvas_page.drawString("WiFi: Disconnected", 140, 420);
    }

    // Draw bottom rounded-outline buttons: [Stats] [Settings]
    const int btnW = 140;
    const int btnH = 60;
    const int spacing = 40;
    const int totalW = btnW * 2 + spacing;
    const int startX = (M5PAPER_WIDTH - totalW) / 2;
    const int y = M5PAPER_HEIGHT - btnH - 40;

    draw_outline_button(canvas_page, startX, y, btnW, btnH, 12, "Stats");
    draw_outline_button(canvas_page, startX + btnW + spacing, y, btnW, btnH, 12, "Settings");

    // Use full refresh for page changes (UPDATE_MODE_GC16 for quality)
    canvas_page.pushCanvas(0, 0, UPDATE_MODE_GC16);

    // Update tracking
    last_drawn_page = 0;
    last_wifi_status = wifiConnected;
    last_mining_active = miningActive;
    page_initialized = true;  // Static content drawn
}

void display_m5paper_page_setup(bool wifiEnabled, bool wifiConnected, bool isApMode, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin)
{
    // Check if we need to redraw (page changed or data changed)
    bool page_changed = (last_drawn_page != 2);
    bool data_changed = (wifiConnected != last_wifi_status) || (miningActive != last_mining_active) || (isApMode != last_ap_mode);
    
    // Force full redraw on page change
    if (page_changed) {
        page_initialized = false;
    }
    
    // Skip redraw if nothing changed and page already initialized
    if (!page_changed && !data_changed && page_initialized) {
        return;
    }
    
    Serial.println("[M5PAPER] Setup page: Redrawing");
    
    // Full page redraw for setup page (page change)
    canvas_page.fillCanvas(0);   // White background (0 = white in M5EPD)
    canvas_page.setTextColor(15); // Black text (15 = black in M5EPD)
    
    // Draw time/date and battery at top
    canvas_page.setTextSize(2);
    if (timeStr != nullptr) {
        canvas_page.drawString(timeStr, 10, 10);
    }
    
    // Battery percentage on the right
    int battPct = getBatteryPercentage();
    String battStr = "Batt: " + String(battPct) + "%";
    canvas_page.drawString(battStr.c_str(), M5PAPER_WIDTH - 140, 10);
    
    canvas_page.setTextSize(5);
    canvas_page.drawString("Setup", 50, 50);
    
    canvas_page.setTextSize(3);
    
    if (wifiConnected) {
        String ssid = "WiFi: " + WiFi.SSID();
        canvas_page.drawString(ssid.c_str(), 50, 150);
        
        String ip = "IP: " + WiFi.localIP().toString();
        canvas_page.drawString(ip.c_str(), 50, 210);
    } else {
        canvas_page.drawString("WiFi: Disconnected", 50, 150);
    }
    
    // Show AP credentials only when AP mode is enabled
    if (isApMode) {
        canvas_page.setTextSize(2);
        canvas_page.drawString("AP SSID: TzCoinMinerWifi", 50, 260);
        canvas_page.drawString("AP Password: theansweris42", 50, 290);
        canvas_page.setTextSize(2);
        canvas_page.drawString("Connect to the WiFi AP above", 50, 340);
        canvas_page.drawString("to configure the device", 50, 370);
    }
    
    canvas_page.setTextSize(3);
    String modeStr = "Mode: ";
    if (isDuinoCoin) {
        modeStr += "Duino-Coin";
    } else if (isSoloMode) {
        modeStr += "Solo Mining";
    } else {
        modeStr += "Pool Mining";
    }
    canvas_page.drawString(modeStr.c_str(), 50, 420);
    
    String statusStr = String("Mining: ") + (miningActive ? "Active" : "Stopped");
    canvas_page.drawString(statusStr.c_str(), 50, 480);
    
    // Draw bottom buttons: [Back] [Configure/Disconnect]
    const int btnW = 140;
    const int btnH = 60;
    const int spacing = 40;
    const int totalW = btnW * 2 + spacing;
    const int startX = (M5PAPER_WIDTH - totalW) / 2;
    const int by = M5PAPER_HEIGHT - btnH - 40;
    
    draw_outline_button(canvas_page, startX, by, btnW, btnH, 12, "Back");
    
    // Second button changes based on AP mode state
    if (isApMode) {
        draw_filled_button(canvas_page, startX + btnW + spacing, by, btnW, btnH, 12, "Disconnect");
    } else {
        draw_outline_button(canvas_page, startX + btnW + spacing, by, btnW, btnH, 12, "Configure");
    }
    
    // Full refresh for page change
    canvas_page.pushCanvas(0, 0, UPDATE_MODE_GC16);
    
    // Update tracking
    last_drawn_page = 2;
    last_wifi_status = wifiConnected;
    last_mining_active = miningActive;
    last_ap_mode = isApMode;
    page_initialized = true;  // Static content drawn
}

void display_m5paper_page_mining(bool miningActive, bool wifiConnected, const char* timeStr, bool isSoloMode, bool isDuinoCoin)
{
    // Get stats
    MiningStats miningStats;
    DuinoStats duinoStats;
    
    uint32_t current_hashrate = 0;
    uint32_t current_shares = 0;
    
    if (isDuinoCoin) {
        duinoStats = duino_get_stats();
        current_hashrate = duinoStats.hashes_per_second;
        current_shares = duinoStats.shares_accepted;
    } else {
        miningStats = mining_get_stats();
        current_hashrate = miningStats.hashes_per_second;
        current_shares = miningStats.shares_accepted;
    }
    
    // Check if page changed
    bool page_changed = (last_drawn_page != 1);
    
    // Force full redraw on page change
    if (page_changed) {
        page_initialized = false;
    }
    
    // Check if data actually changed (avoid unnecessary updates)
    bool data_changed = (current_hashrate != last_hashrate) || 
                       (current_shares != last_shares) ||
                       (wifiConnected != last_wifi_status);
    
    // Force full refresh every 60 seconds to clear ghosting
    unsigned long now = millis();
    bool force_full_refresh = (now - last_full_refresh) > FULL_REFRESH_INTERVAL_MS;
    
    // Draw static content only once when page is first displayed
    if (!page_initialized) {
        Serial.println("[M5PAPER] Mining page: Drawing static content");
        
        // Full page clear and static content
        canvas_page.fillCanvas(0);   // White background
        canvas_page.setTextColor(15); // Black text
        
        // Draw time/date and battery at top
        canvas_page.setTextSize(2);
        if (timeStr != nullptr) {
            canvas_page.drawString(timeStr, 10, 10);
        }
        
        // Battery percentage on the right
        int battPct = getBatteryPercentage();
        String battStr = "Batt: " + String(battPct) + "%";
        canvas_page.drawString(battStr.c_str(), M5PAPER_WIDTH - 140, 10);
        
        // Title area
        canvas_page.setTextSize(5);
        if (isDuinoCoin) {
            canvas_page.drawString("Duino-Coin", 50, 50);
        } else {
            String title = String(isSoloMode ? "Solo" : "Pool") + " Mining";
            canvas_page.drawString(title.c_str(), 50, 50);
        }
        
        // Pool URL below title
        extern WifiConfig config;
        canvas_page.setTextSize(2);
        if (isDuinoCoin) {
            canvas_page.drawString("Pool: server.duinocoin.com", 50, 120);
        } else if (!isSoloMode && strlen(config.poolUrl) > 0) {
            String poolStr = "Pool: ";
            poolStr += config.poolUrl;
            if (config.poolPort > 0) {
                poolStr += ":" + String(config.poolPort);
            }
            canvas_page.drawString(poolStr.c_str(), 50, 120);
        } else if (isSoloMode) {
            canvas_page.drawString("Solo: Local RPC", 50, 120);
        }
        
        // Status text
        canvas_page.setTextSize(3);
        const char* statusText;
        if (!wifiConnected) {
            statusText = "Status: no wifi";
        } else {
            statusText = miningActive ? "Status: active" : "Status: inactive";
        }
        canvas_page.drawString(statusText, 50, 150);
        
        // Static labels for mining stats
        canvas_page.setTextSize(3);
        canvas_page.drawString("H/s:", 50, 250);
        canvas_page.drawString("best:", 50, 320);
        canvas_page.drawString("pool:", 50, 390);
        canvas_page.drawString("shares:", 50, 460);
        canvas_page.drawString("found:", 50, 530);
        
        // Coin label overlay
        const char* coinName;
        if (isDuinoCoin) {
            coinName = "DUCO";
        } else {
            extern WifiConfig config;
            coinName = config.useBitcoinCash ? "BCH" : "BTC";
        }
        canvas_page.setTextSize(5);
        canvas_page.drawString(coinName, M5PAPER_WIDTH - 150, M5PAPER_HEIGHT - 100);
        
        // Draw bottom Back button (outline)
        const int btnW = 140;
        const int btnH = 60;
        const int bx = (M5PAPER_WIDTH - btnW) / 2;
        const int by = M5PAPER_HEIGHT - btnH - 40;
        draw_outline_button(canvas_page, bx, by, btnW, btnH, 12, "Back");
        
        // Push full page with high quality
        canvas_page.pushCanvas(0, 0, UPDATE_MODE_GC16);
        
        page_initialized = true;
        last_drawn_page = 1;  // Mining page
        last_full_refresh = now;
        
        // Initialize tracking variables
        last_hashrate = 0;
        last_shares = 0;
        last_wifi_status = wifiConnected;
        update_counter = 0;
        
        // Force update of dynamic content
        data_changed = true;
    }
    
    // Skip update if nothing changed and not forcing refresh
    if (!data_changed && !force_full_refresh) {
        return;
    }
    
    // If forcing full refresh, redraw static content too
    if (force_full_refresh) {
        Serial.println("[M5PAPER] Mining page: Periodic full refresh");
        page_initialized = false;
        display_m5paper_page_mining(miningActive, wifiConnected, timeStr, isSoloMode, isDuinoCoin);
        return;
    }
    
    // Update tracking variables
    last_hashrate = current_hashrate;
    last_shares = current_shares;
    last_wifi_status = wifiConnected;
    update_counter++;
    
    // ===== PARTIAL UPDATE: Only update dynamic data areas =====
    
    char buffer[128];
    
    // Prepare small canvas for updates
    canvas_stats.fillCanvas(0);  // Clear
    canvas_stats.setTextSize(3);
    canvas_stats.setTextColor(15);
    
    if (miningActive) {
        // Update hash rate
        canvas_stats.fillCanvas(0);
        float totalHashesM = (isDuinoCoin ? duinoStats.total_hashes : miningStats.total_hashes) / 1000000.0;
        if (current_hashrate >= 1000) {
            snprintf(buffer, sizeof(buffer), "%.1fK/%.1fM", current_hashrate / 1000.0, totalHashesM);
        } else {
            snprintf(buffer, sizeof(buffer), "%u/%.1fM", current_hashrate, totalHashesM);
        }
        canvas_stats.drawString(buffer, 0, 0);
        canvas_stats.pushCanvas(200, 250, UPDATE_MODE_DU4);
        
        // Update best difficulty (zeros from dual cores)
        canvas_stats.fillCanvas(0);
        if (isDuinoCoin) {
            snprintf(buffer, sizeof(buffer), "%.1f", duinoStats.difficulty);
        } else {
            int core0_zeros = 0, core1_zeros = 0;
            mining_get_dual_core_stats(&core0_zeros, &core1_zeros);
            if (core0_zeros > 0 || core1_zeros > 0) {
                snprintf(buffer, sizeof(buffer), "%uz - %uz", core0_zeros, core1_zeros);
            } else {
                snprintf(buffer, sizeof(buffer), "0z - 0z");
            }
        }
        canvas_stats.drawString(buffer, 0, 0);
        canvas_stats.pushCanvas(200, 320, UPDATE_MODE_DU4);
        
        // Update pool/block info
        canvas_stats.fillCanvas(0);
        if (isDuinoCoin) {
            snprintf(buffer, sizeof(buffer), "duco pool");
        } else if (miningStats.block_height > 0) {
            snprintf(buffer, sizeof(buffer), "blk %u", miningStats.block_height);
        } else if (!isSoloMode) {
            // Pool status
            if (miningStats.shares_accepted > 0) {
                snprintf(buffer, sizeof(buffer), "active");
            } else if (miningStats.pool_connected) {
                snprintf(buffer, sizeof(buffer), "connected");
            } else {
                snprintf(buffer, sizeof(buffer), "connecting");
            }
        } else {
            snprintf(buffer, sizeof(buffer), "demo");
        }
        canvas_stats.drawString(buffer, 0, 0);
        canvas_stats.pushCanvas(200, 390, UPDATE_MODE_DU4);
        
        // Update shares
        canvas_stats.fillCanvas(0);
        if (isDuinoCoin) {
            snprintf(buffer, sizeof(buffer), "%u/%u", duinoStats.shares_accepted, duinoStats.shares_rejected);
        } else {
            uint32_t submitted = isSoloMode ? miningStats.shares_accepted : miningStats.shares_submitted;
            uint32_t accepted = miningStats.shares_accepted;
            if (submitted < 1000 && accepted < 1000) {
                snprintf(buffer, sizeof(buffer), "%u/%u", submitted, accepted);
            } else {
                uint32_t submitted_k = (submitted + 500) / 1000;
                uint32_t accepted_k = (accepted + 500) / 1000;
                snprintf(buffer, sizeof(buffer), "%uK/%uK", submitted_k, accepted_k);
            }
        }
        canvas_stats.drawString(buffer, 0, 0);
        canvas_stats.pushCanvas(200, 460, UPDATE_MODE_DU4);
        
        // Update blocks found
        canvas_stats.fillCanvas(0);
        if (isDuinoCoin) {
            snprintf(buffer, sizeof(buffer), "%u", duinoStats.shares_accepted);
        } else {
            snprintf(buffer, sizeof(buffer), "%u", miningStats.blocks_found);
        }
        canvas_stats.drawString(buffer, 0, 0);
        canvas_stats.pushCanvas(200, 530, UPDATE_MODE_DU4);
    }
}

void display_m5paper_draw_status_bar(bool wifiConnected, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin)
{
    // Status bar integrated in pages
}

void display_m5paper_sleep(void)
{
    M5.EPD.Sleep();
}

void display_m5paper_wakeup(void)
{
    M5.EPD.SetRotation(90);
}

// Touch handling - returns touch state with button info
TouchState display_m5paper_check_touch(int current_page_num)
{
    static bool wasTouched = false;
    static int lastButton = 0;
    static unsigned long touchStartTime = 0;
    static unsigned long lastReleaseTime = 0;
    
    TouchState state = {false, false, 0, 0, 0};
    
    // Debounce: ignore touches for 200ms after release to prevent phantom touches
    if (wasTouched == false && (millis() - lastReleaseTime) < 200) {
        return state;
    }
    
    M5.TP.update();
    
    if (!M5.TP.available()) {
        if (wasTouched) {
            // Finger was touching, now released
            state.justReleased = true;
            state.buttonNumber = lastButton;
            wasTouched = false;
            lastButton = 0;
            lastReleaseTime = millis();
            Serial.println("[M5PAPER] Touch released (not available)");
        }
        return state;
    }
    
    // Check if finger is up (touch ended)
    if (M5.TP.isFingerUp()) {
        if (wasTouched) {
            // Finger was touching, now released
            state.justReleased = true;
            state.buttonNumber = lastButton;
            wasTouched = false;
            lastButton = 0;
            lastReleaseTime = millis();
            Serial.println("[M5PAPER] Touch released (finger up)");
        }
        return state;
    }
    
    tp_finger_t finger = M5.TP.readFinger(0);
    
    state.x = finger.x;
    state.y = finger.y;
    
    // Filter out invalid touches (0,0 is typically a false positive)
    if (finger.x == 0 && finger.y == 0) {
        return state;
    }
    
    // Define button areas based on page (using landscape coordinates: WIDTH=540, HEIGHT=960)
    // Buttons are at the bottom of the screen (y > 860)
    if (finger.y > 860 && finger.y < 920) {
        int buttonPressed = 0;
        
        // Page 0 (Logo): Two buttons [Stats][Settings]
        if (current_page_num == 0) {
            const int btnW = 140;
            const int spacing = 40;
            const int totalW = btnW * 2 + spacing;
            const int startX = (M5PAPER_WIDTH - totalW) / 2;
            
            if (finger.x >= startX && finger.x < (startX + btnW)) {
                buttonPressed = 1;  // Left button (Stats)
            } else if (finger.x >= (startX + btnW + spacing) && 
                       finger.x < (startX + totalW)) {
                buttonPressed = 2;  // Right button (Settings)
            }
        }
        // Page 1 (Mining): Single centered [Back] button
        else if (current_page_num == 1) {
            const int btnW = 140;
            const int bx = (M5PAPER_WIDTH - btnW) / 2;
            
            if (finger.x >= bx && finger.x < (bx + btnW)) {
                buttonPressed = 1;  // Back button
            }
        }
        // Page 2 (Setup): Two buttons [Back][AP MODE]
        else if (current_page_num == 2) {
            const int btnW = 140;
            const int spacing = 40;
            const int totalW = btnW * 2 + spacing;
            const int startX = (M5PAPER_WIDTH - totalW) / 2;
            
            if (finger.x >= startX && finger.x < (startX + btnW)) {
                buttonPressed = 1;  // Left button (Back)
            } else if (finger.x >= (startX + btnW + spacing) && 
                       finger.x < (startX + totalW)) {
                buttonPressed = 2;  // Right button (AP MODE)
            }
        }
        
        if (buttonPressed > 0) {
            state.isPressed = true;
            state.buttonNumber = buttonPressed;
            
            // First touch on this button
            if (!wasTouched || lastButton != buttonPressed) {
                Serial.printf("[M5PAPER] Button %d pressed at (%d,%d)\n", buttonPressed, finger.x, finger.y);
                wasTouched = true;
                lastButton = buttonPressed;
                touchStartTime = millis();
            }
        }
    }
    
    return state;
}
