#ifndef EINK_DRIVER_H
#define EINK_DRIVER_H

#include <Arduino.h>
#include <SPI.h>

// M5Paper pin definitions
#define EINK_CS_PIN    15
#define EINK_SCK_PIN   14
#define EINK_MOSI_PIN  12
#define EINK_MISO_PIN  13
#define EINK_BUSY_PIN  27
#define EINK_RST_PIN   23

// M5Paper display specs
#define EINK_WIDTH  960
#define EINK_HEIGHT 540

// Simple grayscale levels (4-bit = 16 levels)
#define EINK_BLACK      0x00
#define EINK_DARK_GRAY  0x05
#define EINK_GRAY       0x0A
#define EINK_LIGHT_GRAY 0x0C
#define EINK_WHITE      0x0F

class EinkDriver {
public:
    EinkDriver();
    
    // Basic initialization
    bool begin();
    
    // Display control
    void clear(uint8_t color = EINK_WHITE);
    void update();
    void sleep();
    void wakeup();
    
    // Drawing primitives
    void drawPixel(int16_t x, int16_t y, uint8_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color);
    
    // Text rendering (simple)
    void drawChar(int16_t x, int16_t y, char c, uint8_t color, uint8_t size = 1);
    void drawString(int16_t x, int16_t y, const char* str, uint8_t color, uint8_t size = 1);
    
    // Get dimensions
    int16_t width() { return EINK_WIDTH; }
    int16_t height() { return EINK_HEIGHT; }
    
private:
    uint8_t* framebuffer;
    bool initialized;
    uint16_t devInfo[20];  // Device info from IT8951
    
    // IT8951 commands
    void writeCommand(uint16_t cmd);
    void writeData(uint16_t data);
    uint16_t readData();
    void waitBusy();
    void writeWords(const uint16_t* buf, uint32_t len);
    
    // IT8951 initialization and control
    void it8951GetSystemInfo();
    void it8951SystemRun();
    void it8951StandBy();
    void it8951Sleep();
    void it8951WriteReg(uint16_t reg, uint16_t val);
    uint16_t it8951ReadReg(uint16_t reg);
    
    // IT8951 image loading
    void it8951LoadImageStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void it8951LoadImageEnd();
    void it8951DisplayArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t mode);
    
    // Memory addresses
    uint32_t imgBufAddr;
    
    // SPI communication
    void spiWrite(uint8_t data);
    uint8_t spiRead();
    
    // Simple font (5x7 for each character)
    static const uint8_t font5x7[][5];
};

#endif // EINK_DRIVER_H
