// USER DEFINED SETTINGS for LilyGo T-Display S3 AMOLED
#define USER_SETUP_INFO "LilyGo_T_Display_S3_AMOLED"

// Driver for RM67162 AMOLED display
#define USER_SETUP_LOADED

// Display size
#define TFT_WIDTH  240
#define TFT_HEIGHT 536

// We don't use TFT_eSPI driver directly, we use rm67162 library
// But we need these defines for TFT_eSPI to compile
#define DISABLE_ALL_LIBRARY_WARNINGS

// Dummy driver to satisfy TFT_eSPI
#define ST7735_DRIVER

// Font loading
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH, 96 characters
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH, 96 characters
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH, only characters 1234567890:-.apm
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH, only characters 1234567890:-.
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs ~3256 bytes in FLASH, only characters 1234567890:-.
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48 and custom fonts

#define SMOOTH_FONT

// SPI frequency
#define SPI_FREQUENCY  27000000

// Optional reduced SPI frequency for reading TFT
#define SPI_READ_FREQUENCY  20000000

// Comment out the #defines below if you want to remove that font to save FLASH memory
#define GFXFF 1
