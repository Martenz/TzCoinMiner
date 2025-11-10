# TzCoin Miner - ESP32 Multi-Coin Cryptocurrency Miner

A feature-rich cryptocurrency miner for ESP32-S3 (LilyGo T-Display) supporting Bitcoin (BTC), Bitcoin Cash (BCH), and Duino-Coin mining with an elegant AMOLED display interface.

![TzCoin Miner Display](images/IMG_3256.HEIC)

![TzCoin Miner Display](images/IMG_3243.HEIC)

![TzCoin Miner Device](images/IMG_3244.HEIC)

3d-Case TPU+PLA model: https://www.printables.com/model/1476247-lilygo-t-display-s3-amoled-case

## 🎓 Educational Purpose & Realistic Expectations

**This project is purely educational and experimental.** It demonstrates:
- How cryptocurrency mining protocols work at a low level
- SHA-256 and SHA-1 hashing implementations on embedded systems
- ESP32 dual-core programming and optimization techniques
- Real-time mining pool communication (Stratum protocol)
- Web-based configuration and IoT device management

### 💰 The Reality: A High-Tech Lottery Ticket

Mining cryptocurrency with an ESP32 is essentially playing a **probabilistic lottery** with extremely low odds:

- **Bitcoin (BTC)**: At ~12,000 H/s vs. global hashrate of ~600 EH/s, your probability of finding a block is approximately **1 in 2 billion years**
- **Bitcoin Cash (BCH)**: With lower network difficulty, odds improve to roughly **1 in 6 million years** (~350x better than BTC, but still astronomical)
- **Expected earnings**: ~$0.00000001 per year (before electricity costs of ~$15/year)

**Why build it then?** 

Think of it as a **working science experiment** where you contribute a tiny lottery ticket to the blockchain while learning how it all works under the hood.

## ✨ Features

- **Multi-Coin Support**
  - Bitcoin (BTC) - SHA-256 mining (~12,000 H/s on Core 1)
  - Bitcoin Cash (BCH) - SHA-256 mining with pre-configured pool
  - Duino-Coin (DUCO) - SHA-1 mining (~5,700 H/s)

- **Mining Modes**
  - Pool Mining (default)
  - Solo Mining via RPC
  - Automatic coin switching

- **Web-Based Configuration**
  - WiFi setup portal
  - Dynamic pool configuration per coin
  - Real-time settings adjustment
  - Timezone configuration

- **Display Features**
  - 536x240 AMOLED display
  - Real-time hashrate monitoring
  - Mining statistics (shares, rejections)
  - Network status indicators
  - Animated screensaver ("The answer is 42" with star field)
  - Color-coded status bar

- **Performance**
  - Dual-core optimization (Core 0: WiFi, Core 1: Mining)
  - Efficient power management
  - Adaptive display refresh rates (50fps for animations, 1fps for stats)

## 🛠️ Hardware Requirements

- **ESP32-S3** LilyGo T-Display
  - 240MHz dual-core processor
  - 536x240 AMOLED display
  - WiFi connectivity
  - Minimum 4MB flash

  My setup: https://lilygo.cc/products/t-display-s3-amoled?variant=42859368677557

## 📦 Software Requirements

