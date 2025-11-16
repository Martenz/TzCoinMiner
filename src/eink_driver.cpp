#include "eink_driver.h"
#include "esp_task_wdt.h"

// Simple 5x7 ASCII font (32-127)
const uint8_t EinkDriver::font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
};

// IT8951 register addresses
#define IT8951_TCON_SYS_RUN      0x0001
#define IT8951_TCON_STANDBY      0x0002
#define IT8951_TCON_SLEEP        0x0003
#define IT8951_TCON_REG_WR       0x0011
#define IT8951_TCON_REG_RD       0x0010
#define IT8951_TCON_LD_IMG       0x0020
#define IT8951_TCON_LD_IMG_AREA  0x0021
#define IT8951_TCON_LD_IMG_END   0x0022

// IT8951 display modes
#define IT8951_MODE_INIT         0
#define IT8951_MODE_GC16         2
#define IT8951_MODE_A2           6

// Register addresses
#define DISPLAY_REG_BASE         0x1000
#define SYS_REG_BASE             0x0000

#define PREAMBLE                 0x6000

EinkDriver::EinkDriver() : framebuffer(nullptr), initialized(false), imgBufAddr(0) {
}

bool EinkDriver::begin() {
    Serial.println("[E-INK] Initializing minimal E-ink driver...");
    
    // Allocate framebuffer (4-bit grayscale = WIDTH * HEIGHT / 2 bytes)
    size_t bufferSize = (EINK_WIDTH * EINK_HEIGHT) / 2;
    framebuffer = (uint8_t*)ps_malloc(bufferSize);
    
    if (!framebuffer) {
        Serial.println("[E-INK] ERROR: Failed to allocate framebuffer in PSRAM!");
        return false;
    }
    
    Serial.printf("[E-INK] Framebuffer allocated: %d bytes in PSRAM\n", bufferSize);
    
    // Initialize pins
    pinMode(EINK_CS_PIN, OUTPUT);
    pinMode(EINK_RST_PIN, OUTPUT);
    pinMode(EINK_BUSY_PIN, INPUT);
    
    digitalWrite(EINK_CS_PIN, HIGH);
    digitalWrite(EINK_RST_PIN, HIGH);
    
    // Initialize SPI
    SPI.begin(EINK_SCK_PIN, EINK_MISO_PIN, EINK_MOSI_PIN, EINK_CS_PIN);
    SPI.setFrequency(4000000); // 4MHz
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    
    // Reset display
    digitalWrite(EINK_RST_PIN, LOW);
    delay(100);
    digitalWrite(EINK_RST_PIN, HIGH);
    delay(100);
    
    waitBusy();
    
    // Initialize IT8951 and get system info
    it8951SystemRun();
    delay(100);
    
    it8951GetSystemInfo();
    delay(100);
    
    Serial.printf("[E-INK] IT8951 Panel: %dx%d\n", EINK_WIDTH, EINK_HEIGHT);
    Serial.printf("[E-INK] Image buffer addr: 0x%08X\n", imgBufAddr);
    
    // Clear framebuffer
    memset(framebuffer, 0xFF, bufferSize); // Fill with white
    
    initialized = true;
    Serial.println("[E-INK] IT8951 driver initialized successfully");
    
    return true;
}

void EinkDriver::waitBusy() {
    unsigned long start = millis();
    while (digitalRead(EINK_BUSY_PIN) == HIGH) {
        if (millis() - start > 5000) {
            Serial.println("[E-INK] WARNING: Busy timeout!");
            break;
        }
        delay(10);
    }
}

void EinkDriver::spiWrite(uint8_t data) {
    digitalWrite(EINK_CS_PIN, LOW);
    SPI.transfer(data);
    digitalWrite(EINK_CS_PIN, HIGH);
}

uint8_t EinkDriver::spiRead() {
    digitalWrite(EINK_CS_PIN, LOW);
    uint8_t data = SPI.transfer(0x00);
    digitalWrite(EINK_CS_PIN, HIGH);
    return data;
}

