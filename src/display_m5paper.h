#ifndef DISPLAY_M5PAPER_H
#define DISPLAY_M5PAPER_H

#include <Arduino.h>

// Page definitions
enum Page_M5Paper {
    PAGE_LOGO_M5 = 0,
    PAGE_MINING_M5 = 1,
    PAGE_SETUP_M5 = 2,
    PAGE_COUNT_M5 = 3
};

// Touch state structure
struct TouchState {
    bool isPressed;      // Currently touching
    bool justReleased;   // Just lifted finger
    int buttonNumber;    // Which button (0=none, 1=left, 2=middle, 3=right)
    int x, y;           // Touch coordinates
};

// Display initialization and management functions for M5Paper
void display_m5paper_init(void);

// Page rendering functions
void display_m5paper_page_logo(bool wifiConnected, const char* timeStr, bool miningActive, bool soloMode, bool isDuinoCoin);
void display_m5paper_page_mining(bool miningActive, bool wifiConnected, const char* timeStr, bool isSoloMode, bool isDuinoCoin);
void display_m5paper_page_setup(bool wifiEnabled, bool wifiConnected, bool isApMode, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin);

// Status bar (shared across all pages)
#define STATUS_BAR_HEIGHT_M5 50
void display_m5paper_draw_status_bar(bool wifiConnected, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin);

// Page navigation
void display_m5paper_refresh(void);
void display_m5paper_clear(void);

// Touch handling
TouchState display_m5paper_check_touch(int current_page_num);

// Power management
void display_m5paper_sleep(void);
void display_m5paper_wakeup(void);

#endif // DISPLAY_M5PAPER_H
