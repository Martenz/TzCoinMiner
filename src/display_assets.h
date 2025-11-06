#ifndef DISPLAY_ASSETS_H
#define DISPLAY_ASSETS_H

#include <Arduino.h>

// Display dimensions
#define WIDTH 536
#define HEIGHT 240

// Simple color definitions (RGB565 - byte swapped for display)
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0x00F8  // Bright red (byte swapped from 0xF800)
#define COLOR_GREEN   0xE007  // Bright green (byte swapped from 0x07E0)
#define COLOR_BLUE    0x1F00  // Bright blue (byte swapped from 0x001F)
#define COLOR_CYAN    0xFF07  // Cyan (byte swapped from 0x07FF)
#define COLOR_YELLOW  0xE0FF  // Yellow (byte swapped from 0xFFE0)
#define COLOR_MAGENTA 0x1FF8  // Magenta (byte swapped from 0xF81F)
#define COLOR_ORANGE  0x20FD  // Orange (byte swapped from 0xFD20)
#define COLOR_PURPLE  0x0F78  // Purple (byte swapped from 0x780F)
#define COLOR_LIME    0xE007  // Lime green (byte swapped from 0x07E0)
#define COLOR_PINK    0x1FF8  // Hot pink (byte swapped from 0xF81F)
#define COLOR_SMOKE_WHITE 0x79CE  // Smoke white (byte swapped)

// Additional vibrant colors for more pairs (all byte swapped)
#define COLOR_TEAL    0x4107  // Bright teal (byte swapped from 0x0741)
#define COLOR_CORAL   0xCBFD  // Bright coral (byte swapped from 0xFDCB)
#define COLOR_SKYBLUE 0xFF67  // Sky blue instead of dark indigo (byte swapped from 0x67FF)
#define COLOR_GOLD    0x60FE  // Gold (byte swapped from 0xFE60)
#define COLOR_MINT    0xF7D7  // Mint (byte swapped from 0xD7F7)
#define COLOR_ROSE    0x5FFD  // Rose pink (byte swapped from 0xFD5F)

// Complementary color pairs (color1, color2)
// 15 completely unique color combinations - no duplicates or reverses
const uint16_t colorPairs[][2] = {
    {COLOR_RED, COLOR_CYAN},           // Classic red-cyan
    {COLOR_GREEN, COLOR_MAGENTA},      // Classic green-magenta
    {COLOR_BLUE, COLOR_YELLOW},        // Classic blue-yellow
    {COLOR_ORANGE, COLOR_SKYBLUE},     // Orange-sky blue (warm-cool)
    {COLOR_PURPLE, COLOR_LIME},        // Purple-lime
    {COLOR_PINK, COLOR_TEAL},          // Pink-teal
    {COLOR_CORAL, COLOR_MINT},         // Coral-mint (pastel warm-cool)
    {COLOR_SKYBLUE, COLOR_GOLD},       // Sky blue-gold
    {COLOR_ROSE, COLOR_GREEN},         // Rose-green
    {COLOR_YELLOW, COLOR_PURPLE},      // Yellow-purple
    {COLOR_ORANGE, COLOR_CYAN},        // Orange-cyan
    {COLOR_MAGENTA, COLOR_LIME},       // Magenta-lime (bright contrast)
    {COLOR_GOLD, COLOR_BLUE},          // Gold-blue
    {COLOR_WHITE, COLOR_RED},          // White-red (high contrast)
    {COLOR_TEAL, COLOR_CORAL}          // Teal-coral (ocean theme)
};
const int numColorPairs = sizeof(colorPairs) / sizeof(colorPairs[0]);

