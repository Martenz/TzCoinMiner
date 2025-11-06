#include "wifi_config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>

// AP credentials
#define AP_SSID "TzCoinMinerWifi"
#define AP_PASSWORD "theansweris42"

// DNS server for captive portal
#define DNS_PORT 53

// NTP server
#define NTP_SERVER "pool.ntp.org"

// Preferences namespace
#define PREFS_NAMESPACE "miner_cfg"

// Web server on port 80
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// Current WiFi status
static WifiStatus currentStatus = WIFI_DISCONNECTED;
static WifiConfig currentConfig;
static bool timeConfigured = false;

// Global config variable (exported for other modules)
WifiConfig config;

// HTML page for configuration
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>TzCoinMiner Configuration</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 12px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            padding: 40px;
            max-width: 500px;
            width: 100%;
        }
        h1 {
            color: #333;
            margin-bottom: 10px;
            font-size: 28px;
            text-align: center;
        }
        .subtitle {
            color: #666;
            text-align: center;
            margin-bottom: 30px;
            font-size: 14px;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            color: #555;
            margin-bottom: 8px;
            font-weight: 600;
            font-size: 14px;
        }
        input[type="text"],
        input[type="password"],
        input[type="number"] {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 14px;
            transition: border-color 0.3s;
        }
        input:focus {
            outline: none;
            border-color: #667eea;
        }
        .btn {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 20px rgba(102, 126, 234, 0.4);
        }
        .btn:active {
            transform: translateY(0);
        }
        .section-title {
            color: #667eea;
            font-size: 16px;
            margin-top: 25px;
            margin-bottom: 15px;
            padding-bottom: 8px;
            border-bottom: 2px solid #e0e0e0;
        }
        .success {
            background: #4CAF50;
            color: white;
            padding: 15px;
            border-radius: 8px;
            margin-top: 20px;
            text-align: center;
            display: none;
        }
        .toggle-container {
            margin: 25px 0;
            padding: 20px;
            background: #f8f9fa;
            border-radius: 8px;
            border: 2px solid #e0e0e0;
        }
        .toggle-label {
            display: flex;
            align-items: center;
            justify-content: space-between;
            margin-bottom: 10px;
            font-weight: 600;
            color: #333;
        }
        .switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 34px;
        }
        .switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 34px;
        }
        .slider:before {
            position: absolute;
            content: "";
            height: 26px;
            width: 26px;
            left: 4px;
            bottom: 4px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }
        input:checked + .slider {
            background-color: #667eea;
        }
        input:checked + .slider:before {
            transform: translateX(26px);
        }
        .mode-description {
            font-size: 13px;
            color: #666;
            margin-top: 10px;
            padding: 10px;
            background: white;
            border-radius: 6px;
        }
        #poolSettings {
            transition: all 0.3s ease;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>⛏️ TzCoinMiner</h1>
        <p class="subtitle">Configuration Panel</p>
        
        <form method="POST" action="/save" onsubmit="showSuccess()">
            <div class="section-title">WiFi Settings</div>
            
            <div class="form-group">
                <label for="ssid">WiFi SSID</label>
                <input type="text" id="ssid" name="ssid" value="%SSID%" required>
            </div>
            
            <div class="form-group">
                <label for="wifiPW">WiFi Password</label>
                <input type="password" id="wifiPW" name="wifiPW" value="%WIFI_PW%">
            </div>
            
            <div class="form-group">
                <label for="timezone">⏰ Timezone</label>
                <select id="timezone" name="timezone" style="width: 100%; padding: 12px; border: 2px solid #e0e0e0; border-radius: 8px; font-size: 14px;">
                    <option value="CET-1CEST,M3.5.0,M10.5.0/3" %TZ_EUROPE_ROME%>Europe/Rome (CET/CEST)</option>
                    <option value="GMT0BST,M3.5.0/1,M10.5.0" %TZ_EUROPE_LONDON%>Europe/London (GMT/BST)</option>
                    <option value="WET0WEST,M3.5.0/1,M10.5.0" %TZ_EUROPE_LISBON%>Europe/Lisbon (WET/WEST)</option>
                    <option value="EET-2EEST,M3.5.0/3,M10.5.0/4" %TZ_EUROPE_ATHENS%>Europe/Athens (EET/EEST)</option>
                    <option value="EST5EDT,M3.2.0,M11.1.0" %TZ_US_EASTERN%>US/Eastern (EST/EDT)</option>
                    <option value="CST6CDT,M3.2.0,M11.1.0" %TZ_US_CENTRAL%>US/Central (CST/CDT)</option>
                    <option value="MST7MDT,M3.2.0,M11.1.0" %TZ_US_MOUNTAIN%>US/Mountain (MST/MDT)</option>
                    <option value="PST8PDT,M3.2.0,M11.1.0" %TZ_US_PACIFIC%>US/Pacific (PST/PDT)</option>
                    <option value="AEST-10AEDT,M10.1.0,M4.1.0/3" %TZ_AUSTRALIA_SYDNEY%>Australia/Sydney (AEST/AEDT)</option>
                    <option value="JST-9" %TZ_ASIA_TOKYO%>Asia/Tokyo (JST)</option>
                    <option value="CST-8" %TZ_ASIA_SHANGHAI%>Asia/Shanghai (CST)</option>
                    <option value="UTC0" %TZ_UTC%>UTC (No DST)</option>
                </select>
            </div>
            
            <div class="form-group">
                <label>🪙 Select Coin to Mine</label>
                <div style="margin-top: 10px;">
                    <label style="display: block; margin-bottom: 8px; cursor: pointer;">
                        <input type="radio" name="coinType" value="btc" %BTC_CHECKED% onchange="toggleCoinSettings()" style="margin-right: 8px;">
                        <strong>Mine Bitcoin (BTC)</strong> - SHA-256, pool or solo
                    </label>
                    <label style="display: block; margin-bottom: 8px; cursor: pointer;">
                        <input type="radio" name="coinType" value="bch" %BCH_CHECKED% onchange="toggleCoinSettings()" style="margin-right: 8px;">
                        <strong>Mine Bitcoin Cash (BCH)</strong> - SHA-256, faster blocks
                    </label>
                    <label style="display: block; cursor: pointer;">
                        <input type="radio" name="coinType" value="duco" %DUCO_CHECKED% onchange="toggleCoinSettings()" style="margin-right: 8px;">
                        <strong>Mine Duino-Coin (DUCO)</strong> - Pool mining for IoT devices
                    </label>
                </div>
            </div>
            
            <div class="toggle-container" id="soloToggle" style="display: %SOLO_DISPLAY%;">
                <div class="toggle-label">
                    <span>⛏️ Solo Mining Mode</span>
                    <label class="switch">
                        <input type="checkbox" id="soloMode" name="soloMode" %SOLO_CHECKED% onchange="togglePoolSettings()">
                        <span class="slider"></span>
                    </label>
                </div>
                <div class="mode-description" id="modeDesc">
                    <strong>Pool Mode:</strong> Mine with a mining pool (recommended). Use the pool settings below to connect.
                </div>
            </div>
            
            <div class="toggle-container">
                <div class="toggle-label">
                    <span>🚀 Auto Start Mining</span>
                    <label class="switch">
                        <input type="checkbox" id="autoStartMining" name="autoStartMining" %AUTO_START_CHECKED%>
                        <span class="slider"></span>
                    </label>
                </div>
                <div class="mode-description">
                    <strong>Auto Start:</strong> Automatically start mining when device boots and WiFi is connected.
                </div>
            </div>
            
            <div id="btcPoolSettings" style="display: %BTC_POOL_DISPLAY%;">
                <div class="section-title">⛏️ Bitcoin (BTC) Pool Settings</div>
                
                <div class="form-group">
                    <label for="poolUrl">Pool URL</label>
                    <input type="text" id="poolUrl" name="poolUrl" value="%POOL_URL%">
                </div>
                
                <div class="form-group">
                    <label for="poolPort">Pool Port</label>
                    <input type="number" id="poolPort" name="poolPort" value="%POOL_PORT%">
                </div>
                
                <div class="form-group">
                    <label for="poolPassword">Pool Password</label>
                    <input type="text" id="poolPassword" name="poolPassword" value="%POOL_PW%">
                </div>
                
                <div class="form-group">
                    <label for="btcWallet">💰 Bitcoin (BTC) Wallet Address</label>
                    <input type="text" id="btcWallet" name="btcWallet" value="%BTC_WALLET%" placeholder="bc1q... or 1...">
                </div>
            </div>
            
            <div id="bchPoolSettings" style="display: %BCH_POOL_DISPLAY%;">
                <div class="section-title">⛏️ Bitcoin Cash (BCH) Pool Settings</div>
                
                <div class="form-group">
                    <label>Pool URL</label>
                    <input type="text" value="eu2.solopool.org" disabled style="background: #f0f0f0;">
                    <small style="color: #666; display: block; margin-top: 5px;">Solo mining pool for Bitcoin Cash (Europe)</small>
                </div>
                
                <div class="form-group">
                    <label>Pool Port</label>
                    <input type="text" value="8002" disabled style="background: #f0f0f0;">
                </div>
                
                <div class="form-group">
                    <label for="bchWallet">💰 Bitcoin Cash (BCH) Wallet Address</label>
                    <input type="text" id="bchWallet" name="bchWallet" value="%BCH_WALLET%" placeholder="bitcoincash:q... or q...">
                </div>
            </div>
            
            <div id="rpcSettings" style="display: %RPC_DISPLAY%;">
                <div class="section-title">Bitcoin RPC Settings (Solo Mode)</div>
                
                <div class="form-group">
                    <label for="rpcHost">RPC Host</label>
                    <input type="text" id="rpcHost" name="rpcHost" value="%RPC_HOST%" placeholder="127.0.0.1">
                </div>
                
                <div class="form-group">
                    <label for="rpcPort">RPC Port</label>
                    <input type="number" id="rpcPort" name="rpcPort" value="%RPC_PORT%" placeholder="8332">
                </div>
                
                <div class="form-group">
                    <label for="rpcUser">RPC Username</label>
                    <input type="text" id="rpcUser" name="rpcUser" value="%RPC_USER%" placeholder="bitcoinrpc">
                </div>
                
                <div class="form-group">
                    <label for="rpcPassword">RPC Password</label>
                    <input type="password" id="rpcPassword" name="rpcPassword" value="%RPC_PASSWORD%">
                </div>
            </div>
            
            <div id="ducoSettings" style="display: %DUCO_DISPLAY%;">
                <div class="section-title">Duino-Coin Settings</div>
                
                <div class="form-group">
                    <label for="ducoUsername">Duino-Coin Username</label>
                    <input type="text" id="ducoUsername" name="ducoUsername" value="%DUCO_USER%" placeholder="your_duco_username">
                </div>
                
                <div class="form-group">
                    <label for="ducoMiningKey">Mining Key (Optional)</label>
                    <input type="password" id="ducoMiningKey" name="ducoMiningKey" value="%DUCO_KEY%" placeholder="optional_mining_key">
                </div>
                
                <div class="mode-description">
                    <strong>Pool:</strong> server.duinocoin.com:2811 (automatic)<br>
                    <strong>Info:</strong> Create account at <a href="https://duinocoin.com" target="_blank" style="color: #667eea;">duinocoin.com</a>
                </div>
            </div>
            
            <button type="submit" class="btn">💾 Save Configuration</button>
        </form>
        
        <div class="success" id="successMsg">
            ✓ Configuration saved! Device will restart in 3 seconds...
        </div>
    </div>
    
    <script>
        function showSuccess() {
            document.getElementById('successMsg').style.display = 'block';
        }
        
        function toggleCoinSettings() {
            const coinTypeRadios = document.getElementsByName('coinType');
            let selectedCoin = 'btc';
            for (const radio of coinTypeRadios) {
                if (radio.checked) {
                    selectedCoin = radio.value;
                    break;
                }
            }
            
            const soloToggle = document.getElementById('soloToggle');
            const btcPoolSettings = document.getElementById('btcPoolSettings');
            const bchPoolSettings = document.getElementById('bchPoolSettings');
            const rpcSettings = document.getElementById('rpcSettings');
            const ducoSettings = document.getElementById('ducoSettings');
            
            if (selectedCoin === 'duco') {
                // Show Duino-Coin, hide Bitcoin options
                soloToggle.style.display = 'none';
                btcPoolSettings.style.display = 'none';
                bchPoolSettings.style.display = 'none';
                rpcSettings.style.display = 'none';
                ducoSettings.style.display = 'block';
            } else if (selectedCoin === 'bch') {
                // Show BCH pool settings
                soloToggle.style.display = 'none'; // BCH doesn't support solo toggle
                btcPoolSettings.style.display = 'none';
                bchPoolSettings.style.display = 'block';
                rpcSettings.style.display = 'none';
                ducoSettings.style.display = 'none';
            } else {
                // Show BTC options (pool/solo)
                soloToggle.style.display = 'block';
                bchPoolSettings.style.display = 'none';
                ducoSettings.style.display = 'none';
                togglePoolSettings();  // Update Bitcoin pool/solo visibility
            }
        }
        
        function togglePoolSettings() {
            const soloMode = document.getElementById('soloMode').checked;
            const btcPoolSettings = document.getElementById('btcPoolSettings');
            const rpcSettings = document.getElementById('rpcSettings');
            const modeDesc = document.getElementById('modeDesc');
            const poolInputs = btcPoolSettings ? btcPoolSettings.querySelectorAll('input') : [];
            const rpcInputs = rpcSettings.querySelectorAll('input');
            
            if (soloMode) {
                btcPoolSettings.style.opacity = '0.5';
                btcPoolSettings.style.pointerEvents = 'none';
                btcPoolSettings.style.display = 'block';
                rpcSettings.style.opacity = '1';
                rpcSettings.style.pointerEvents = 'auto';
                rpcSettings.style.display = 'block';
                modeDesc.innerHTML = '<strong>Solo Mode:</strong> Mine directly with a Bitcoin node. Configure RPC settings below to connect to your Bitcoin Core node.';
                poolInputs.forEach(input => input.removeAttribute('required'));
            } else {
                btcPoolSettings.style.opacity = '1';
                btcPoolSettings.style.pointerEvents = 'auto';
                btcPoolSettings.style.display = 'block';
                rpcSettings.style.opacity = '0.5';
                rpcSettings.style.pointerEvents = 'none';
                rpcSettings.style.display = 'block';
                modeDesc.innerHTML = '<strong>Pool Mode:</strong> Mine with a mining pool (recommended). Use the pool settings below to connect.';
                rpcInputs.forEach(input => input.removeAttribute('required'));
            }
        }
        
        // Initialize on load
        window.onload = function() {
            toggleCoinSettings();
        };
    </script>