void EinkDriver::clear(uint8_t color) {
    if (!framebuffer) return;
    
    uint8_t fillByte = (color << 4) | color;
    size_t bufferSize = (EINK_WIDTH * EINK_HEIGHT) / 2;
    memset(framebuffer, fillByte, bufferSize);
}

void EinkDriver::update() {
    if (!initialized) return;
    
    Serial.println("[E-INK] Updating display...");
    
    // Temporarily disable watchdog for this task
    esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
    
    // Load image area (full screen)
    it8951LoadImageStart(0, 0, EINK_WIDTH, EINK_HEIGHT);
    
    // Convert 4-bit framebuffer to 8-bit grayscale for IT8951
    // IT8951 expects 8-bit grayscale, we have 4-bit
    // Send in chunks to avoid blocking too long
    const int CHUNK_SIZE = 1024;  // Process 1KB at a time
    int totalPixels = (EINK_WIDTH * EINK_HEIGHT) / 2;
    
    for (int i = 0; i < totalPixels; i++) {
        uint8_t pixels = framebuffer[i];
        uint8_t pixel1 = (pixels >> 4) & 0x0F;  // High nibble
        uint8_t pixel2 = pixels & 0x0F;          // Low nibble
        
        // Convert 4-bit to 8-bit (0-15 to 0-255)
        pixel1 = pixel1 * 17;  // 0x0 -> 0x00, 0xF -> 0xFF
        pixel2 = pixel2 * 17;
        
        // Send as 16-bit words (2 pixels per word)
        uint16_t word = (pixel1 << 8) | pixel2;
        writeData(word);
        
        // Yield periodically
        if (i % CHUNK_SIZE == 0) {
            yield();
        }
    }
    
    it8951LoadImageEnd();
    
    // Display the area with GC16 mode (high quality)
    it8951DisplayArea(0, 0, EINK_WIDTH, EINK_HEIGHT, IT8951_MODE_GC16);
    
    // Re-enable watchdog
    esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0));
    
    Serial.println("[E-INK] Display updated");
}

void EinkDriver::sleep() {
    it8951Sleep();
}

void EinkDriver::wakeup() {
    it8951SystemRun();
}

void EinkDriver::drawPixel(int16_t x, int16_t y, uint8_t color) {
    if (!framebuffer || x < 0 || x >= EINK_WIDTH || y < 0 || y >= EINK_HEIGHT) {
        return;
    }
    
    // Calculate position in framebuffer (4-bit per pixel)
    size_t pos = (y * EINK_WIDTH + x) / 2;
    bool isOdd = (x % 2) == 1;
    
    if (isOdd) {
        framebuffer[pos] = (framebuffer[pos] & 0xF0) | (color & 0x0F);
    } else {
        framebuffer[pos] = (framebuffer[pos] & 0x0F) | ((color & 0x0F) << 4);
    }
}

void EinkDriver::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            drawPixel(x + i, y + j, color);
        }
    }
}

void EinkDriver::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
    // Top and bottom
    for (int16_t i = 0; i < w; i++) {
        drawPixel(x + i, y, color);
        drawPixel(x + i, y + h - 1, color);
    }
    // Left and right
    for (int16_t i = 0; i < h; i++) {
        drawPixel(x, y + i, color);
        drawPixel(x + w - 1, y + i, color);
    }
}

void EinkDriver::drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t size) {
    if (c < 32 || c > 90) return; // Only printable ASCII
    
    const uint8_t* glyph = font5x7[c - 32];
    
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t line = glyph[i];
        for (uint8_t j = 0; j < 7; j++) {
            if (line & 0x01) {
                if (size == 1) {
                    drawPixel(x + i, y + j, color);
                } else {
                    fillRect(x + i * size, y + j * size, size, size, color);
                }
            }
            line >>= 1;
        }
    }
}