- [PlatformIO](https://platformio.org/) IDE
- ESP32 platform support
- Libraries (auto-installed via platformio.ini):
  - TFT_eSPI
  - WiFi
  - WebServer
  - Preferences
  - ArduinoJson

## 🚀 Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/yourusername/TzBtcMiner.git
cd TzBtcMiner
```

### 2. Open in PlatformIO

Open the project folder in Visual Studio Code with PlatformIO extension installed.

### 3. Configure Hardware

The project is pre-configured for LilyGo T-Display S3. If using different hardware, adjust `platformio.ini`:

```ini
[env:lilygo-t-display-s3]
platform = espressif32
board = lilygo-t-display-s3
framework = arduino
```

### 4. Build and Upload

```bash
# Build the project
pio run

# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor
```

### 5. Initial Setup

1. **WiFi Configuration**
   - On first boot, the device creates a WiFi access point named "ESP32-Miner-Config"
   - Connect to this network
   - Navigate to `http://192.168.4.1`
   - Enter your WiFi credentials and save

2. **Mining Configuration**
   - After WiFi connection, access the web interface at the device's IP address
   - Configure your preferred coin and mining settings:

   **For Bitcoin (BTC):**
   - Pool URL: `public-pool.io`
   - Pool Port: `21496`
   - Pool Password: `x`
   - BTC Wallet: Your Bitcoin wallet address
   - Pool Type: **PPLNS** (Pay Per Last N Shares) mining pool
   - Documentation: [https://web.public-pool.io/](https://web.public-pool.io/)
   - How it works: Public-Pool is a transparent, open-source pool that distributes rewards proportionally among miners based on contributed hashrate. Even with low hashrate, you'll occasionally receive tiny payouts when the pool finds blocks.

   **For Bitcoin Cash (BCH):**
   - Pool is pre-configured: `eu2.solopool.org:8002`
   - Only enter your BCH wallet address (CashAddr format: `bitcoincash:q...` or legacy)
   - Pool Type: **Solo mining pool** - you only get paid if YOU find a block (not the pool)
   - Documentation: [https://solopool.org/](https://solopool.org/)
   - How it works: SoloPool provides the infrastructure for solo mining without running your own node. If your device finds a valid block, you receive the FULL block reward (~3.125 BCH + fees). However, with ~12,000 H/s, this is statistically unlikely in your lifetime—it's literally a lottery ticket. The pool charges a small fee (typically 0.5-1%) only if you win.

   **For Duino-Coin:**
   - Enter your DUCO username
   - Optionally enter mining key

3. **Start Mining**
   - Press the device button to start/stop mining
   - Navigate through pages: Logo → Mining Stats → Network Info
   - Long press for web configuration mode

## 📊 Mining Statistics

### Hash Rates (ESP32-S3 @ 240MHz)
- **Bitcoin/BCH**: ~12,000 H/s
- **Duino-Coin**: ~5,700 H/s

### Solo Mining Probability Comparison
With the same hashrate, BCH solo mining has **~350x better odds** than BTC due to lower network difficulty, though block rewards are proportionally different.

### Expected Shares
- Pool mining: Shares accepted based on pool difficulty
- Solo mining: Extremely low probability - educational/lottery purpose

## 🎨 Display Interface

### Pages
1. **Logo Page** (default)
   - Animated screensaver after 5 seconds of inactivity
   - Star field with scrolling "The answer is 42" text
   
2. **Mining Stats Page**
   - Current hashrate
   - Shares accepted/rejected
   - Mining duration
   - Coin type indicator

3. **Network Page**
   - WiFi SSID and signal strength
   - IP address
   - Pool connection status
   - Uptime

### Status Bar
- WiFi signal strength indicator
- Mining status (⛏️ = active)
- Coin type (BTC/BCH/DUCO)

## 🔧 Configuration Options

All settings are stored in ESP32 NVS (non-volatile storage):

- WiFi credentials
- Pool settings (URL, port, password)
- Wallet addresses
- Mining mode (pool/solo)
- Coin selection
- Auto-start mining
- Timezone

## 🌐 Web Interface

Access the configuration web interface:
- During setup: `http://192.168.4.1`
- After WiFi connection: `http://<device-ip-address>`

Features:
- Responsive design
- Real-time configuration
- Separate sections for each coin
- Form validation
- Settings persistence

## 📝 File Structure

```
TzBtcMiner/
├── src/
│   ├── main.cpp           # Main application logic
│   ├── main.h             # Configuration and declarations
│   ├── display.cpp        # Display rendering and animations
│   ├── display.h          # Display function declarations
│   ├── wifi_config.cpp    # WiFi and web interface
│   └── wifi_config.h      # Configuration structures
├── include/
│   └── README
├── lib/
│   └── README
├── test/
│   └── README
├── platformio.ini         # PlatformIO configuration
├── sdkconfig.lilygo-t-display-s3  # ESP32-S3 SDK config
├── CMakeLists.txt         # CMake build configuration
├── .gitignore
├── LICENSE
└── README.md
```

## 🔐 Security Notes

- **Never commit wallet addresses or credentials to version control**
- All sensitive data is stored in ESP32 NVS only
- Default values in code are placeholders
- Use WiFi with strong password
- Consider firewall rules for web interface access

## 🐛 Troubleshooting

### WiFi Connection Issues
- Ensure correct SSID/password
- Check signal strength
- Verify router is 2.4GHz compatible (ESP32 doesn't support 5GHz)

### Mining Not Starting
- Verify wallet address is valid
- Check pool connectivity (ping pool URL)
- Ensure proper coin selection
- Monitor serial output for errors

### Display Issues
- Verify TFT_eSPI configuration in `platformio.ini`
- Check display cable connections
- Adjust brightness in code if needed

### Low Hashrate
- Normal for ESP32 (~12K H/s for SHA-256)
- Ensure CPU is running at 240MHz
- Check temperature (throttling may occur)

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Bitcoin Core developers
- Bitcoin Cash community
- Duino-Coin project
- ESP32 Arduino community
- TFT_eSPI library by Bodmer
- LilyGo for the T-Display hardware

## ⚠️ Disclaimer

This project is for **educational purposes only**. ESP32 mining is not profitable due to low hashrates compared to ASIC miners. This is a learning project to understand cryptocurrency mining protocols and ESP32 development.

**Expected profitability: ~$0.00000001 per year** (excluding electricity costs)

Use this project to:
- Learn about mining protocols
- Experiment with ESP32 capabilities
- Understand blockchain technology
- Have fun with hardware hacking

Do NOT expect any financial returns from ESP32 mining!

## 📧 Support

For issues, questions, or suggestions:
- Open an issue on GitHub
- Check existing issues for solutions
- Review the troubleshooting section

---

**Happy Mining!** ⛏️💎

*Remember: Be thankful for all the fish!*