</body>
</html>
)rawliteral";

// Replace placeholders in HTML
String getHtmlPage() {
    String html = String(HTML_PAGE);
    html.replace("%SSID%", currentConfig.ssid);
    html.replace("%WIFI_PW%", currentConfig.password);
    html.replace("%POOL_URL%", currentConfig.poolUrl);
    html.replace("%POOL_PORT%", String(currentConfig.poolPort));
    html.replace("%POOL_PW%", currentConfig.poolPassword);
    html.replace("%BTC_WALLET%", currentConfig.btcWallet);
    html.replace("%BCH_WALLET%", currentConfig.bchWallet);
    html.replace("%RPC_HOST%", currentConfig.rpcHost);
    html.replace("%RPC_PORT%", String(currentConfig.rpcPort));
    html.replace("%RPC_USER%", currentConfig.rpcUser);
    html.replace("%RPC_PASSWORD%", currentConfig.rpcPassword);
    html.replace("%DUCO_USER%", currentConfig.ducoUsername);
    html.replace("%DUCO_KEY%", currentConfig.ducoMiningKey);
    html.replace("%SOLO_CHECKED%", currentConfig.soloMode ? "checked" : "");
    html.replace("%BTC_CHECKED%", (!currentConfig.useDuinoCoin && !currentConfig.useBitcoinCash) ? "checked" : "");
    html.replace("%BCH_CHECKED%", (!currentConfig.useDuinoCoin && currentConfig.useBitcoinCash) ? "checked" : "");
    html.replace("%DUCO_CHECKED%", currentConfig.useDuinoCoin ? "checked" : "");
    html.replace("%AUTO_START_CHECKED%", currentConfig.autoStartMining ? "checked" : "");
    
    // Timezone selection
    html.replace("%TZ_EUROPE_ROME%", strcmp(currentConfig.timezone, "CET-1CEST,M3.5.0,M10.5.0/3") == 0 ? "selected" : "");
    html.replace("%TZ_EUROPE_LONDON%", strcmp(currentConfig.timezone, "GMT0BST,M3.5.0/1,M10.5.0") == 0 ? "selected" : "");
    html.replace("%TZ_EUROPE_LISBON%", strcmp(currentConfig.timezone, "WET0WEST,M3.5.0/1,M10.5.0") == 0 ? "selected" : "");
    html.replace("%TZ_EUROPE_ATHENS%", strcmp(currentConfig.timezone, "EET-2EEST,M3.5.0/3,M10.5.0/4") == 0 ? "selected" : "");
    html.replace("%TZ_US_EASTERN%", strcmp(currentConfig.timezone, "EST5EDT,M3.2.0,M11.1.0") == 0 ? "selected" : "");
    html.replace("%TZ_US_CENTRAL%", strcmp(currentConfig.timezone, "CST6CDT,M3.2.0,M11.1.0") == 0 ? "selected" : "");
    html.replace("%TZ_US_MOUNTAIN%", strcmp(currentConfig.timezone, "MST7MDT,M3.2.0,M11.1.0") == 0 ? "selected" : "");
    html.replace("%TZ_US_PACIFIC%", strcmp(currentConfig.timezone, "PST8PDT,M3.2.0,M11.1.0") == 0 ? "selected" : "");
    html.replace("%TZ_AUSTRALIA_SYDNEY%", strcmp(currentConfig.timezone, "AEST-10AEDT,M10.1.0,M4.1.0/3") == 0 ? "selected" : "");
    html.replace("%TZ_ASIA_TOKYO%", strcmp(currentConfig.timezone, "JST-9") == 0 ? "selected" : "");
    html.replace("%TZ_ASIA_SHANGHAI%", strcmp(currentConfig.timezone, "CST-8") == 0 ? "selected" : "");
    html.replace("%TZ_UTC%", strcmp(currentConfig.timezone, "UTC0") == 0 ? "selected" : "");
    
    // Set initial visibility based on configuration
    if (currentConfig.useDuinoCoin) {
        // Duino-Coin selected: hide Bitcoin sections, show DUCO
        html.replace("%SOLO_DISPLAY%", "none");
        html.replace("%BTC_POOL_DISPLAY%", "none");
        html.replace("%BCH_POOL_DISPLAY%", "none");
        html.replace("%RPC_DISPLAY%", "none");
        html.replace("%DUCO_DISPLAY%", "block");
    } else if (currentConfig.useBitcoinCash) {
        // Bitcoin Cash selected: show only BCH pool settings
        html.replace("%SOLO_DISPLAY%", "none");
        html.replace("%BTC_POOL_DISPLAY%", "none");
        html.replace("%BCH_POOL_DISPLAY%", "block");
        html.replace("%RPC_DISPLAY%", "none");
        html.replace("%DUCO_DISPLAY%", "none");
    } else {
        // Bitcoin selected: show Bitcoin sections, hide DUCO and BCH
        html.replace("%SOLO_DISPLAY%", "block");
        html.replace("%BCH_POOL_DISPLAY%", "none");
        html.replace("%DUCO_DISPLAY%", "none");
        // Pool/RPC visibility depends on solo mode
        if (currentConfig.soloMode) {
            html.replace("%BTC_POOL_DISPLAY%", "none");
            html.replace("%RPC_DISPLAY%", "block");
        } else {
            html.replace("%BTC_POOL_DISPLAY%", "block");
            html.replace("%RPC_DISPLAY%", "none");
        }
    }
    
    return html;
}

