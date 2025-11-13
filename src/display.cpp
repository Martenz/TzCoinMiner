#include "display.h"
#include "mining_task.h"
#include "duino_task.h"
#include "wifi_config.h"
#include <rm67162.h>
#include "display_assets.h"

// Frame buffer for the entire display
uint16_t *framebuffer;

// Current color pair index for logo page
static int currentColorPairIndex = 0;

// Animation state for logo page scrolling text
static unsigned long lastActivityTime = 0;
static bool showingAnimation = false;
static bool animationCompleted = false;  // Track if animation played once
static int scrollOffset = 0;
static unsigned long lastScrollUpdate = 0;
static unsigned long animationStartTime = 0;  // Track when animation started
#define INACTIVITY_TIMEOUT 5000  // 5 seconds
#define ANIMATION_START_DELAY 1000  // 1 second delay before text appears (stars only)
#define SCROLL_SPEED 2  // pixels per frame (reduced from 8 for slower 4-second animation)
#define SCROLL_FRAME_DELAY 20  // 20ms per frame (~50fps)

// Star field for animation background
#define NUM_STARS 50
static int starX[NUM_STARS];
static int starY[NUM_STARS];
static bool starsInitialized = false;

void display_init(void)
{
    Serial.println("=================================");
    Serial.println("   Initializing Display...      ");
    Serial.println("=================================");
    
    // Allocate frame buffer
    framebuffer = (uint16_t*)malloc(WIDTH * HEIGHT * sizeof(uint16_t));
    if (!framebuffer) {
        Serial.println("ERROR: Failed to allocate framebuffer!");
        return;
    }
    
    // Initialize the AMOLED display
    rm67162_init();
    lcd_setRotation(1);  // Landscape mode
    
    // Initialize random color pair
    uint32_t randomValue = esp_random();
    currentColorPairIndex = randomValue % numColorPairs;
    
    // Initialize animation state
    lastActivityTime = millis();
    showingAnimation = false;
    animationCompleted = false;
    scrollOffset = 0;
    
    Serial.println("Display initialized successfully");
}

void fillScreen(uint16_t color)
{
    for(int i = 0; i < WIDTH * HEIGHT; i++) {
        framebuffer[i] = color;
    }
}

void drawPixel(int x, int y, uint16_t color)
{
    if(x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) {
        framebuffer[y * WIDTH + x] = color;
    }
}

void pushFramebuffer()
{
    lcd_PushColors(0, 0, WIDTH, HEIGHT, framebuffer);
}

// Draw a rectangle outline
void drawRect(int x, int y, int w, int h, uint16_t color)
{
    // Top and bottom lines
    for(int i = 0; i < w; i++) {
        drawPixel(x + i, y, color);
        drawPixel(x + i, y + h - 1, color);
    }
    // Left and right lines
    for(int i = 0; i < h; i++) {
        drawPixel(x, y + i, color);
        drawPixel(x + w - 1, y + i, color);
    }
}

// Draw a filled rectangle
void fillRect(int x, int y, int w, int h, uint16_t color)
{
    for(int j = 0; j < h; j++) {
        for(int i = 0; i < w; i++) {
            drawPixel(x + i, y + j, color);
        }
    }
}

// Draw a rounded rectangle with stroke
void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color, int strokeWidth)
{
    // Draw multiple strokes for thickness
    for(int s = 0; s < strokeWidth; s++) {
        int xo = x + s;
        int yo = y + s;
        int wo = w - 2 * s;
        int ho = h - 2 * s;
        int ro = r - s;
        if (ro < 0) ro = 0;
        
        // Draw straight edges
        // Top
        for(int i = xo + ro; i < xo + wo - ro; i++) {
            drawPixel(i, yo, color);
        }
        // Bottom
        for(int i = xo + ro; i < xo + wo - ro; i++) {
            drawPixel(i, yo + ho - 1, color);
        }
        // Left
        for(int i = yo + ro; i < yo + ho - ro; i++) {
            drawPixel(xo, i, color);
        }
        // Right
        for(int i = yo + ro; i < yo + ho - ro; i++) {
            drawPixel(xo + wo - 1, i, color);
        }
        
        // Draw corners using Bresenham circle algorithm
        int f = 1 - ro;
        int ddF_x = 1;
        int ddF_y = -2 * ro;
        int px = 0;
        int py = ro;
        
        while(px < py) {
            if(f >= 0) {
                py--;
                ddF_y += 2;
                f += ddF_y;
            }
            px++;
            ddF_x += 2;
            f += ddF_x;
            
            // Top-left
            drawPixel(xo + ro - px, yo + ro - py, color);
            drawPixel(xo + ro - py, yo + ro - px, color);
            // Top-right
            drawPixel(xo + wo - ro + px - 1, yo + ro - py, color);
            drawPixel(xo + wo - ro + py - 1, yo + ro - px, color);
            // Bottom-left
            drawPixel(xo + ro - px, yo + ho - ro + py - 1, color);
            drawPixel(xo + ro - py, yo + ho - ro + px - 1, color);
            // Bottom-right
            drawPixel(xo + wo - ro + px - 1, yo + ho - ro + py - 1, color);
            drawPixel(xo + wo - ro + py - 1, yo + ho - ro + px - 1, color);
        }
    }
}