void EinkDriver::drawString(int16_t x, int16_t y, const char* str, uint8_t color, uint8_t size) {
    int16_t cursorX = x;
    
    while (*str) {
        if (*str == '\n') {
            cursorX = x;
            y += 8 * size;
        } else {
            drawChar(cursorX, y, *str, color, size);
            cursorX += 6 * size;
        }
        str++;
    }
}

// IT8951 command implementations
void EinkDriver::writeCommand(uint16_t cmd) {
    digitalWrite(EINK_CS_PIN, LOW);
    SPI.write16(PREAMBLE);
    SPI.write16(cmd);
    digitalWrite(EINK_CS_PIN, HIGH);
}

void EinkDriver::writeData(uint16_t data) {
    digitalWrite(EINK_CS_PIN, LOW);
    SPI.write16(PREAMBLE | 0x0100);
    SPI.write16(data);
    digitalWrite(EINK_CS_PIN, HIGH);
}

uint16_t EinkDriver::readData() {
    digitalWrite(EINK_CS_PIN, LOW);
    SPI.write16(PREAMBLE | 0x1000);
    SPI.write16(0);  // Dummy write
    uint16_t data = SPI.transfer16(0);
    digitalWrite(EINK_CS_PIN, HIGH);
    return data;
}

void EinkDriver::writeWords(const uint16_t* buf, uint32_t len) {
    digitalWrite(EINK_CS_PIN, LOW);
    SPI.write16(PREAMBLE | 0x0100);  // Write data preamble
    for (uint32_t i = 0; i < len; i++) {
        SPI.write16(buf[i]);
    }
    digitalWrite(EINK_CS_PIN, HIGH);
}

void EinkDriver::it8951GetSystemInfo() {
    writeCommand(0x0302);  // Get system info command
    waitBusy();
    
    // Read device info
    digitalWrite(EINK_CS_PIN, LOW);
    SPI.write16(PREAMBLE | 0x1000);  // Read data preamble
    SPI.write16(0);  // Dummy
    
    for (int i = 0; i < 20; i++) {
        devInfo[i] = SPI.transfer16(0);
    }
    digitalWrite(EINK_CS_PIN, HIGH);
    
    // Extract image buffer address (words 3 and 4 form the 32-bit address)
    imgBufAddr = ((uint32_t)devInfo[3] << 16) | devInfo[4];
    
    // If address is 0, use default M5Paper address
    if (imgBufAddr == 0) {
        imgBufAddr = 0x001236E0;  // Default M5Paper image buffer address
        Serial.println("[E-INK] Using default image buffer address");
    }
}

void EinkDriver::it8951SystemRun() {
    writeCommand(IT8951_TCON_SYS_RUN);
}

void EinkDriver::it8951StandBy() {
    writeCommand(IT8951_TCON_STANDBY);
}

void EinkDriver::it8951Sleep() {
    writeCommand(IT8951_TCON_SLEEP);
}

void EinkDriver::it8951WriteReg(uint16_t reg, uint16_t val) {
    writeCommand(IT8951_TCON_REG_WR);
    writeData(reg);
    writeData(val);
}

uint16_t EinkDriver::it8951ReadReg(uint16_t reg) {
    writeCommand(IT8951_TCON_REG_RD);
    writeData(reg);
    return readData();
}

void EinkDriver::it8951LoadImageStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    writeCommand(IT8951_TCON_LD_IMG_AREA);
    writeData((imgBufAddr >> 16) & 0xFFFF);  // Address high
    writeData(imgBufAddr & 0xFFFF);           // Address low
    writeData(x);
    writeData(y);
    writeData(w);
    writeData(h);
}

void EinkDriver::it8951LoadImageEnd() {
    writeCommand(IT8951_TCON_LD_IMG_END);
}

void EinkDriver::it8951DisplayArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t mode) {
    writeCommand(0x0034);  // Display area command
    writeData(x);
    writeData(y);
    writeData(w);
    writeData(h);
    writeData(mode);
    waitBusy();
}