// Handle root page
void handleRoot() {
    server.send(200, "text/html", getHtmlPage());
}

// Handle save configuration
void handleSave() {
    // Get form data
    if (server.hasArg("ssid")) {
        strncpy(currentConfig.ssid, server.arg("ssid").c_str(), sizeof(currentConfig.ssid) - 1);
    }
    if (server.hasArg("wifiPW")) {
        strncpy(currentConfig.password, server.arg("wifiPW").c_str(), sizeof(currentConfig.password) - 1);
    }
    if (server.hasArg("timezone")) {
        strncpy(currentConfig.timezone, server.arg("timezone").c_str(), sizeof(currentConfig.timezone) - 1);
    }
    if (server.hasArg("poolUrl")) {
        strncpy(currentConfig.poolUrl, server.arg("poolUrl").c_str(), sizeof(currentConfig.poolUrl) - 1);
    }
    if (server.hasArg("poolPort")) {
        currentConfig.poolPort = server.arg("poolPort").toInt();
    }
    if (server.hasArg("poolPassword")) {
        strncpy(currentConfig.poolPassword, server.arg("poolPassword").c_str(), sizeof(currentConfig.poolPassword) - 1);
    }
    if (server.hasArg("btcWallet")) {
        strncpy(currentConfig.btcWallet, server.arg("btcWallet").c_str(), sizeof(currentConfig.btcWallet) - 1);
    }
    if (server.hasArg("bchWallet")) {
        strncpy(currentConfig.bchWallet, server.arg("bchWallet").c_str(), sizeof(currentConfig.bchWallet) - 1);
    }
    if (server.hasArg("rpcHost")) {
        strncpy(currentConfig.rpcHost, server.arg("rpcHost").c_str(), sizeof(currentConfig.rpcHost) - 1);
    }
    if (server.hasArg("rpcPort")) {
        currentConfig.rpcPort = server.arg("rpcPort").toInt();
    }
    if (server.hasArg("rpcUser")) {
        strncpy(currentConfig.rpcUser, server.arg("rpcUser").c_str(), sizeof(currentConfig.rpcUser) - 1);
    }
    if (server.hasArg("rpcPassword")) {
        strncpy(currentConfig.rpcPassword, server.arg("rpcPassword").c_str(), sizeof(currentConfig.rpcPassword) - 1);
    }
    if (server.hasArg("ducoUsername")) {
        strncpy(currentConfig.ducoUsername, server.arg("ducoUsername").c_str(), sizeof(currentConfig.ducoUsername) - 1);
    }
    if (server.hasArg("ducoMiningKey")) {
        strncpy(currentConfig.ducoMiningKey, server.arg("ducoMiningKey").c_str(), sizeof(currentConfig.ducoMiningKey) - 1);
    }
    
    // Check solo mode checkbox
    currentConfig.soloMode = server.hasArg("soloMode");
    
    // Check coin type from radio buttons
    if (server.hasArg("coinType")) {
        String coinType = server.arg("coinType");
        currentConfig.useDuinoCoin = (coinType == "duco");
        currentConfig.useBitcoinCash = (coinType == "bch");
    }
    
    // Check auto start mining checkbox
    currentConfig.autoStartMining = server.hasArg("autoStartMining");
    
    currentConfig.isConfigured = true;
    
    // Save to NVS
    if (wifi_save_config(currentConfig)) {
        Serial.println("Configuration saved successfully!");
        Serial.printf("Solo Mode: %s\n", currentConfig.soloMode ? "ON" : "OFF");
        Serial.printf("Auto Start Mining: %s\n", currentConfig.autoStartMining ? "ON" : "OFF");
        
        // Update global config variable
        memcpy(&config, &currentConfig, sizeof(WifiConfig));
        
        // Send success page with auto-close
        String successHtml = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Configuration Saved</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            padding: 40px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            text-align: center;
            max-width: 400px;
        }
        h1 {
            color: #667eea;
            margin: 0 0 20px 0;
            font-size: 28px;
        }
        .checkmark {
            width: 80px;
            height: 80px;
            border-radius: 50%;
            display: block;
            margin: 0 auto 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            position: relative;
        }
        .checkmark::after {
            content: '✓';
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            color: white;
            font-size: 50px;
            font-weight: bold;
        }
        p {
            color: #666;
            font-size: 16px;
            line-height: 1.6;
        }
        .info {
            background: #f0f0f0;
            padding: 15px;
            border-radius: 10px;
            margin-top: 20px;
            font-size: 14px;
            color: #333;
        }
    </style>
    <script>
        // Close captive portal and window after delay
        setTimeout(function() {
            // Try to close the window (works on some captive portals)
            window.close();
            // Also try to navigate away to force captive portal to close
            window.location.href = 'about:blank';
        }, 3000);
    </script>
</head>
<body>
    <div class="container">
        <div class="checkmark"></div>
        <h1>Configuration Saved!</h1>
        <p>Your settings have been saved successfully.</p>
        <p>The device will restart in 3 seconds...</p>
        <div class="info">
            <strong>Note:</strong> This window should close automatically. If it doesn't, you can close it manually.
        </div>
    </div>
</body>
</html>
)rawliteral";
        
        server.send(200, "text/html", successHtml);
        
        // Restart after a delay
        delay(3000);
        ESP.restart();
    } else {
        server.send(500, "text/plain", "Failed to save configuration");
    }
}