// Draw a character at position (x, y) with specified color and scale
void drawChar(int x, int y, char c, uint16_t color, int scale)
{
    const uint8_t* bitmap = getCharBitmap(c);
    
    for(int row = 0; row < 8; row++) {
        uint8_t line = bitmap[row];
        for(int col = 0; col < 8; col++) {
            // Reverse bit order to fix mirroring: read from bit 7 down to bit 0
            if(line & (1 << (7 - col))) {
                // Draw scaled pixel
                for(int sy = 0; sy < scale; sy++) {
                    for(int sx = 0; sx < scale; sx++) {
                        drawPixel(x + col * scale + sx, y + row * scale + sy, color);
                    }
                }
            }
        }
    }
}

// Draw text string with specified scale (centered if centerX is true)
void drawText(const char* text, int x, int y, uint16_t color, int scale, bool centerX)
{
    int len = strlen(text);
    int totalWidth = len * 8 * scale;
    
    int startX = centerX ? (WIDTH - totalWidth) / 2 : x;
    
    for(int i = 0; i < len; i++) {
        drawChar(startX + i * 8 * scale, y, text[i], color, scale);
    }
}

// Refresh logo colors with a new random pair
void display_refresh_logo_colors(void)
{
    // Reset activity timer (user pressed button)
    lastActivityTime = millis();
    showingAnimation = false;
    animationCompleted = false;  // Allow animation to play again
    starsInitialized = false;  // Reset stars when stopping animation
    
    // Ensure we pick a different color pair than the current one
    int oldIndex = currentColorPairIndex;
    do {
        uint32_t randomValue = esp_random();
        currentColorPairIndex = randomValue % numColorPairs;
    } while (currentColorPairIndex == oldIndex && numColorPairs > 1);
    
    Serial.printf("Color pair changed: #%d -> #%d (animation reset)\n", oldIndex, currentColorPairIndex);
}

// Reset animation state (called when page changes)
void display_reset_animation(void)
{
    lastActivityTime = millis();
    showingAnimation = false;
    animationCompleted = false;  // Allow animation to play when returning to page
    scrollOffset = 0;
    starsInitialized = false;  // Reset stars when animation stops
}

