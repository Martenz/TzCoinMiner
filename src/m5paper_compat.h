#ifndef M5PAPER_COMPAT_H
#define M5PAPER_COMPAT_H

// Compatibility header for M5Paper
// This ensures Serial is properly defined before M5EPD.h includes

#include <Arduino.h>
#include <HardwareSerial.h>

// Ensure Serial is defined
#ifndef Serial
extern HardwareSerial Serial;
#endif

#endif // M5PAPER_COMPAT_H
