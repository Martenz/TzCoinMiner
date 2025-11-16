# M5Paper v1.1 Support

This document describes the complete implementation of M5Paper v1.1 support in TzCoinMiner.

## Hardware

- **Board**: M5Paper v1.1
- **MCU**: ESP32 D0WDQ6-V3
- **Display**: IT8951 E-ink 960x540 pixels (4.7" 16-level greyscale)
- **Touch**: GT911 capacitive touch controller
- **Rotazione**: Landscape (90°) - 540x960 effective resolution
- **Flash**: 16MB
- **PSRAM**: 8MB
- **Battery**: Built-in with IP5306 power management
- **Physical Buttons**: 
  - GPIO38: Click (center wheel button)
  - GPIO37: Up (wheel rotation)
  - GPIO39: Down (wheel rotation)

## Configurazione PlatformIO

L'environment per M5Paper è configurato in `platformio.ini`:

```ini
[env:m5paper]
platform = espressif32@^5.1.0
board = m5stack-fire
framework = arduino
monitor_speed = 115200
upload_speed = 1500000
build_flags = 
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=0
    -D CONFIG_FREERTOS_UNICORE=0
    -D CONFIG_MBEDTLS_HARDWARE_SHA=1
    -D DISPLAY_TYPE_M5PAPER=1
    -D M5PAPER_V1_1=1
    -D CONFIG_ESP_TASK_WDT_TIMEOUT_S=30
    -O3
    -funroll-loops
    -ffast-math
board_build.partitions = partitions_16mb.csv
board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
board_build.flash_mode = qio
lib_deps = 
    bblanchon/ArduinoJson@^7.2.0
    m5stack/M5EPD@^0.1.5
```

### Build Flags Explained

- `DISPLAY_TYPE_M5PAPER=1`: Identifies M5Paper board and activates specific code
- `M5PAPER_V1_1=1`: Specifies hardware version 1.1
- `CONFIG_ESP_TASK_WDT_TIMEOUT_S=30`: Extended watchdog timeout for E-ink operations
- `board = m5stack-fire`: Uses M5Stack vendor configuration for library compatibility

## Architettura Software

### Files Dedicated to M5Paper Display

- `src/display_m5paper.h`: Header with definitions, touch structures and function prototypes
- `src/display_m5paper.cpp`: Complete E-ink display and touch handling implementation

### Monitor Task on Core 1

To avoid WDT conflicts and ensure smooth display updates:
- **Core 0**: Mining workers and main logic
- **Core 1**: Dedicated monitor task for display management and UI commands
- **Communication**: FreeRTOS queue for display-safe commands

### Conditional Management in main.cpp

The `main.cpp` file uses preprocessor directives to automatically select the correct code:

```cpp
#ifdef DISPLAY_TYPE_M5PAPER
    #include "display_m5paper.h"
    #define PAGE_LOGO_TYPE PAGE_LOGO_M5
    #define PAGE_MINING_TYPE PAGE_MINING_M5
    #define PAGE_SETUP_TYPE PAGE_SETUP_M5
#else
    #include "display.h"
    #define PAGE_LOGO_TYPE PAGE_LOGO
    #define PAGE_MINING_TYPE PAGE_MINING
    #define PAGE_SETUP_TYPE PAGE_SETUP
#endif
```

## E-ink Display Features

### Refresh Modes

The IT8951 E-ink display supports several optimized refresh modes:

- **UPDATE_MODE_INIT**: Full refresh with initialization (used only at startup)
- **UPDATE_MODE_GC16**: High quality, 16 grayscale levels - used for page changes (~450ms, with flash)
- **UPDATE_MODE_DU4**: Fast update, 4 grayscale levels - used for dynamic stats (~260ms, no flash)
- **UPDATE_MODE_GL16**: Balanced update, 16 grayscale levels
- **UPDATE_MODE_A2**: Ultra-fast binary mode (not used)

### Optimized Canvas System

Two separate canvases to maximize performance:

1. **canvas_page** (540x960): For static content and page changes
   - Full refresh with UPDATE_MODE_GC16
   - Used only when page or static content changes
   
2. **canvas_stats** (400x60): For dynamic updates (hashrate, shares)
   - Fast partial refresh with UPDATE_MODE_DU4
   - Maximum 1 update per second
   - Precise positioning with pushCanvas(x, y)

### Anti-Ghosting Optimizations

To maximize quality and display lifespan:

1. **Change Detection**: Track previous values to avoid unnecessary refreshes
2. **Periodic Full Refresh**: Every 60 seconds to prevent permanent ghosting
3. **Page-Change Detection**: Force full refresh on every page change
4. **Smart Update Logic**: Compare values (hashrate, shares, wifi status) before updating

### Grayscale Mapping

M5EPD uses inverted scale:
- `0` = White (background)
- `15` = Black (text, borders)
- Intermediate values = Gray shades

## UI/UX Design

### 3-Page Layout

#### 1. PAGE_LOGO_M5 (Main Page)
**Content:**
- **Top-left**: Current date/time (updates every second)
- **Top-right**: Battery level (e.g., "Batt: 81%")
- **Center**: "TzCoinMiner" logo + "M5Paper Edition"
- **Info**: Coin type (Bitcoin/Duino-Coin), WiFi status
- **Buttons**: [Stats] [Settings] - capacitive touch

**Touch Buttons:**
- **Stats**: Direct navigation to PAGE_MINING_M5
- **Settings**: Direct navigation to PAGE_SETUP_M5

#### 2. PAGE_MINING_M5 (Mining Statistics)
**Content:**
- **Top-left**: Date/time
- **Top-right**: Battery level
- **Title**: "Solo Mining" / "Pool Mining" / "Duino-Coin"
- **Pool Info**: Pool URL and port (e.g., "Pool: public-pool.io:21496")
- **Status**: "Status: active" / "Status: inactive" / "Status: no wifi"
- **Dynamic stats** (updated every second with partial refresh):
  - `H/s`: Current hashrate / Total hashes (in millions)
  - `best`: Best difficulty found (zeros per core - e.g., "5z - 4z")
  - `pool`: Pool status ("connected" / "active" / "connecting")
  - `shares`: Submitted/Accepted (with K notation for large numbers)
  - `found`: Total best difficulty in scientific format
- **Coin badge**: BTC/BCH/DUCO large text (bottom-right)
- **Button**: [Back] - return to logo

**Partial Updates:**
Stats are updated only if they change, using small canvas_stats for speed

#### 3. PAGE_SETUP_M5 (Configuration)
**Content:**
- **Top-left**: Date/time
- **Top-right**: Battery level
- **Title**: "Setup"
- **WiFi Status**: 
  - If connected: SSID + IP address
  - If disconnected: "WiFi: Disconnected"
- **AP Mode Info** (visible only when AP is active):
  - "AP SSID: TzCoinMinerWifi"
  - "AP Password: theansweris42"
  - "Connect to the WiFi AP above"
  - "to configure the device"
- **Mining Info**:
  - Mode: Solo/Pool/Duino-Coin
  - Mining: Active/Stopped
- **Buttons**: [Back] [Configure/Disconnect]
  - **Configure**: Outline button - activates AP mode
  - **Disconnect**: Filled button (black bg, white text) - deactivates AP mode

**Dynamic Button:**
The second button changes appearance and text based on AP mode state

## Touch Handling

### GT911 Capacitive Touch Controller

**Initialization:**
```cpp
M5.TP.SetRotation(90);  // Match display rotation
```

**Touch State Machine:**

`TouchState` structure:
```cpp
struct TouchState {
    bool isPressed;      // Finger currently down on button
    bool justReleased;   // Finger just lifted (trigger action)
    int buttonNumber;    // Which button (1, 2, 3...)
    int x, y;            // Touch coordinates
};
```

**Anti-Bounce Logic:**
1. **Touch Detection**: `M5.TP.available()` + `M5.TP.update()`
2. **Debounce**: 200ms cooldown after each release to prevent phantom touches
3. **False Touch Filter**: Ignores (0,0) coordinates which are false positives
4. **Release Detection**: `M5.TP.isFingerUp()` indicates when finger is lifted
5. **Page Validation**: Action executed only if `pressedPage == currentPage`

**Touch-Release Pattern:**
- **On Press**: Records button and page, no action
- **On Release**: Executes action ONLY if still on the same page
- Prevents touch propagation during page transitions

**Button Area Detection:**

Each page has a specific layout (landscape coordinates 540x960):
- **Y range**: 860-920 (bottom 60px + 40px margin)
- **Button size**: 140x60px with border-radius 12px

**Page 0** (Logo): 2 centered buttons
```
[Stats: 110-250]  [Settings: 290-430]
Spacing: 40px
```

**Page 1** (Mining): 1 centered button
```
[Back: 200-340]
```

**Page 2** (Setup): 2 centered buttons
```
[Back: 110-250]  [Configure/Disconnect: 290-430]
Spacing: 40px
```

### Visual Feedback Helpers

Two functions for button rendering:

**draw_outline_button()**: Normal button
- Black 2px border
- White background
- Centered black text

**draw_filled_button()**: Active/pressed button
- Black background
- Centered white text
- Used for "Disconnect" state in AP mode

## Display Commands Queue

### DisplayCommand Enum

Commands sent from main loop to Monitor task via queue:

```cpp
enum DisplayCommand {
    DISPLAY_CMD_REFRESH,      // Force refresh current page
    DISPLAY_CMD_NEXT_PAGE,    // Sequential navigation (not used in touch UI)
    DISPLAY_CMD_PREV_PAGE,    // Sequential navigation (not used in touch UI)
    DISPLAY_CMD_GOTO_LOGO,    // Direct jump to logo page
    DISPLAY_CMD_GOTO_MINING,  // Direct jump to mining page
    DISPLAY_CMD_GOTO_SETUP,   // Direct jump to setup page
    DISPLAY_CMD_TOGGLE_WIFI,  // Toggle AP mode on/off
    DISPLAY_CMD_TOGGLE_MINING // Toggle mining on/off (not implemented)
};
```

**Command Flow:**
1. Touch handler detects button release
2. Main loop sends command to queue
3. Monitor task (Core 1) processes command
4. Display updates without blocking mining

## Battery Monitoring

### getBatteryPercentage() Function

Reads battery voltage via M5EPD and converts to percentage:

```cpp
uint32_t vol = M5.getBatteryVoltage();  // In millivolts
// Range: 3.0V (0%) to 4.2V (100%)
// Linear interpolation
int percentage = ((vol - 3000) * 100) / 1200;
```

**Display:**
- Format: "Batt: XX%"
- Position: Top-right on all pages
- Update: On every page refresh (so 1/sec on mining page)

## Time/Date Display

**Source**: NTP via WiFi with configurable timezone

**Format**: Automatic from `struct tm`
```cpp
strftime(buffer, sizeof(buffer), "%d/%m/%Y %H:%M:%S", &timeinfo);
```

**Position**: Top-left on all pages (10, 10)

**Update Frequency**: 
- Logo page: on every redraw (when data changes)
- Mining page: every second (along with stats)
- Setup page: every second

## Build e Upload

### Build per M5Paper
```bash
pio run -e m5paper
```

### Upload su M5Paper
```bash
pio run -e m5paper -t upload
```

### Monitor Seriale
```bash
pio device monitor -e m5paper
```

### One-liner: Upload + Monitor
```bash
pio run -e m5paper --target upload && pio device monitor -e m5paper
```

## Flash Partitions

`partitions_16mb.csv` file optimized for 16MB flash:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x640000,
app1,     app,  ota_1,   0x650000,0x640000,
spiffs,   data, spiffs,  0xC90000,0x360000,
coredump, data, coredump,0xFF0000,0x10000,
```

**Features:**
- **OTA**: 2 partitions of 6.4MB each for firmware updates
- **SPIFFS**: 3.4MB for configuration and file storage
- **Coredump**: 64KB for crash debugging

## Performance Metrics

### Typical Hashrate
- **Bitcoin/BCH**: 10-12 kH/s (kilo-hashes per second)
- **Duino-Coin**: 40-50 H/s (hashes per second, variable difficulty)

### Display Performance
- **Full page refresh**: ~450ms (UPDATE_MODE_GC16)
- **Partial stats update**: ~260ms (UPDATE_MODE_DU4)
- **Touch response**: < 200ms (including debounce)
- **Stats update rate**: 1 Hz (massimo, se dati cambiano)

### Memory Usage
- **RAM**: ~49KB / 4.5MB (1.1%)
- **Flash**: ~1.23MB / 6.5MB (18.8%)
- **Canvas buffers**: ~540KB PSRAM
- **Task stacks**: Monitor 10KB, Mining workers 8KB each

## Differenze rispetto a T-Display S3

| Feature | T-Display S3 AMOLED | M5Paper E-ink |
|---------|---------------------|---------------|
| Resolution | 536x240 | 540x960 (landscape) |
| Technology | AMOLED RGB | E-ink Greyscale |
| Colors | 65K colors | 16 grayscale levels |
| Touch | Resistive | Capacitive GT911 |
| Input | 2 physical buttons | Touch buttons + 3 wheel buttons |
| Refresh Rate | ~50fps (logo), 1-5fps (stats) | Max 1fps, adaptive |
| Refresh Time | Instant | 260-450ms |
| Display Power | High (backlight always on) | Very low (only during update) |
| Sunlight Visibility | Poor (reflections) | Excellent (e-paper) |
| Ghosting | No | Yes (mitigated with periodic refreshes) |
| Battery Life | Hours | Days/Weeks |
| Ideal for | Desktop, indoor | Outdoor, battery-powered, remote |

## Troubleshooting

### Display not updating
**Cause**: Missing initialization or canvas not sent
**Solution**:
```cpp
M5.EPD.Clear(true);  // Force full clear
canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
```

### Persistent ghosting
**Cause**: Too many partial refreshes without full refresh
**Solution**: 
- Verify that `last_full_refresh` tracking works
- Force `UPDATE_MODE_GC16` for page changes
- Reduce `FULL_REFRESH_INTERVAL_MS` interval if necessary

### Touch not responding
**Cause**: Touch rotation not matching display or debounce too long
**Solution**:
```cpp
M5.TP.SetRotation(90);  // Must match M5.EPD.SetRotation()
```
Verify that `lastReleaseTime` debounce is not too high (200ms is ok)

### Phantom Touches (0,0)
**Cause**: Known GT911 bug with false positives
**Solution**: Already implemented filter in `display_m5paper_check_touch()`:
```cpp
if (finger.x == 0 && finger.y == 0) {
    return state;  // Ignore
}
```

### Touch propagates between pages
**Cause**: Action executed during page transition
**Solution**: Already implemented page validation:
```cpp
if (pressedPage == currentPage) {
    // Execute action only if still on same page
}
```

### "Pool: connecting" even when connected
**Cause**: `pool_connected` flag not copied in `mining_get_stats()`
**Solution**: Already fixed - flag is now copied from stats_worker0

### High power consumption
**Cause**: Display refreshing too frequently
**Solution**:
- Verify change detection: `data_changed` logic
- Check that partial updates use small canvas_stats
- Verify that UPDATE_MODE_DU4 is used for stats (not GC16)

### WDT Reset / Panic
**Cause**: Display operations too long on Core 0 blocking mining
**Solution**: Already implemented - Monitor task on separate Core 1:
```cpp
xTaskCreatePinnedToCore(runMonitor, "MonitorTask", 10000, NULL, 1, NULL, 1);
```

## Code Examples

### Add new display command

1. **Add enum**:
```cpp
enum DisplayCommand {
    // ... existing ...
    DISPLAY_CMD_MY_NEW_COMMAND
};
```

2. **Handler in Monitor task**:
```cpp
case DISPLAY_CMD_MY_NEW_COMMAND:
    // Your logic here
    Serial.println("[MONITOR] Executing my new command");
    currentPage = PAGE_SOMETHING;
    lastDisplayUpdate = 0;  // Force refresh
    break;
```

3. **Trigger from main loop**:
```cpp
DisplayCommand cmd = DISPLAY_CMD_MY_NEW_COMMAND;
xQueueSend(displayQueue, &cmd, 0);
```

### Add new touch button

1. **Define area in display_m5paper.cpp**:
```cpp
if (current_page_num == YOUR_PAGE) {
    const int btnW = 140;
    const int startX = (M5PAPER_WIDTH - btnW) / 2;
    if (finger.x >= startX && finger.x < (startX + btnW)) {
        buttonPressed = 1;  // Your button
    }
}
```

2. **Handler in main.cpp**:
```cpp
if (currentPage == YOUR_PAGE) {
    if (pressedButton == 1) {
        Serial.println("[TOUCH] Your button pressed");
        DisplayCommand cmd = DISPLAY_CMD_WHATEVER;
        xQueueSend(displayQueue, &cmd, 0);
    }
}
```

### Add new display metric

1. **Canvas stats update**:
```cpp
canvas_stats.fillCanvas(0);
snprintf(buffer, sizeof(buffer), "Value: %u", your_metric);
canvas_stats.drawString(buffer, 0, 0);
canvas_stats.pushCanvas(200, YOUR_Y_POSITION, UPDATE_MODE_DU4);
```

2. **Change detection**:
```cpp
static uint32_t last_your_metric = 0;
bool data_changed = (your_metric != last_your_metric);
// ... update logic ...
last_your_metric = your_metric;
```

## Final Notes

### E-ink Best Practices

1. **Minimize Full Refreshes**: Expensive in time and power
2. **Use Partial Updates Intelligently**: Only for data that changes
3. **Change Detection Mandatory**: Never update if data hasn't changed
4. **Canvas Size Matters**: Small canvas = fast updates
5. **Periodic Anti-Ghosting**: Full refresh every 60 sec minimum

### Possible Future Optimizations

- [ ] Sleep mode when inactive (display off after X minutes)
- [ ] Touch gesture support (swipe for page changes)
- [ ] Custom fonts for more elaborate UI
- [ ] QR code display for wallet address
- [ ] Hashrate trend graphs (using smart partial updates)
- [ ] OTA update via web interface

### Known Limitations

1. **No Animations**: E-ink too slow for smooth animations
2. **Refresh Artifacts**: Black flash during GC16 update is unavoidable
3. **Touch Precision**: GT911 sometimes less precise at edges
4. **Battery Reading**: Accuracy ~±5% (depends on IP5306 calibration)
5. **Memory Constraints**: Fullscreen canvas uses 540KB PSRAM

## Riferimenti

- [M5Paper Docs](https://docs.m5stack.com/en/core/m5paper)
- [M5EPD Library](https://github.com/m5stack/M5EPD)
- [IT8951 Controller](https://www.waveshare.com/wiki/IT8951)
- [GT911 Touch Controller](https://github.com/goodix/gt9xx)
- [ESP32 FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html)