// Draw status bar (shared across all pages)
void display_draw_status_bar(bool wifiConnected, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin)
{
    // Darker grey for status bar - RGB565: R=4, G=8, B=4 -> 0x2104 (needs byte swap)
    uint16_t darkGrey = 0x0421;  // Swapped from 0x2104 - very dark neutral grey
    
    int cornerRadius = 12;
    
    // Fill the main rectangular area (excluding corners)
    fillRect(0, 0, WIDTH, STATUS_BAR_HEIGHT - cornerRadius, darkGrey);
    
    // Fill the bottom part with corners
    for (int y = STATUS_BAR_HEIGHT - cornerRadius; y < STATUS_BAR_HEIGHT; y++) {
        int rowY = y - (STATUS_BAR_HEIGHT - cornerRadius);
        int cutoff = cornerRadius - (int)sqrt(cornerRadius * cornerRadius - rowY * rowY);
        
        // Left side - from cutoff to full width minus right cutoff
        fillRect(cutoff, y, WIDTH - 2 * cutoff, 1, darkGrey);
    }
    
    // Draw WiFi icon in top right (16x16 icon with some padding)
    int iconX = WIDTH - 16 - 10;  // 10px from right edge
    int iconY = (STATUS_BAR_HEIGHT - 16) / 2;  // Centered vertically
    
    const uint8_t* wifiIcon = wifiConnected ? 
        WifiIcons::CONNECTED_16x16 : WifiIcons::DISCONNECTED_16x16;
    
    // RGB565 with byte swapping: Green 0x07E0 -> 0xE007, Red 0xF800 -> 0x00F8
    uint16_t iconColor = wifiConnected ? 0xE007 : 0x00F8;  // Green if connected, red if not
    
    // Draw 16x16 WiFi icon
    for (int row = 0; row < 16; row++) {
        uint8_t byte1 = wifiIcon[row * 2];
        uint8_t byte2 = wifiIcon[row * 2 + 1];
        uint16_t rowData = (byte1 << 8) | byte2;
        
        for (int col = 0; col < 16; col++) {
            if (rowData & (1 << (15 - col))) {
                drawPixel(iconX + col, iconY + row, iconColor);
            }
        }
    }
    
    // Draw mining icon next to WiFi icon if mining is active
    if (miningActive) {
        int miningIconX = iconX - 16 - 8;  // 8px spacing from WiFi icon
        int miningIconY = iconY;
        
        uint16_t miningColor = 0xE007;  // Green like connected WiFi
        
        // Draw 16x16 mining pickaxe icon
        for (int row = 0; row < 16; row++) {
            uint8_t byte1 = MiningIcons::PICKAXE_16x16[row * 2];
            uint8_t byte2 = MiningIcons::PICKAXE_16x16[row * 2 + 1];
            uint16_t rowData = (byte1 << 8) | byte2;
            
            for (int col = 0; col < 16; col++) {
                if (rowData & (1 << (15 - col))) {
                    drawPixel(miningIconX + col, miningIconY + row, miningColor);
                }
            }
        }
        
        // Draw coin circle to the left of mining icon
        // Circle should have diameter 16px (same as icons) with radius 8
        int radius = 8;
        int coinDiameter = radius * 2;  // 16px
        int coinX = miningIconX - coinDiameter - 8;  // 16px for circle diameter + 8px padding from mining icon
        int coinY = miningIconY + 8;  // Center vertically with 16px icons (8px from top)
        
        // Determine color: black normally, orange if block/share found
        uint16_t coinColor;
        bool foundReward = isDuinoCoin ? duino_has_found_share() : mining_has_found_block();
        
        if (foundReward) {
            coinColor = 0x07FD;  // Orange (RGB565: 11111 100000 11101) byte-swapped
        } else {
            coinColor = 0x0000;  // Black
        }
        
        // Draw filled circle (coin) with radius 8 (diameter 16px)
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy <= radius*radius) {
                    drawPixel(coinX + dx, coinY + dy, coinColor);
                }
            }
        }
        
        // Draw "S" (Solo) or "P" (Pool) to the left of coin - only for Bitcoin
        if (!isDuinoCoin) {
            int modeX = coinX - radius - 16 - 8;  // Move left: radius (8px) + character width (16px) + 8px padding
            int modeY = miningIconY;
            uint16_t modeColor = 0xE007;  // Green like WiFi connected
            
            // Use the passed isSoloMode parameter instead of config
            const char* modeChar = isSoloMode ? "S" : "P";
            
            drawText(modeChar, modeX, modeY, modeColor, 2, false);  // Scale 2 (16x16) to match icons
        }
    }
    
    // Draw time on the left side if WiFi is connected
    if (wifiConnected && timeStr != nullptr) {
        int timeScale = 2;  // Small text for time
        int timeHeight = 8 * timeScale;
        int timeX = 10;  // 10px from left edge
        int timeY = (STATUS_BAR_HEIGHT - timeHeight) / 2;  // Centered vertically
        
        // Draw time in pure white with byte swap (0xFFFF -> 0xFFFF, no change needed)
        uint16_t white = 0xFFFF;
        
        for (int i = 0; timeStr[i] != '\0'; i++) {
            drawChar(timeX + i * 8 * timeScale, timeY, timeStr[i], white, timeScale);
        }
    }
}

// Initialize star field for animation
void initStarField()
{
    for (int i = 0; i < NUM_STARS; i++) {
        starX[i] = esp_random() % WIDTH;
        starY[i] = STATUS_BAR_HEIGHT + (esp_random() % (HEIGHT - STATUS_BAR_HEIGHT));
    }
    starsInitialized = true;
}