// 8x8 Bitmap Font Data
// Each character is represented as 8 bytes (8 rows of 8 pixels)
namespace Font8x8 {
    const uint8_t SPACE[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    // Uppercase letters
    const uint8_t A[] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00};
    const uint8_t B[] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00};
    const uint8_t C[] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00};
    const uint8_t D[] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00};
    const uint8_t E[] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00};
    const uint8_t F[] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00};
    const uint8_t G[] = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00};
    const uint8_t H[] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00};
    const uint8_t I[] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00};
    const uint8_t K[] = {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00};
    const uint8_t L[] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00};
    const uint8_t M[] = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00};
    const uint8_t N[] = {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00};
    const uint8_t O[] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00};
    const uint8_t P[] = {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00};
    const uint8_t R[] = {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00};
    const uint8_t S[] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00};
    const uint8_t T[] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00};
    const uint8_t U[] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00};
    const uint8_t W[] = {0x66, 0x66, 0x66, 0x66, 0x6B, 0x7F, 0x63, 0x00};
    const uint8_t Z[] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00};
    
    // Lowercase letters
    const uint8_t a[] = {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00};
    const uint8_t b[] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00};
    const uint8_t c[] = {0x00, 0x00, 0x3C, 0x60, 0x60, 0x60, 0x3C, 0x00};
    const uint8_t d[] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00};
    const uint8_t e[] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00};
    const uint8_t f[] = {0x1C, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00};
    const uint8_t g[] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C};
    const uint8_t h[] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00};
    const uint8_t i[] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00};
    const uint8_t k[] = {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00};
    const uint8_t l[] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00};
    const uint8_t m[] = {0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x00};
    const uint8_t n[] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00};
    const uint8_t o[] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00};
    const uint8_t p[] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60};
    const uint8_t r[] = {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00};
    const uint8_t s[] = {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00};
    const uint8_t t[] = {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00};
    const uint8_t u[] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00};
    const uint8_t v[] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00};
    const uint8_t w[] = {0x00, 0x00, 0x63, 0x63, 0x6B, 0x7F, 0x36, 0x00};
    const uint8_t z[] = {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00};
    
    // Numbers
    const uint8_t NUM_0[] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00};
    const uint8_t NUM_1[] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00};
    const uint8_t NUM_2[] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00};
    const uint8_t NUM_3[] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00};
    const uint8_t NUM_4[] = {0x0C, 0x1C, 0x2C, 0x4C, 0x7E, 0x0C, 0x0C, 0x00};
    const uint8_t NUM_5[] = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00};
    const uint8_t NUM_6[] = {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00};
    const uint8_t NUM_7[] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00};
    const uint8_t NUM_8[] = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00};
    const uint8_t NUM_9[] = {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00};
    
    // Special characters
    const uint8_t COLON[] = {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00};
    const uint8_t SLASH[] = {0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00, 0x00};
    const uint8_t DASH[] = {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00};
    const uint8_t PERIOD[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00};
}

// WiFi icon bitmaps (16x16 pixels)
namespace WifiIcons {
    // WiFi connected icon (3 arcs)
    const uint8_t CONNECTED_16x16[] = {
        0b00000000, 0b00000000,  // Row 0
        0b00000000, 0b00000000,  // Row 1
        0b00011111, 0b11111000,  // Row 2
        0b00111111, 0b11111100,  // Row 3
        0b01110000, 0b00001110,  // Row 4
        0b01100000, 0b00000110,  // Row 5
        0b00000111, 0b11100000,  // Row 6
        0b00001111, 0b11110000,  // Row 7
        0b00011000, 0b00011000,  // Row 8
        0b00010000, 0b00001000,  // Row 9
        0b00000011, 0b11000000,  // Row 10
        0b00000111, 0b11100000,  // Row 11
        0b00000100, 0b00100000,  // Row 12
        0b00000001, 0b10000000,  // Row 13
        0b00000001, 0b10000000,  // Row 14
        0b00000000, 0b00000000   // Row 15
    };
    
    // WiFi disconnected icon (WiFi with X)
    const uint8_t DISCONNECTED_16x16[] = {
        0b10000000, 0b00000001,  // Row 0 - X top
        0b01000000, 0b00000010,  // Row 1
        0b00111111, 0b11111100,  // Row 2
        0b00111111, 0b11111100,  // Row 3
        0b01110000, 0b00001110,  // Row 4
        0b01100000, 0b00000110,  // Row 5
        0b00000111, 0b11100000,  // Row 6
        0b00001111, 0b11110000,  // Row 7
        0b00011000, 0b00011000,  // Row 8
        0b00010000, 0b00001000,  // Row 9
        0b00000011, 0b11000000,  // Row 10
        0b00000111, 0b11100000,  // Row 11
        0b00000100, 0b00100000,  // Row 12
        0b00000001, 0b10000000,  // Row 13 - X bottom
        0b01000001, 0b10000010,  // Row 14
        0b10000000, 0b00000001   // Row 15
    };
}

