#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>

// Page definitions
enum Page {
    PAGE_LOGO = 0,
    PAGE_MINING = 1,
    PAGE_SETUP = 2,
    PAGE_COUNT = 3
};

// Display initialization and management functions
void display_init(void);

// Status bar (shared across all pages)
#define STATUS_BAR_HEIGHT 45
void display_draw_status_bar(bool wifiConnected, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin);

// Page rendering functions
void display_page_logo(bool wifiConnected, const char* timeStr, bool miningActive, bool soloMode, bool isDuinoCoin);
void display_page_mining(bool miningActive, bool wifiConnected, const char* timeStr, bool isSoloMode, bool isDuinoCoin);
void display_page_setup(bool wifiEnabled, bool wifiConnected, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin);

// Page navigation
void display_refresh_logo_colors(void);
void display_reset_animation(void);

// Low-level drawing functions
void fillScreen(uint16_t color);
void drawPixel(int x, int y, uint16_t color);
void pushFramebuffer(void);

// Shape drawing functions
void drawRect(int x, int y, int w, int h, uint16_t color);
void fillRect(int x, int y, int w, int h, uint16_t color);
void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color, int strokeWidth);

// Text rendering functions
void drawChar(int x, int y, char c, uint16_t color, int scale);
void drawText(const char* text, int x, int y, uint16_t color, int scale, bool centerX);

#endif // DISPLAY_H