// Draw star field background
void drawStarField()
{
    if (!starsInitialized) {
        initStarField();
    }
    
    // Draw white dots for stars
    uint16_t white = COLOR_WHITE;
    for (int i = 0; i < NUM_STARS; i++) {
        // Draw a small 2x2 star for better visibility
        drawPixel(starX[i], starY[i], white);
        drawPixel(starX[i] + 1, starY[i], white);
        drawPixel(starX[i], starY[i] + 1, white);
        drawPixel(starX[i] + 1, starY[i] + 1, white);
    }
}

// Draw scrolling "The answer is 42" animation
void drawScrollingAnswer(int yOffset, bool showText)
{
    // Clear screen below status bar
    fillRect(0, STATUS_BAR_HEIGHT, WIDTH, HEIGHT - STATUS_BAR_HEIGHT, COLOR_BLACK);
    
    // Draw star field background (always visible)
    drawStarField();
    
    // Only draw text if delay has passed
    if (!showText) {
        return;  // Just show stars for the first second
    }
    
    // Use current color pair
    uint16_t color1 = colorPairs[currentColorPairIndex][0];
    uint16_t color2 = colorPairs[currentColorPairIndex][1];
    
    // Text scale
    int scale1 = 4;  // "The answer is"
    int scale2 = 8;  // "42"
    
    int lineHeight1 = 8 * scale1;
    int lineHeight2 = 8 * scale2;
    int spacing = 30;
    
    // Calculate total height of the text block
    int totalTextHeight = lineHeight1 + spacing + lineHeight2;
    
    // Starting position: below the visible area
    // The text starts at HEIGHT and scrolls up to -totalTextHeight
    int availableHeight = HEIGHT - STATUS_BAR_HEIGHT;
    int startY = HEIGHT + yOffset;  // Start from bottom of screen
    
    int line1Y = startY;
    int line2Y = startY + lineHeight1 + spacing;
    
    // Draw text first (will be clipped later if needed)
    // Only draw if at least partially visible
    if (line1Y < HEIGHT && (line1Y + lineHeight1) > STATUS_BAR_HEIGHT) {
        drawText("The answer is", 0, line1Y, color1, scale1, true);
    }
    
    if (line2Y < HEIGHT && (line2Y + lineHeight2) > STATUS_BAR_HEIGHT) {
        drawText("42", 0, line2Y, color2, scale2, true);
    }
    
    // Clip any text that goes into status bar area by drawing black rectangle
    // This creates the "sliding under" effect
    if (line1Y < STATUS_BAR_HEIGHT || line2Y < STATUS_BAR_HEIGHT) {
        fillRect(0, 0, WIDTH, STATUS_BAR_HEIGHT, COLOR_BLACK);
    }
}