void wifi_init() {
    Serial.println("Initializing WiFi system...");
    
    // Initialize preferences
    preferences.begin(PREFS_NAMESPACE, false);
    
    // Load saved configuration
    memset(&currentConfig, 0, sizeof(currentConfig));
    wifi_load_config(currentConfig);
    
    // Copy to global config variable
    memcpy(&config, &currentConfig, sizeof(WifiConfig));
    
    if (currentConfig.isConfigured) {
        Serial.println("Found saved WiFi configuration");
        Serial.printf("SSID: %s\n", currentConfig.ssid);
        Serial.printf("Pool: %s:%d\n", currentConfig.poolUrl, currentConfig.poolPort);
        
        // Try to connect to saved WiFi
        if (wifi_connect_saved()) {
            Serial.println("WiFi connected successfully!");
            currentStatus = WIFI_CONNECTED;
        } else {
            Serial.println("Failed to connect to saved WiFi");
            currentStatus = WIFI_DISCONNECTED;
        }
    } else {
        Serial.println("No WiFi configuration found");
        currentStatus = WIFI_DISCONNECTED;
    }
}

void wifi_start_ap() {
    Serial.println("Starting WiFi AP mode...");
    
    // Stop any existing connection
    WiFi.disconnect();
    delay(100);
    
    // Start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    
    // Start DNS server for captive portal
    // This redirects all DNS requests to the AP's IP address
    dnsServer.start(DNS_PORT, "*", IP);
    Serial.println("DNS server started for captive portal");
    
    // Setup web server routes - catch all requests
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    
    // Captive portal - redirect all unknown requests to root
    server.onNotFound(handleRoot);
    
    server.begin();
    
    currentStatus = WIFI_AP_MODE;
    Serial.println("Web server started on http://192.168.4.1");
    Serial.println("Captive portal active - browsers should auto-redirect");
}

