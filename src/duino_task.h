#ifndef DUINO_TASK_H
#define DUINO_TASK_H

#include <Arduino.h>

// Duino-Coin mining statistics
struct DuinoStats {
    uint32_t hashes_per_second;
    uint32_t shares_accepted;
    uint32_t shares_rejected;
    float difficulty;
    uint32_t total_hashes;
};

// Duino task management
void duino_task_start(void);
void duino_task_stop(void);
bool duino_task_is_running(void);
DuinoStats duino_get_stats(void);
bool duino_has_found_share(void);  // Check if any share has been found

// Configure Duino-Coin credentials
void duino_set_credentials(const char* username, const char* rigId = "ESP32", const char* miningKey = "");

#endif // DUINO_TASK_H