// Display the logo page
void display_page_logo(bool wifiConnected, const char* timeStr, bool miningActive, bool soloMode, bool isDuinoCoin)
{
    unsigned long currentTime = millis();
    
    // Check for inactivity to trigger animation
    if (currentTime - lastActivityTime > INACTIVITY_TIMEOUT && !animationCompleted) {
        if (!showingAnimation) {
            // Start animation
            showingAnimation = true;
            scrollOffset = 0;
            animationStartTime = currentTime;  // Track when animation started
            lastScrollUpdate = currentTime;
        }
        
        // Calculate time elapsed since animation started
        unsigned long animationElapsed = currentTime - animationStartTime;
        
        // Only start scrolling text after initial delay (1 second of stars only)
        bool showText = (animationElapsed >= ANIMATION_START_DELAY);
        
        // Update scroll position only if delay has passed
        if (showText && (currentTime - lastScrollUpdate > SCROLL_FRAME_DELAY)) {
            scrollOffset -= SCROLL_SPEED;
            lastScrollUpdate = currentTime;
            
            // Calculate total animation height
            int scale1 = 4;
            int scale2 = 8;
            int lineHeight1 = 8 * scale1;
            int lineHeight2 = 8 * scale2;
            int spacing = 30;
            int totalTextHeight = lineHeight1 + spacing + lineHeight2;
            int availableHeight = HEIGHT - STATUS_BAR_HEIGHT;
            
            // Check if animation completed (text scrolled off top)
            // Text needs to travel: availableHeight (to appear) + totalTextHeight (to disappear)
            if (scrollOffset < -(availableHeight + totalTextHeight)) {
                // Animation completed - stop and show normal logo
                showingAnimation = false;
                animationCompleted = true;
                lastActivityTime = millis();  // Reset timer for next cycle
            }
        }
        
        // Draw animation
        fillScreen(COLOR_BLACK);
        drawScrollingAnswer(scrollOffset, showText);
        // Draw status bar LAST so it's always on top
        display_draw_status_bar(wifiConnected, timeStr, miningActive, soloMode, isDuinoCoin);
        pushFramebuffer();
        return;
    }
    
    // Reset animation flag after showing normal logo for a while
    if (animationCompleted && (currentTime - lastActivityTime > INACTIVITY_TIMEOUT)) {
        animationCompleted = false;  // Allow animation to play again
    }
    
    // Reset animation when activity detected
    showingAnimation = false;
    
    // Normal logo display
    // Clear to black
    fillScreen(COLOR_BLACK);
    
    // Draw status bar with mining status
    display_draw_status_bar(wifiConnected, timeStr, miningActive, soloMode, isDuinoCoin);
    
    uint16_t color1 = colorPairs[currentColorPairIndex][0];
    uint16_t color2 = colorPairs[currentColorPairIndex][1];
    
    // Text scale factor (larger for better visibility)
    int scale = 5;
    
    // Calculate vertical positions for centered text (below status bar)
    int availableHeight = HEIGHT - STATUS_BAR_HEIGHT;
    int lineHeight = 8 * scale;
    int spacing = 10;
    int totalHeight = lineHeight * 2 + spacing;
    
    // Center vertically in available space
    int startY = STATUS_BAR_HEIGHT + (availableHeight - totalHeight) / 2;
    int line1Y = startY;
    int line2Y = startY + lineHeight + spacing;
    
    // Draw "TzCoinMiner" centered
    drawText("TzCoinMiner", 0, line1Y, color1, scale, true);
    
    // Draw coin type and mode: "BTC SOLO/POOL", "BCH SOLO/POOL", or "DUCO POOL"
    char modeText[16];
    if (isDuinoCoin) {
        snprintf(modeText, sizeof(modeText), "DUCO POOL");
    } else {
        // Check if BCH is configured
        extern WifiConfig config;
        const char* coinType = config.useBitcoinCash ? "BCH" : "BTC";
        snprintf(modeText, sizeof(modeText), soloMode ? "%s SOLO" : "%s POOL", coinType);
    }
    drawText(modeText, 0, line2Y, color2, scale, true);
    
    // Push to display
    pushFramebuffer();
}

