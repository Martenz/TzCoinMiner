#ifndef DUINO_CLIENT_H
#define DUINO_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>

// Duino-Coin server configuration (fallback)
#define DUCO_SERVER_FALLBACK "server.duinocoin.com"
#define DUCO_PORT_FALLBACK 2811
#define DUCO_POOL_PICKER_URL "https://server.duinocoin.com/getPool"

// Duino-Coin client state
enum DuinoState {
    DUCO_DISCONNECTED,
    DUCO_CONNECTING,
    DUCO_CONNECTED,
    DUCO_MINING,
    DUCO_ERROR
};

// Initialize Duino-Coin client
void duino_init(const char* username, const char* rigIdentifier = "ESP32", const char* miningKey = "");

// Fetch best pool from pool picker
bool duino_fetch_pool(String &host, int &port);

// Connect to Duino-Coin pool
bool duino_connect(void);

// Disconnect from pool
void duino_disconnect(void);

// Check if connected
bool duino_is_connected(void);

// Get current state
DuinoState duino_get_state(void);

// Mine one job (blocking call, returns accepted/rejected)
bool duino_mine_job(void);

// Get mining statistics
uint32_t duino_get_accepted_shares(void);
uint32_t duino_get_rejected_shares(void);
uint32_t duino_get_hashrate(void);
float duino_get_difficulty(void);

#endif // DUINO_CLIENT_H