void wifi_stop_ap() {
    Serial.println("Stopping WiFi AP mode...");
    dnsServer.stop();
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    currentStatus = WIFI_DISCONNECTED;
}

bool wifi_connect_saved() {
    if (!currentConfig.isConfigured || strlen(currentConfig.ssid) == 0) {
        Serial.println("No WiFi credentials to connect with");
        return false;
    }
    
    Serial.printf("Connecting to WiFi: %s\n", currentConfig.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(currentConfig.ssid, currentConfig.password);
    
    // Wait up to 10 seconds for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        
        // Configure time with NTP - Use configured timezone
        Serial.println("Configuring time via NTP...");
        configTime(0, 0, NTP_SERVER);
        
        // Load timezone from config, fallback to Europe/Rome if not set
        if (strlen(config.timezone) > 0) {
            setenv("TZ", config.timezone, 1);
            Serial.printf("Timezone set to: %s\n", config.timezone);
        } else {
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);  // Default: Europe/Rome
            Serial.println("Timezone set to: CET-1CEST,M3.5.0,M10.5.0/3 (default Europe/Rome)");
        }
        tzset();
        
        // Wait a bit for time to sync
        struct tm timeinfo;
        int retries = 0;
        while (!getLocalTime(&timeinfo) && retries < 10) {
            delay(500);
            Serial.print(".");
            retries++;
        }
        Serial.println();
        
        if (getLocalTime(&timeinfo)) {
            timeConfigured = true;
            Serial.println("Time synchronized!");
            Serial.printf("Current time: %02d/%02d/%04d %02d:%02d:%02d (Local time)\n",
                         timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                         timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        } else {
            timeConfigured = false;
            Serial.println("Failed to sync time");
        }
        
        return true;
    } else {
        Serial.println("WiFi connection failed");
        WiFi.disconnect();
        timeConfigured = false;
        return false;
    }
}