// Display the mining page
void display_page_mining(bool miningActive, bool wifiConnected, const char* timeStr, bool isSoloMode, bool isDuinoCoin)
{
    // Clear to black
    fillScreen(COLOR_BLACK);
    
    // Draw status bar with mining status and mode indicator
    display_draw_status_bar(wifiConnected, timeStr, miningActive, isSoloMode, isDuinoCoin);
    
    // Colors - RGB565 with corrected byte order
    // RGB565 format: RRRRR GGGGGG BBBBB (5 red, 6 green, 5 blue)
    // Orange: R=31, G=32, B=0 -> 0xFC20 (swapped from 0xFD20)
    uint16_t lightBlue = 0xF83C;  // Light blue (swapped from 0x3C1F)
    uint16_t orange = 0x20FC;     // Orange (swapped from 0xFD20)
    uint16_t darkOrange = 0x4031; // Very dark orange/brown (swapped from 0x3140) - 20% brightness
    uint16_t darkGrey = 0x0421;   // Dark grey (same as status bar)
    
    // Button dimensions - 1/3 of display width, 80% of remaining screen height
    // Aligned to left and bottom
    int btnWidth = WIDTH / 3;  // 536 / 3 ≈ 178px
    int btnHeight = (HEIGHT - STATUS_BAR_HEIGHT) * 0.8;  // (240 - 45) * 0.8 = 156px
    int btnX = 20;  // Small margin from left
    int btnY = HEIGHT - btnHeight - 10;  // 10px margin from bottom
    int cornerRadius = 10;
    int strokeWidth = 4;
    
    // Button color based on mining state
    uint16_t buttonColor = miningActive ? lightBlue : orange;
    
    // Draw button border (stroke color changes based on state, black background)
    drawRoundRect(btnX, btnY, btnWidth, btnHeight, cornerRadius, buttonColor, strokeWidth);
    
    // When mining is active, fill bottom 1/3 of button with dark orange/brown
    if (miningActive) {
        uint16_t fillColor = darkOrange;  // Dark orange/brown (swapped from 0xC400)
        int fillHeight = btnHeight / 3;
        int fillY = btnY + btnHeight - fillHeight - strokeWidth;  // Account for stroke width
        int fillX = btnX + strokeWidth;
        int fillWidth = btnWidth - (strokeWidth * 2);
        
        // Draw filled rectangle (respecting rounded corners at bottom)
        for (int y = fillY; y < btnY + btnHeight - strokeWidth; y++) {
            int yOffset = (btnY + btnHeight - strokeWidth) - y;
            int xMargin = 0;
            
            // Adjust for rounded corners in the bottom area
            if (yOffset < cornerRadius) {
                int cornerOffset = cornerRadius - yOffset;
                xMargin = cornerRadius - (int)sqrt(cornerRadius * cornerRadius - cornerOffset * cornerOffset);
            }
            
            for (int x = fillX + xMargin; x < fillX + fillWidth - xMargin; x++) {
                drawPixel(x, y, fillColor);
            }
        }
    }
    
    // Draw button text - different layout for single word vs two words
    int textScale = 2;
    
    if (miningActive) {
        // Single word "Mining" - centered in light blue
        const char* buttonText = "Mining";
        int textLen = strlen(buttonText);
        int textWidth = textLen * 8 * textScale;
        int textHeight = 8 * textScale;
        int textX = btnX + (btnWidth - textWidth) / 2;
        int textY = btnY + (btnHeight - textHeight) / 2;
        
        for(int i = 0; i < textLen; i++) {
            drawChar(textX + i * 8 * textScale, textY, buttonText[i], lightBlue, textScale);
        }
    } else {
        // Two rows: "Start" and "Mining" in orange
        const char* line1 = "Start";
        const char* line2 = "Mining";
        
        int line1Len = strlen(line1);
        int line2Len = strlen(line2);
        int line1Width = line1Len * 8 * textScale;
        int line2Width = line2Len * 8 * textScale;
        int lineHeight = 8 * textScale;
        int lineSpacing = 6;
        int totalHeight = lineHeight * 2 + lineSpacing;
        
        // Center both lines
        int line1X = btnX + (btnWidth - line1Width) / 2;
        int line2X = btnX + (btnWidth - line2Width) / 2;
        int startY = btnY + (btnHeight - totalHeight) / 2;
        int line1Y = startY;
        int line2Y = startY + lineHeight + lineSpacing;
        
        // Draw first line in orange
        for(int i = 0; i < line1Len; i++) {
            drawChar(line1X + i * 8 * textScale, line1Y, line1[i], orange, textScale);
        }
        
        // Draw second line in orange
        for(int i = 0; i < line2Len; i++) {
            drawChar(line2X + i * 8 * textScale, line2Y, line2[i], orange, textScale);
        }
    }
    
    // Status panel - to the right of the button
    int panelPadding = 20;  // Padding between button and panel
    int panelX = btnX + btnWidth + panelPadding;
    int panelWidth = WIDTH - panelX - 20;  // Fill remaining width with 20px margin from right
    int panelHeight = btnHeight;  // Same height as button
    int panelY = btnY;  // Same Y position as button
    
    // Draw status panel with dark grey stroke and black background
    drawRoundRect(panelX, panelY, panelWidth, panelHeight, cornerRadius, darkGrey, strokeWidth);
    
    // Status text - small font aligned to top
    int statusTextScale = 2;
    int statusTextPadding = 15;  // Padding from top and left edges of panel
    int statusTextX = panelX + statusTextPadding;
    int statusTextY = panelY + statusTextPadding;
    
    const char* statusText;
    if (!wifiConnected) {
        statusText = "Status: no wifi";
    } else {
        statusText = miningActive ? "Status: active" : "Status: inactive";
    }
    
    // Draw status text in white
    uint16_t white = 0xFFFF;
    drawText(statusText, statusTextX, statusTextY, white, statusTextScale, false);
    
    // If mining is active, show statistics below status text
    if (miningActive) {
        int statsY = statusTextY + (8 * statusTextScale) + 12;  // Below status text with 12px gap
        int statsScale = 2;  // Same scale as status text for better readability
        int lineSpacing = 18;  // Spacing between stat lines
        
        if (isDuinoCoin) {
            // Duino-Coin statistics
            DuinoStats ducoStats = duino_get_stats();
            
            // Line 1: Hash rate + Total hashes in millions
            char line1[32];
            float totalHashesM = ducoStats.total_hashes / 1000000.0;
            if (ducoStats.hashes_per_second >= 1000) {
                snprintf(line1, sizeof(line1), "H/s: %.1fK/%.1fM", 
                         ducoStats.hashes_per_second / 1000.0, totalHashesM);
            } else {
                snprintf(line1, sizeof(line1), "H/s: %u/%.1fM", 
                         ducoStats.hashes_per_second, totalHashesM);
            }
            drawText(line1, statusTextX, statsY, white, statsScale, false);
            
            // Line 2: Difficulty
            char line2[32];
            snprintf(line2, sizeof(line2), "diff: %.1f", ducoStats.difficulty);
            drawText(line2, statusTextX, statsY + lineSpacing, white, statsScale, false);
            
            // Line 3: Shares (accepted/rejected)
            char line3[32];
            snprintf(line3, sizeof(line3), "shares: %u/%u", ducoStats.shares_accepted, ducoStats.shares_rejected);
            drawText(line3, statusTextX, statsY + lineSpacing * 2, white, statsScale, false);
            
        } else {
            // Bitcoin statistics
            MiningStats stats = mining_get_stats();
            
            // Line 1: Hash rate + Total hashes in millions
            char line1[32];
            float totalHashesM = stats.total_hashes / 1000000.0;
            if (stats.hashes_per_second >= 1000) {
                snprintf(line1, sizeof(line1), "H/s: %.1fK/%.1fM", 
                         stats.hashes_per_second / 1000.0, totalHashesM);
            } else {
                snprintf(line1, sizeof(line1), "H/s: %u/%.1fM", 
                         stats.hashes_per_second, totalHashesM);
            }
            drawText(line1, statusTextX, statsY, white, statsScale, false);
            
            // Line 2: Best difficulty - show zeros (more intuitive) with difficulty in parentheses
            char line2[32];
            if (stats.best_difficulty_zeros > 0) {
                // Show zeros count with difficulty
                if (stats.best_difficulty < 1000.0) {
                    snprintf(line2, sizeof(line2), "best: %uz (%.0f)", 
                            stats.best_difficulty_zeros, stats.best_difficulty);
                } else {
                    snprintf(line2, sizeof(line2), "best: %uz", stats.best_difficulty_zeros);
                }
            } else {
                snprintf(line2, sizeof(line2), "best: 0z");
            }
            drawText(line2, statusTextX, statsY + lineSpacing, white, statsScale, false);
            
            // Line 3: Block height, pool difficulty, or demo
            char line3[32];
            if (stats.block_height > 0) {
                // Solo mode with real block height from RPC
                snprintf(line3, sizeof(line3), "block: %u", stats.block_height);
            } else if (!isSoloMode) {
                // Pool mode - show pool difficulty or working status
                if (stats.shares_accepted > 0 || stats.shares_rejected > 0) {
                    snprintf(line3, sizeof(line3), "pool: active");
                } else {
                    snprintf(line3, sizeof(line3), "pool: connecting");
                }
            } else {
                // Solo mode without block height (educational/demo)
                snprintf(line3, sizeof(line3), "demo: solo");
            }
            drawText(line3, statusTextX, statsY + lineSpacing * 2, white, statsScale, false);
            
            // Line 4: Shares (submitted/accepted) in K format with no decimals (rounded)
            char line4[32];
            if (!isSoloMode) {
                uint32_t submitted_k = (stats.shares_submitted + 500) / 1000;  // Round to nearest K
                uint32_t accepted_k = (stats.shares_accepted + 500) / 1000;
                snprintf(line4, sizeof(line4), "shares: %uK/%uK", submitted_k, accepted_k);
            } else {
                uint32_t accepted_k = (stats.shares_accepted + 500) / 1000;
                uint32_t rejected_k = (stats.shares_rejected + 500) / 1000;
                snprintf(line4, sizeof(line4), "shares: %uK/%uK", accepted_k, rejected_k);
            }
            drawText(line4, statusTextX, statsY + lineSpacing * 3, white, statsScale, false);
            
            // Line 5: Blocks found
            char line5[32];
            snprintf(line5, sizeof(line5), "found: %u", stats.blocks_found);
            drawText(line5, statusTextX, statsY + lineSpacing * 4, white, statsScale, false);
        }
    }
    
    // Draw coin overlay in orange at top of button
    const char* coinName;
    if (isDuinoCoin) {
        coinName = "DUCO";
    } else if (config.useBitcoinCash) {
        coinName = "BCH";
    } else {
        coinName = "BTC";
    }
    
    int coinScale = 3;
    int coinTextLen = strlen(coinName);
    int coinTextWidth = coinTextLen * 8 * coinScale;
    int coinPadding = 20;  // 20px padding from top
    int coinX = btnX + (btnWidth - coinTextWidth) / 2;  // Centered horizontally
    int coinY = btnY + coinPadding;  // 20px from top of button
    
    // Draw coin name in orange
    drawText(coinName, coinX, coinY, orange, coinScale, false);
    
    // If mining is active and in educational fallback mode, show "EDUCATIONAL" overlay
    if (miningActive && !isDuinoCoin && mining_is_educational_fallback()) {
        // Fuchsia/Magenta color: RGB565 byte swapped
        // Standard fuchsia RGB565: 0xF81F, byte swapped: 0x1FF8
        uint16_t fuchsia = 0x1FF8;
        
        const char* overlayText = "EDUCATIONAL";
        int overlayScale = 2;  // Larger text for better visibility
        int overlayTextLen = strlen(overlayText);
        int overlayTextWidth = overlayTextLen * 8 * overlayScale;
        int overlayTextHeight = 8 * overlayScale;
        
        // Position at bottom of mining button, centered
        int overlayX = btnX + (btnWidth - overlayTextWidth) / 2;
        int overlayY = btnY + btnHeight - overlayTextHeight - 8;  // 8px margin from bottom
        
        // Draw text in fuchsia
        for(int i = 0; i < overlayTextLen; i++) {
            drawChar(overlayX + i * 8 * overlayScale, overlayY, overlayText[i], fuchsia, overlayScale);
        }
    }
    
    // Push to display
    pushFramebuffer();
}

