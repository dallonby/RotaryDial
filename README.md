# M5Stack Dial FreeSleep Temperature Controller

A beautiful, intuitive temperature controller for [FreeSleep](https://github.com/throwaway31265/free-sleep) bed temperature systems, built on the M5Stack Dial (ESP32-S3). Control your bed and pillow temperatures with a rotary dial interface featuring a colorful arc display, automatic night mode, and full FreeSleep API integration.

## Features

### Temperature Control
- **Dual Zone Control**: Independent temperature setpoints for bed and pillow zones
- **Rotary Dial Interface**: Smooth 0.5°C increments with tactile feedback
- **Touch Arc Control**: Tap anywhere on the temperature arc to jump to that temperature
- **Temperature Range**: 10°C to 35°C (converted to 55-110°F for FreeSleep API)
- **Visual Temperature Arc**: Color-coded gradient from blue (cold) through green/yellow to red (hot)
- **Setpoint Indicators**: Outer marker for bed, inner marker for pillow - filled when active, outlined when inactive

### FreeSleep Integration
- **Automatic Sync**: Fetches current temperature and power state from FreeSleep API on startup
- **Debounced Updates**: API calls are batched (500ms delay) to prevent conflicts while adjusting
- **Power Control**: Short tap on temperature to toggle bed/pillow power on/off
- **Bed Side Selection**: Configure which side of the bed (Left/Right) to control
- **Dual Controller Support**: Configure separate IP addresses for bed and pillow FreeSleep controllers

### Automatic Night Mode
The display automatically switches to a red-only color scheme during night hours to preserve your night vision and minimize sleep disruption:
- **Automatic Activation**: Night mode activates between 10pm and 7am (configurable in `config.h`)
- **Red Color Theme**: All UI elements switch to red tones that are easier on your eyes in darkness
- **Reduced Brightness**: Display brightness automatically reduces to 20% during night hours
- **Manual Override**: Long press (500ms+) on the temperature display to toggle night mode manually
- **Real-Time Clock**: Time synced via NTP on startup and maintained by the ESP32's RTC

### Smart Display Dimming
- **Activity Timeout**: Screen dims to ~1% brightness after 10 seconds of inactivity
- **Instant Wake**: Any touch or dial rotation immediately wakes the display
- **Preserves Display Life**: Minimal power draw when not in active use

### REST API
The controller exposes a local REST API for integration with home automation systems:
- `GET /api/temperature` - Current active setpoint and mode
- `POST /api/temperature` - Set active setpoint
- `GET /api/bed` - Bed temperature setpoint
- `POST /api/bed` - Set bed temperature
- `GET /api/pillow` - Pillow temperature setpoint
- `POST /api/pillow` - Set pillow temperature
- `GET /api/config/bed-ip` - Get bed controller IP
- `POST /api/config/bed-ip` - Set bed controller IP
- `GET /api/config/pillow-ip` - Get pillow controller IP
- `POST /api/config/pillow-ip` - Set pillow controller IP

### Persistent Settings
All configuration is saved to non-volatile storage (NVS) and persists across reboots:
- Bed and pillow controller IP addresses
- WiFi credentials (when configured via on-device menu)
- Bed side preference (Left/Right)

## Hardware Requirements

- **M5Stack Dial** - ESP32-S3 based rotary dial with 240x240 round touchscreen
- **FreeSleep server** running on your network (one or two instances for bed/pillow zones)

## Setup Instructions

### 1. Install PlatformIO

PlatformIO is required to build and flash the firmware. Install it via one of these methods:

**Option A: VS Code Extension (Recommended)**
1. Install [Visual Studio Code](https://code.visualstudio.com/)
2. Open VS Code and go to Extensions (Ctrl+Shift+X / Cmd+Shift+X)
3. Search for "PlatformIO IDE" and install it
4. Restart VS Code when prompted

**Option B: Command Line**
```bash
# Using pip
pip install platformio

# Or using Homebrew (macOS)
brew install platformio
```

### 2. Clone the Repository

```bash
git clone https://github.com/yourusername/RotaryDial.git
cd RotaryDial
```

### 3. Configure WiFi Credentials

**This is the recommended method** - much easier than using the on-device WiFi scanner!

1. Copy the example credentials file:
   ```bash
   cp include/credentials.h.example include/credentials.h
   ```

2. Edit `include/credentials.h` with your WiFi details:
   ```c
   #ifndef CREDENTIALS_H
   #define CREDENTIALS_H

   #define WIFI_SSID "YourWiFiNetworkName"
   #define WIFI_PASSWORD "YourWiFiPassword"

   #endif
   ```

The `credentials.h` file is gitignored to keep your credentials private.

### 4. Configure Timezone (Optional)

Edit `include/config.h` to set your timezone for accurate night mode timing:

```c
#define GMT_OFFSET_SEC 0            // Seconds offset from GMT (e.g., -18000 for EST, 3600 for CET)
#define DAYLIGHT_OFFSET_SEC 0       // Daylight saving offset in seconds (typically 3600 when active)
```

Common timezone offsets:
- **EST (US Eastern)**: `-18000` (UTC-5)
- **CST (US Central)**: `-21600` (UTC-6)
- **PST (US Pacific)**: `-28800` (UTC-8)
- **GMT/UTC**: `0`
- **CET (Central Europe)**: `3600` (UTC+1)
- **JST (Japan)**: `32400` (UTC+9)

### 5. Build and Flash

Connect your M5Stack Dial via USB-C and run:

```bash
# Using PlatformIO CLI
pio run --target upload

# Or in VS Code with PlatformIO extension:
# Click the "Upload" arrow button in the bottom toolbar
```

The firmware will compile and automatically upload to your M5Stack Dial.

### 6. Configure FreeSleep IP Addresses

On first boot, you'll need to configure the IP addresses of your FreeSleep server(s):

1. Tap the time/IP area at the bottom of the screen to open Settings
2. Navigate to "Bed Controller IP" and click to edit
3. Use the rotary dial to adjust each octet (0-255), click to advance to next octet
4. After the 4th octet, the IP is saved automatically
5. Repeat for "Pillow Controller IP" if using a separate controller

## Usage Guide

### Main Temperature Screen

The main interface shows:
- **Temperature Arc**: Visual indicator of current setpoint with color gradient
- **Temperature Value**: Large digital display in the center
- **Setpoint Markers**: Outer circle for bed, inner circle for pillow
- **Mode Buttons**: Pillow (left) and Bed (right) icons at the bottom
- **Clock**: Current time display
- **IP Address**: Device's IP address for API access

### Controls

| Action | Result |
|--------|--------|
| **Rotate dial** | Adjust temperature (0.5°C per detent) |
| **Press dial button** | Reset to default temperature (21°C) |
| **Tap temperature arc** | Jump to that temperature |
| **Short tap center** | Toggle power ON/OFF for current mode |
| **Long tap center (500ms+)** | Toggle night mode override |
| **Tap pillow icon (left)** | Switch to pillow mode |
| **Tap bed icon (right)** | Switch to bed mode |
| **Tap time/IP area** | Open settings menu |

### Power State Indicator

When the FreeSleep side is powered OFF:
- Temperature display is dimmed
- "OFF" appears in red below the temperature
- Short tap the center to turn power back ON

### Settings Menu

Access by tapping the time/IP area at the bottom of the main screen.

| Setting | Description |
|---------|-------------|
| **WiFi Settings** | Scan for networks and connect (see note below) |
| **Bed Controller IP** | IP address of your bed FreeSleep server |
| **Pillow Controller IP** | IP address of your pillow FreeSleep server |
| **Bed Side** | Toggle between Left/Right side of the bed |
| **Night Mode** | Shows current status (Auto/Override ON) - tap to toggle |
| **Temperature Mode** | Shows current mode (Bed/Pillow) - tap to toggle |

**Navigation:**
- Rotate dial to scroll through options
- Press dial button to select/toggle
- Tap screen to exit settings

### WiFi Configuration Note

While the device includes an on-device WiFi scanner and password entry system, **configuring WiFi via `credentials.h` is strongly recommended**. The on-device password entry requires scrolling through characters one-by-one using the rotary dial, which is extremely tedious for complex passwords.

If you must use on-device WiFi configuration:
1. Open Settings > WiFi Settings
2. Wait for network scan to complete
3. Rotate to select your network, press to select
4. Rotate to select each character, press to add it to the password
5. Long press (1 second) to submit the password and connect

## Configuration Reference

All configuration options in `include/config.h`:

| Setting | Default | Description |
|---------|---------|-------------|
| `TEMP_MIN` | 10.0°C | Minimum temperature |
| `TEMP_MAX` | 35.0°C | Maximum temperature |
| `TEMP_DEFAULT` | 21.0°C | Default/reset temperature |
| `TEMP_STEP` | 0.5°C | Temperature change per encoder detent |
| `API_PORT` | 80 | HTTP API port |
| `BRIGHTNESS_DAY` | 255 | Day mode brightness (0-255) |
| `BRIGHTNESS_NIGHT` | 51 | Night mode brightness (~20%) |
| `BRIGHTNESS_DIM` | 2 | Idle dimmed brightness (~1%) |
| `DIM_TIMEOUT_MS` | 10000 | Inactivity timeout before dimming (ms) |
| `NIGHT_START_HOUR` | 22 | Night mode start (24h format) |
| `NIGHT_END_HOUR` | 7 | Night mode end (24h format) |
| `NTP_SERVER` | pool.ntp.org | NTP time server |
| `GMT_OFFSET_SEC` | 0 | Timezone offset from GMT |
| `DAYLIGHT_OFFSET_SEC` | 0 | Daylight saving time offset |

## Troubleshooting

### Device won't connect to WiFi
- Verify credentials in `credentials.h` are correct
- Ensure your WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check that the device is within range of your router

### FreeSleep API not responding
- Verify the FreeSleep server is running and accessible
- Check the IP address is correctly configured
- Ensure port 3000 is accessible on your network

### Temperature not syncing
- The device syncs on startup - restart to re-fetch current temperatures
- Check serial monitor for API error messages
- Verify bed side (Left/Right) matches your FreeSleep configuration

### Night mode not activating at correct times
- Check timezone settings in `config.h`
- Ensure device successfully synced time with NTP on startup
- The clock display confirms time sync is working

## License

MIT License - See LICENSE file for details.

## Acknowledgments

- [FreeSleep](https://github.com/throwaway31265/free-sleep) - Open source bed temperature control
- [M5Stack](https://m5stack.com/) - For the excellent Dial hardware
- [PlatformIO](https://platformio.org/) - Build system and library management