WifiStatus wifi_get_status() {
    // Update status based on actual WiFi state
    if (WiFi.getMode() == WIFI_AP) {
        currentStatus = WIFI_AP_MODE;
    } else if (WiFi.status() == WL_CONNECTED) {
        currentStatus = WIFI_CONNECTED;
    } else {
        currentStatus = WIFI_DISCONNECTED;
    }
    return currentStatus;
}

bool wifi_load_config(WifiConfig &config) {
    config.isConfigured = preferences.getBool("configured", false);
    
    if (!config.isConfigured) {
        // Set defaults
        strcpy(config.ssid, "myWifiSSID");
        strcpy(config.password, "myWifiPassword");
        strcpy(config.poolUrl, "public-pool.io");
        config.poolPort = 21496;
        strcpy(config.poolPassword, "x");
        strcpy(config.btcWallet, "YOUR_BTC_WALLET_ADDRESS");
        strcpy(config.bchWallet, "YOUR_BCH_WALLET_ADDRESS");  // Empty by default
        strcpy(config.rpcHost, "127.0.0.1");
        config.rpcPort = 8332;
        strcpy(config.rpcUser, "bitcoinrpc");
        strcpy(config.rpcPassword, "");
        strcpy(config.ducoUsername, "");
        strcpy(config.ducoMiningKey, "");
        strcpy(config.timezone, "CET-1CEST,M3.5.0,M10.5.0/3");  // Default: Europe/Rome
        config.soloMode = false;  // Default to pool mode
        config.useDuinoCoin = false;  // Default to Bitcoin
        config.useBitcoinCash = false;  // Default to Bitcoin (not BCH)
        config.autoStartMining = false;  // Default to manual start
        return false;
    }
    
    // Load from NVS
    preferences.getString("ssid", config.ssid, sizeof(config.ssid));
    preferences.getString("password", config.password, sizeof(config.password));
    preferences.getString("poolUrl", config.poolUrl, sizeof(config.poolUrl));
    config.poolPort = preferences.getUShort("poolPort", 21496);
    preferences.getString("poolPW", config.poolPassword, sizeof(config.poolPassword));
    preferences.getString("btcWallet", config.btcWallet, sizeof(config.btcWallet));
    preferences.getString("bchWallet", config.bchWallet, sizeof(config.bchWallet));
    preferences.getString("rpcHost", config.rpcHost, sizeof(config.rpcHost));
    config.rpcPort = preferences.getUShort("rpcPort", 8332);
    preferences.getString("rpcUser", config.rpcUser, sizeof(config.rpcUser));
    preferences.getString("rpcPW", config.rpcPassword, sizeof(config.rpcPassword));
    preferences.getString("ducoUser", config.ducoUsername, sizeof(config.ducoUsername));
    preferences.getString("ducoKey", config.ducoMiningKey, sizeof(config.ducoMiningKey));
    preferences.getString("timezone", config.timezone, sizeof(config.timezone));
    // Fallback to Europe/Rome if empty
    if (strlen(config.timezone) == 0) {
        strcpy(config.timezone, "CET-1CEST,M3.5.0,M10.5.0/3");
    }
    config.soloMode = preferences.getBool("soloMode", false);
    config.useDuinoCoin = preferences.getBool("useDuco", false);
    config.useBitcoinCash = preferences.getBool("useBCH", false);
    config.autoStartMining = preferences.getBool("autoStart", false);
    
    return true;
}