// Display the setup page
void display_page_setup(bool wifiEnabled, bool wifiConnected, const char* timeStr, bool miningActive, bool isSoloMode, bool isDuinoCoin)
{
    // Clear to black
    fillScreen(COLOR_BLACK);
    
    // Draw status bar with mining status
    display_draw_status_bar(wifiConnected, timeStr, miningActive, isSoloMode, isDuinoCoin);
    
    // Button dimensions - larger to fit text properly
    // Position at top below status bar with small padding
    int btnWidth = 320;
    int btnHeight = 80;
    int btnX = (WIDTH - btnWidth) / 2;
    int topPadding = 20;  // Small padding from status bar
    int btnY = STATUS_BAR_HEIGHT + topPadding;
    int cornerRadius = 12;
    int strokeWidth = 4;
    
    // Button colors based on WiFi state
    // Use light blue: RGB565 = 0x3C1F (R=56, G=128, B=248)
    uint16_t lightBlue = 0x3C1F;  // Light blue
    uint16_t bgColor = wifiEnabled ? lightBlue : COLOR_BLACK;
    uint16_t strokeColor = wifiEnabled ? COLOR_BLACK : lightBlue;
    uint16_t textColor = wifiEnabled ? COLOR_BLACK : lightBlue;
    const char* buttonText = wifiEnabled ? "WiFi ON" : "Configure";
    
    // Draw button background
    fillRect(btnX, btnY, btnWidth, btnHeight, bgColor);
    
    // Draw button border
    drawRoundRect(btnX, btnY, btnWidth, btnHeight, cornerRadius, strokeColor, strokeWidth);
    
    // Draw button text centered
    int textScale = 4;  // Larger text
    int textLen = strlen(buttonText);
    int textWidth = textLen * 8 * textScale;
    int textHeight = 8 * textScale;
    int textX = btnX + (btnWidth - textWidth) / 2;
    int textY = btnY + (btnHeight - textHeight) / 2;
    
    // Manual positioning since we can't use the centered version with custom X
    for(int i = 0; i < textLen; i++) {
        drawChar(textX + i * 8 * textScale, textY, buttonText[i], textColor, textScale);
    }
    
    // If WiFi AP is enabled, show connection info below button
    if (wifiEnabled) {
        int smallScale = 2;  // Smaller text for info
        int lineHeight = 8 * smallScale;
        int lineSpacing = 8;  // Space between lines
        int infoY = btnY + btnHeight + 15;  // 15px below button
        
        // SSID info
        const char* ssidLabel = "SSID: TzCoinMinerWifi";
        drawText(ssidLabel, 0, infoY, COLOR_WHITE, smallScale, true);
        
        // Password info - use simpler text with available characters
        const char* passLabel = "PWD: theansweris42";
        drawText(passLabel, 0, infoY + lineHeight + lineSpacing, COLOR_WHITE, smallScale, true);
    }
    
    // Push to display
    pushFramebuffer();
}
