#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <Arduino.h>

// WiFi configuration structure
struct WifiConfig {
    char ssid[64];
    char password[64];
    char poolUrl[128];
    uint16_t poolPort;
    char poolPassword[64];
    char btcWallet[128];
    char bchWallet[128];    // Separate BCH wallet address
    char rpcHost[128];      // Bitcoin RPC host
    uint16_t rpcPort;       // Bitcoin RPC port
    char rpcUser[64];       // Bitcoin RPC username
    char rpcPassword[64];   // Bitcoin RPC password
    char ducoUsername[64];  // Duino-Coin username
    char ducoMiningKey[64]; // Duino-Coin mining key (optional)
    char timezone[64];      // Timezone string (e.g., "CET-1CEST,M3.5.0,M10.5.0/3")
    bool soloMode;          // true = solo mining, false = pool mining
    bool useDuinoCoin;      // true = mine Duino-Coin, false = mine Bitcoin/BCH
    bool useBitcoinCash;    // true = mine Bitcoin Cash, false = mine Bitcoin (only when useDuinoCoin=false)
    bool autoStartMining;   // true = start mining automatically on boot
    bool isConfigured;
};

// WiFi status
enum WifiStatus {
    WIFI_DISCONNECTED = 0,
    WIFI_CONNECTED = 1,
    WIFI_AP_MODE = 2
};

// Initialize WiFi system
void wifi_init(void);

// Start WiFi AP mode (hotspot)
void wifi_start_ap(void);

// Stop WiFi AP mode
void wifi_stop_ap(void);

// Try to connect to saved WiFi credentials
bool wifi_connect_saved(void);

// Get current WiFi status
WifiStatus wifi_get_status(void);

// Load configuration from NVS
bool wifi_load_config(WifiConfig &config);

// Save configuration to NVS
bool wifi_save_config(const WifiConfig &config);

// Clear saved configuration
void wifi_clear_config(void);

// Handle web server requests (call in loop)
void wifi_handle_client(void);

// Get current time string in format DD/MM/YY - HH:MM:SS (Configured timezone)
String wifi_get_time_string(void);

// Check if time is synchronized
bool wifi_is_time_synced(void);

// Global config variable (defined in wifi_config.cpp)
extern WifiConfig config;

#endif // WIFI_CONFIG_H
