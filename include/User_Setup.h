// Setup for LilyGo T-Display S3 AMOLED with RM67162 driver
#ifndef USER_SETUP_H
#define USER_SETUP_H

#define USER_SETUP_ID 999
#define USER_SETUP_INFO "T-Display_S3_AMOLED"

// Define display dimensions BEFORE including TFT_eSPI
#define TFT_WIDTH  240
#define TFT_HEIGHT 536

// Use ST7735 as a dummy driver - but disable its init
#define ST7735_DRIVER
#define TFT_RGB_ORDER TFT_RGB  

// Disable standard commands/init
#define CGRAM_OFFSET

// Pin definitions (dummy - we use rm67162 library)
#define TFT_MISO -1
#define TFT_MOSI -1
#define TFT_SCLK -1
#define TFT_CS   -1
#define TFT_DC   -1
#define TFT_RST  -1

// Suppress warnings
#define DISABLE_ALL_LIBRARY_WARNINGS

// Load fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

#define SMOOTH_FONT

// SPI frequency (not actually used but required by TFT_eSPI)
#define SPI_FREQUENCY  20000000

#endif