bool wifi_save_config(const WifiConfig &config) {
    preferences.putBool("configured", true);
    preferences.putString("ssid", config.ssid);
    preferences.putString("password", config.password);
    preferences.putString("poolUrl", config.poolUrl);
    preferences.putUShort("poolPort", config.poolPort);
    preferences.putString("poolPW", config.poolPassword);
    preferences.putString("btcWallet", config.btcWallet);
    preferences.putString("bchWallet", config.bchWallet);
    preferences.putString("rpcHost", config.rpcHost);
    preferences.putUShort("rpcPort", config.rpcPort);
    preferences.putString("rpcUser", config.rpcUser);
    preferences.putString("rpcPW", config.rpcPassword);
    preferences.putString("ducoUser", config.ducoUsername);
    preferences.putString("ducoKey", config.ducoMiningKey);
    preferences.putString("timezone", config.timezone);
    preferences.putBool("soloMode", config.soloMode);
    preferences.putBool("useDuco", config.useDuinoCoin);
    preferences.putBool("useBCH", config.useBitcoinCash);
    preferences.putBool("autoStart", config.autoStartMining);
    
    Serial.println("Configuration saved to NVS:");
    Serial.printf("  SSID: %s\n", config.ssid);
    Serial.printf("  Timezone: %s\n", config.timezone);
    
    // Determine coin type
    const char* coinType;
    if (config.useDuinoCoin) {
        coinType = "Duino-Coin";
    } else if (config.useBitcoinCash) {
        coinType = "Bitcoin Cash (BCH)";
    } else {
        coinType = "Bitcoin (BTC)";
    }
    Serial.printf("  Coin: %s\n", coinType);
    
    if (config.useDuinoCoin) {
        Serial.printf("  DUCO Username: %s\n", config.ducoUsername);
    } else {
        Serial.printf("  Pool: %s:%d\n", config.poolUrl, config.poolPort);
        Serial.printf("  Wallet: %s\n", config.btcWallet);
        Serial.printf("  RPC: %s:%d (user: %s)\n", config.rpcHost, config.rpcPort, config.rpcUser);
        Serial.printf("  Solo Mode: %s\n", config.soloMode ? "YES" : "NO");
    }
    Serial.printf("  Auto Start Mining: %s\n", config.autoStartMining ? "YES" : "NO");
    
    return true;
}

void wifi_clear_config() {
    preferences.clear();
    memset(&currentConfig, 0, sizeof(currentConfig));
    Serial.println("WiFi configuration cleared");
}

void wifi_handle_client() {
    if (currentStatus == WIFI_AP_MODE) {
        dnsServer.processNextRequest();
        server.handleClient();
    }
}

String wifi_get_time_string() {
    if (!timeConfigured) {
        return "-- /-- /--  -- :-- :--";
    }
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "-- /-- /--  -- :-- :--";
    }
    
    char timeStr[24];
    // Format: DD/MM/YY - HH:MM:SS
    snprintf(timeStr, sizeof(timeStr), "%02d/%02d/%02d - %02d:%02d:%02d",
             timeinfo.tm_mday,
             timeinfo.tm_mon + 1,
             timeinfo.tm_year % 100,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);
    
    return String(timeStr);
}

bool wifi_is_time_synced() {
    return timeConfigured;
}