// Mining pickaxe icon
namespace MiningIcons {
    const uint8_t PICKAXE_16x16[] = {
        0b00000000, 0b00000000,  // Row 0
        0b00000000, 0b00000110,  // Row 1 - pickaxe head start
        0b00000000, 0b00001111,  // Row 2
        0b00000000, 0b00011110,  // Row 3
        0b00000000, 0b00111100,  // Row 4
        0b00000000, 0b01111000,  // Row 5
        0b00000000, 0b11110000,  // Row 6
        0b00000001, 0b11100000,  // Row 7
        0b00000011, 0b11000000,  // Row 8 - handle start
        0b00000111, 0b10000000,  // Row 9
        0b00001111, 0b00000000,  // Row 10
        0b00011110, 0b00000000,  // Row 11
        0b00111100, 0b00000000,  // Row 12
        0b01111000, 0b00000000,  // Row 13
        0b11110000, 0b00000000,  // Row 14
        0b11100000, 0b00000000   // Row 15 - handle end
    };
}

// Get bitmap for a character
inline const uint8_t* getCharBitmap(char ch) {
    switch(ch) {
        case ' ': return Font8x8::SPACE;
        case 'A': return Font8x8::A;
        case 'B': return Font8x8::B;
        case 'C': return Font8x8::C;
        case 'D': return Font8x8::D;
        case 'E': return Font8x8::E;
        case 'F': return Font8x8::F;
        case 'G': return Font8x8::G;
        case 'H': return Font8x8::H;
        case 'I': return Font8x8::I;
        case 'K': return Font8x8::K;
        case 'L': return Font8x8::L;
        case 'M': return Font8x8::M;
        case 'N': return Font8x8::N;
        case 'O': return Font8x8::O;
        case 'P': return Font8x8::P;
        case 'R': return Font8x8::R;
        case 'S': return Font8x8::S;
        case 'T': return Font8x8::T;
        case 'U': return Font8x8::U;
        case 'W': return Font8x8::W;
        case 'Z': return Font8x8::Z;
        case 'a': return Font8x8::a;
        case 'b': return Font8x8::b;
        case 'c': return Font8x8::c;
        case 'd': return Font8x8::d;
        case 'e': return Font8x8::e;
        case 'f': return Font8x8::f;
        case 'g': return Font8x8::g;
        case 'h': return Font8x8::h;
        case 'i': return Font8x8::i;
        case 'k': return Font8x8::k;
        case 'l': return Font8x8::l;
        case 'm': return Font8x8::m;
        case 'n': return Font8x8::n;
        case 'o': return Font8x8::o;
        case 'p': return Font8x8::p;
        case 'r': return Font8x8::r;
        case 's': return Font8x8::s;
        case 't': return Font8x8::t;
        case 'u': return Font8x8::u;
        case 'v': return Font8x8::v;
        case 'w': return Font8x8::w;
        case 'z': return Font8x8::z;
        case '0': return Font8x8::NUM_0;
        case '1': return Font8x8::NUM_1;
        case '2': return Font8x8::NUM_2;
        case '3': return Font8x8::NUM_3;
        case '4': return Font8x8::NUM_4;
        case '5': return Font8x8::NUM_5;
        case '6': return Font8x8::NUM_6;
        case '7': return Font8x8::NUM_7;
        case '8': return Font8x8::NUM_8;
        case '9': return Font8x8::NUM_9;
        case ':': return Font8x8::COLON;
        case '/': return Font8x8::SLASH;
        case '-': return Font8x8::DASH;
        case '.': return Font8x8::PERIOD;
        default: return Font8x8::SPACE;
    }
}

#endif // DISPLAY_ASSETS_H
