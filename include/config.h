#ifndef CONFIG_H
#define CONFIG_H

// WiFi Configuration - Update these with your network credentials
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Temperature Settings
#define TEMP_MIN 10.0f      // Minimum temperature (Celsius)
#define TEMP_MAX 35.0f      // Maximum temperature (Celsius)
#define TEMP_DEFAULT 21.0f  // Default temperature setpoint
#define TEMP_STEP 0.5f      // Temperature increment per encoder step

// API Server Settings
#define API_PORT 80

// Display Settings
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

// Colors (RGB565 format)
#define COLOR_BACKGROUND 0x0000    // Black
#define COLOR_ARC_BG 0x2104        // Dark gray
#define COLOR_ARC_COLD 0x001F      // Blue
#define COLOR_ARC_WARM 0xFD20      // Orange
#define COLOR_ARC_HOT 0xF800       // Red
#define COLOR_TEXT 0xFFFF          // White
#define COLOR_SETPOINT 0x07E0      // Green

#endif // CONFIG_H
