#include <M5Dial.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"

Preferences preferences;

// Global variables
float bedSetpoint = TEMP_DEFAULT;
float pillowSetpoint = TEMP_DEFAULT;
bool wifiConnected = false;
long lastEncoderPosition = 0;
unsigned long lastActivityTime = 0;
unsigned long lastClockUpdate = 0;
bool isDimmed = false;
bool timeInitialized = false;
bool pillowModeActive = false;  // false = bed mode (default), true = pillow mode
bool nightModeOverride = false;  // Manual night mode override
bool inSettingsMenu = false;     // Whether settings menu is active

// Saved WiFi credentials
String savedWifiSSID = "";
String savedWifiPassword = "";

// Bed side setting (true = right, false = left)
bool bedSideRight = false;  // Default to left side

// Temperature unit setting (true = Fahrenheit, false = Celsius)
bool useFahrenheit = false;  // Default to Celsius

// FreeSleep power state (true = on, false = off)
bool bedPowerOn = true;      // Assume on until we fetch status
bool pillowPowerOn = true;   // Assume on until we fetch status

// Debounce for FreeSleep API updates
unsigned long lastSetpointChangeTime = 0;
bool pendingFreeSleepUpdate = false;
const unsigned long FREESLEEP_DEBOUNCE_MS = 500;  // Wait 500ms after last change before sending
const unsigned long SYNC_COOLDOWN_AFTER_CHANGE_MS = 1000;  // Don't sync from pod for 1s after user changes

// Periodic sync from FreeSleep API with exponential backoff
unsigned long lastFreeSleepSync = 0;
const unsigned long FREESLEEP_SYNC_INTERVAL_MS = 2000;  // Sync every 2 seconds when successful
unsigned long currentSyncInterval = FREESLEEP_SYNC_INTERVAL_MS;
const unsigned long MAX_SYNC_INTERVAL_MS = 60000;  // Max backoff of 60 seconds
int consecutiveFailures = 0;

// Track night mode state to detect changes
bool wasNightMode = false;

// Touch duration tracking for center tap
unsigned long centerTouchStartTime = 0;
unsigned long lastCenterTapTime = 0;
bool centerTouchActive = false;
// Touch duration thresholds:
// < 200ms: wake/brightness only
// 200-1000ms: power toggle
// 1000-3000ms: night mode toggle
// > 3000ms: open settings menu
const unsigned long TAP_MIN_MS = 200;
const unsigned long POWER_MAX_MS = 1000;
const unsigned long NIGHT_MODE_MAX_MS = 3000;
const unsigned long TAP_DEBOUNCE_MS = 500;  // Minimum time between taps

// Menu navigation
enum MenuItem {
    MENU_WIFI_SETTINGS = 0,
    MENU_BED_IP,
    MENU_PILLOW_IP,
    MENU_BED_SIDE,
    MENU_TEMP_UNIT,
    MENU_NIGHT_MODE,
    MENU_TEMPERATURE_MODE,
    MENU_COUNT  // Total number of menu items
};

enum SubMenu {
    SUBMENU_NONE = 0,
    SUBMENU_WIFI_SCAN,
    SUBMENU_WIFI_PASSWORD,
    SUBMENU_IP_EDITOR
};

MenuItem currentMenuItem = MENU_WIFI_SETTINGS;
SubMenu currentSubMenu = SUBMENU_NONE;
int menuScrollOffset = 0;  // Smooth scrolling offset

// IP editor state
int ipEditorOctet = 0;  // Which octet (0-3) is being edited
int ipEditorDigit = 0;  // Which digit (0-2) within octet
bool editingBedIP = false;
uint8_t tempIPOctets[4] = {192, 168, 1, 1};  // Temporary IP being edited

// WiFi scanning
String scannedSSIDs[20];
int scannedSSIDCount = 0;
int selectedSSIDIndex = 0;
String wifiPasswordInput = "";
int passwordCharIndex = 0;  // Current character being edited
const char alphaNumeric[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!@#$%^&*()_+-=[]{}|;:',.<>?/ ";

// Target IP addresses for bed and pillow controllers
IPAddress bedTargetIP(192, 168, 1, 44);     // Default bed controller IP
IPAddress pillowTargetIP(192, 168, 1, 14);  // Default pillow controller IP

// Web server
WebServer server(API_PORT);

// Display dimensions
const int centerX = SCREEN_WIDTH / 2;
const int centerY = SCREEN_HEIGHT / 2;
const int arcRadius = 100;
const int arcThickness = 15;

// Sprite for double buffering
LGFX_Sprite sprite(&M5Dial.Display);

// Function prototypes
void setupWiFi();
void setupWebServer();
void setupNTP();
void drawTemperatureUI();
void drawSettingsMenu();
void drawIPEditor();
void drawWiFiScanner();
void drawPasswordEntry();
void updateClockDisplay();
void drawArc(int startAngle, int endAngle, uint16_t color);
void handleEncoderInput();
void handleEncoderInSettings();
void handleEncoderInIPEditor();
void handleEncoderInWiFiScanner();
void handleEncoderInPasswordEntry();
void handleTouchInput();
void handleAPIRoot();
void handleAPITemperature();
void handleAPISetTemperature();
void handleAPIBedTemperature();
void handleAPISetBedTemperature();
void handleAPIPillowTemperature();
void handleAPISetPillowTemperature();
void handleNotFound();
void updateBrightness();
void recordActivity();
bool isNightTime();
uint16_t getTemperatureColor(float temp);
uint16_t getTemperatureColorNight(float temp);
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);
float& getActiveSetpoint();
float& getInactiveSetpoint();
String getMenuItemName(MenuItem item);
void startIPEditor(bool isBedIP);
void startWiFiScanner();
void startPasswordEntry();

// FreeSleep API functions
float celsiusToFahrenheit(float celsius);
float fahrenheitToCelsius(float fahrenheit);
bool fetchFreeSleepTemperature(IPAddress ip, const char* side, float& tempCelsius, bool& isOn);
bool setFreeSleepTemperature(IPAddress ip, const char* side, float tempCelsius);
bool setFreeSleepPower(IPAddress ip, const char* side, bool powerOn);
void syncTemperaturesFromFreeSleep();
void syncFromFreeSleep();
void toggleActivePower();

void setup() {
    // Initialize M5Dial
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);  // Enable encoder, disable RFID

    Serial.begin(115200);
    Serial.println("\n\nM5Stack Dial Temperature Controller");
    Serial.println("====================================");

    // Load saved settings from NVS
    preferences.begin("tempctrl", false);
    bedTargetIP = IPAddress(
        preferences.getUChar("bedIP0", 192),
        preferences.getUChar("bedIP1", 168),
        preferences.getUChar("bedIP2", 1),
        preferences.getUChar("bedIP3", 100)
    );
    pillowTargetIP = IPAddress(
        preferences.getUChar("pillowIP0", 192),
        preferences.getUChar("pillowIP1", 168),
        preferences.getUChar("pillowIP2", 1),
        preferences.getUChar("pillowIP3", 101)
    );
    Serial.printf("Loaded Bed IP: %s\n", bedTargetIP.toString().c_str());
    Serial.printf("Loaded Pillow IP: %s\n", pillowTargetIP.toString().c_str());

    // Load saved WiFi credentials
    savedWifiSSID = preferences.getString("wifiSSID", "");
    savedWifiPassword = preferences.getString("wifiPass", "");
    if (savedWifiSSID.length() > 0) {
        Serial.printf("Loaded saved WiFi: %s\n", savedWifiSSID.c_str());
    }

    // Load bed side setting
    bedSideRight = preferences.getBool("bedSideRight", false);
    Serial.printf("Loaded bed side: %s\n", bedSideRight ? "Right" : "Left");

    // Load temperature unit setting
    useFahrenheit = preferences.getBool("useFahrenheit", false);
    Serial.printf("Loaded temp unit: %s\n", useFahrenheit ? "Fahrenheit" : "Celsius");

    // Initialize display
    M5Dial.Display.setRotation(0);
    M5Dial.Display.fillScreen(COLOR_BACKGROUND);
    M5Dial.Display.setTextColor(COLOR_TEXT);
    M5Dial.Display.setTextDatum(middle_center);

    // Create sprite for double buffering
    sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);

    // Show startup message
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString("Connecting to WiFi...", centerX, centerY);

    // Connect to WiFi
    setupWiFi();

    // Setup web server
    setupWebServer();

    // Setup NTP time sync
    setupNTP();

    // Sync temperatures from FreeSleep API
    if (wifiConnected) {
        syncTemperaturesFromFreeSleep();
    }

    // Get initial encoder position
    lastEncoderPosition = M5Dial.Encoder.read();

    // Initialize activity tracking
    lastActivityTime = millis();
    recordActivity();

    // Initialize night mode state
    wasNightMode = isNightTime();

    // Draw initial UI
    drawTemperatureUI();
}

void loop() {
    M5Dial.update();

    // Handle web server requests
    if (wifiConnected) {
        server.handleClient();
    }

    // Handle rotary encoder input
    handleEncoderInput();

    // Handle touch input
    handleTouchInput();

    // Update brightness based on activity and time
    updateBrightness();

    // Update clock display every second (only on main temperature screen)
    unsigned long currentMillis = millis();
    if (!inSettingsMenu && currentMillis - lastClockUpdate >= 1000) {
        lastClockUpdate = currentMillis;
        updateClockDisplay();
    }

    // Handle debounced FreeSleep API updates
    if (pendingFreeSleepUpdate && (currentMillis - lastSetpointChangeTime >= FREESLEEP_DEBOUNCE_MS)) {
        pendingFreeSleepUpdate = false;
        const char* side = bedSideRight ? "right" : "left";
        if (pillowModeActive) {
            setFreeSleepTemperature(pillowTargetIP, side, pillowSetpoint);
        } else {
            setFreeSleepTemperature(bedTargetIP, side, bedSetpoint);
        }
    }

    // Periodic sync from FreeSleep (temperature and power state)
    // Uses exponential backoff when failing to prevent UI freezing
    if (wifiConnected && !inSettingsMenu && !pendingFreeSleepUpdate &&
        (currentMillis - lastFreeSleepSync >= currentSyncInterval)) {
        lastFreeSleepSync = currentMillis;
        syncFromFreeSleep();
    }

    // Check for night mode changes (independent of FreeSleep sync)
    if (!inSettingsMenu) {
        bool currentNightMode = isNightTime();
        if (currentNightMode != wasNightMode) {
            wasNightMode = currentNightMode;
            Serial.printf("Night mode changed to: %s\n", currentNightMode ? "ON" : "OFF");
            drawTemperatureUI();
        }
    }

    delay(10);
}

void setupWiFi() {
    // Use saved credentials if available, otherwise use config defaults
    const char* ssid = savedWifiSSID.length() > 0 ? savedWifiSSID.c_str() : WIFI_SSID;
    const char* password = savedWifiPassword.length() > 0 ? savedWifiPassword.c_str() : WIFI_PASSWORD;

    Serial.printf("Connecting to WiFi: %s\n", ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;

        // Show connection progress
        M5Dial.Display.fillScreen(COLOR_BACKGROUND);
        M5Dial.Display.setTextSize(1);
        M5Dial.Display.drawString("Connecting to WiFi", centerX, centerY - 20);

        // Draw progress dots
        String dots = "";
        for (int i = 0; i < (attempts % 4); i++) dots += ".";
        M5Dial.Display.drawString(dots.c_str(), centerX, centerY + 10);
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\nWiFi Connected!");
        Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());

        // Show success message briefly
        M5Dial.Display.fillScreen(COLOR_BACKGROUND);
        M5Dial.Display.setTextColor(COLOR_SETPOINT);
        M5Dial.Display.drawString("WiFi Connected!", centerX, centerY - 20);
        M5Dial.Display.setTextColor(COLOR_TEXT);
        M5Dial.Display.drawString(WiFi.localIP().toString().c_str(), centerX, centerY + 10);
        delay(2000);
    } else {
        wifiConnected = false;
        Serial.println("\nWiFi Connection Failed!");

        // Show error message
        M5Dial.Display.fillScreen(COLOR_BACKGROUND);
        M5Dial.Display.setTextColor(COLOR_ARC_HOT);
        M5Dial.Display.drawString("WiFi Failed!", centerX, centerY - 10);
        M5Dial.Display.setTextColor(COLOR_TEXT);
        M5Dial.Display.drawString("Running offline", centerX, centerY + 10);
        delay(2000);
    }
}

void setupWebServer() {
    if (!wifiConnected) return;

    // API endpoints
    server.on("/", HTTP_GET, handleAPIRoot);
    server.on("/api/temperature", HTTP_GET, handleAPITemperature);
    server.on("/api/temperature", HTTP_POST, handleAPISetTemperature);
    server.on("/api/bed", HTTP_GET, handleAPIBedTemperature);
    server.on("/api/bed", HTTP_POST, handleAPISetBedTemperature);
    server.on("/api/pillow", HTTP_GET, handleAPIPillowTemperature);
    server.on("/api/pillow", HTTP_POST, handleAPISetPillowTemperature);
    server.on("/api/config/bed-ip", HTTP_GET, []() {
        JsonDocument doc;
        doc["ip"] = bedTargetIP.toString();
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    server.on("/api/config/bed-ip", HTTP_POST, []() {
        if (server.hasArg("plain")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, server.arg("plain"));
            if (!error && doc.containsKey("ip")) {
                String ipStr = doc["ip"].as<String>();
                if (bedTargetIP.fromString(ipStr)) {
                    Serial.printf("Bed target IP set to: %s\n", bedTargetIP.toString().c_str());
                    server.send(200, "application/json", "{\"success\":true}");
                    return;
                }
            }
        }
        server.send(400, "application/json", "{\"error\":\"Invalid IP address\"}");
    });
    server.on("/api/config/pillow-ip", HTTP_GET, []() {
        JsonDocument doc;
        doc["ip"] = pillowTargetIP.toString();
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    server.on("/api/config/pillow-ip", HTTP_POST, []() {
        if (server.hasArg("plain")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, server.arg("plain"));
            if (!error && doc.containsKey("ip")) {
                String ipStr = doc["ip"].as<String>();
                if (pillowTargetIP.fromString(ipStr)) {
                    Serial.printf("Pillow target IP set to: %s\n", pillowTargetIP.toString().c_str());
                    server.send(200, "application/json", "{\"success\":true}");
                    return;
                }
            }
        }
        server.send(400, "application/json", "{\"error\":\"Invalid IP address\"}");
    });
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.printf("HTTP server started on port %d\n", API_PORT);
}

void handleAPIRoot() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<title>M5Dial Temperature Controller</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: Arial; text-align: center; padding: 20px; background: #1a1a2e; color: #fff; }";
    html += ".temp { font-size: 72px; color: #00ff88; margin: 30px 0; }";
    html += ".unit { font-size: 24px; }";
    html += ".info { color: #888; margin: 10px 0; }";
    html += "</style></head><body>";
    html += "<h1>Temperature Controller</h1>";
    html += "<h2>" + String(pillowModeActive ? "Pillow" : "Bed") + " Mode</h2>";
    html += "<div class='temp'>" + String(getActiveSetpoint(), 1) + "<span class='unit'>&deg;C</span></div>";
    html += "<p class='info'>Bed: " + String(bedSetpoint, 1) + "&deg;C | Pillow: " + String(pillowSetpoint, 1) + "&deg;C</p>";
    html += "<p class='info'>API: GET/POST /api/temperature (active)</p>";
    html += "<p class='info'>API: GET/POST /api/bed</p>";
    html += "<p class='info'>API: GET/POST /api/pillow</p>";
    html += "<script>setInterval(()=>location.reload(), 5000);</script>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleAPITemperature() {
    JsonDocument doc;
    doc["setpoint"] = getActiveSetpoint();
    doc["mode"] = pillowModeActive ? "pillow" : "bed";
    doc["bed"] = bedSetpoint;
    doc["pillow"] = pillowSetpoint;
    doc["unit"] = "celsius";
    doc["min"] = TEMP_MIN;
    doc["max"] = TEMP_MAX;

    String response;
    serializeJson(doc, response);

    server.send(200, "application/json", response);
}

void handleAPISetTemperature() {
    if (server.hasArg("plain")) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        if (doc.containsKey("setpoint")) {
            float newTemp = doc["setpoint"].as<float>();

            // Clamp to valid range
            if (newTemp < TEMP_MIN) newTemp = TEMP_MIN;
            if (newTemp > TEMP_MAX) newTemp = TEMP_MAX;

            getActiveSetpoint() = newTemp;

            Serial.printf("%s temperature set via API: %.1f°C\n",
                         pillowModeActive ? "Pillow" : "Bed", newTemp);

            // Update display
            drawTemperatureUI();

            // Send response
            JsonDocument responseDoc;
            responseDoc["success"] = true;
            responseDoc["setpoint"] = getActiveSetpoint();
            responseDoc["mode"] = pillowModeActive ? "pillow" : "bed";

            String response;
            serializeJson(responseDoc, response);
            server.send(200, "application/json", response);
            return;
        }
    }

    server.send(400, "application/json", "{\"error\":\"Missing setpoint parameter\"}");
}

void handleAPIBedTemperature() {
    JsonDocument doc;
    doc["setpoint"] = bedSetpoint;
    doc["unit"] = "celsius";
    doc["min"] = TEMP_MIN;
    doc["max"] = TEMP_MAX;

    String response;
    serializeJson(doc, response);

    server.send(200, "application/json", response);
}

void handleAPISetBedTemperature() {
    if (server.hasArg("plain")) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        if (doc.containsKey("setpoint")) {
            float newTemp = doc["setpoint"].as<float>();

            // Clamp to valid range
            if (newTemp < TEMP_MIN) newTemp = TEMP_MIN;
            if (newTemp > TEMP_MAX) newTemp = TEMP_MAX;

            bedSetpoint = newTemp;

            Serial.printf("Bed temperature set via API: %.1f°C\n", bedSetpoint);

            // Update FreeSleep API
            setFreeSleepTemperature(bedTargetIP, "left", bedSetpoint);

            // Update display
            drawTemperatureUI();

            // Send response
            JsonDocument responseDoc;
            responseDoc["success"] = true;
            responseDoc["setpoint"] = bedSetpoint;

            String response;
            serializeJson(responseDoc, response);
            server.send(200, "application/json", response);
            return;
        }
    }

    server.send(400, "application/json", "{\"error\":\"Missing setpoint parameter\"}");
}

void handleAPIPillowTemperature() {
    JsonDocument doc;
    doc["setpoint"] = pillowSetpoint;
    doc["unit"] = "celsius";
    doc["min"] = TEMP_MIN;
    doc["max"] = TEMP_MAX;

    String response;
    serializeJson(doc, response);

    server.send(200, "application/json", response);
}

void handleAPISetPillowTemperature() {
    if (server.hasArg("plain")) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));

        if (error) {
            server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        if (doc.containsKey("setpoint")) {
            float newTemp = doc["setpoint"].as<float>();

            // Clamp to valid range
            if (newTemp < TEMP_MIN) newTemp = TEMP_MIN;
            if (newTemp > TEMP_MAX) newTemp = TEMP_MAX;

            pillowSetpoint = newTemp;

            Serial.printf("Pillow temperature set via API: %.1f°C\n", pillowSetpoint);

            // Update FreeSleep API
            setFreeSleepTemperature(pillowTargetIP, "right", pillowSetpoint);

            // Update display
            drawTemperatureUI();

            // Send response
            JsonDocument responseDoc;
            responseDoc["success"] = true;
            responseDoc["setpoint"] = pillowSetpoint;

            String response;
            serializeJson(responseDoc, response);
            server.send(200, "application/json", response);
            return;
        }
    }

    server.send(400, "application/json", "{\"error\":\"Missing setpoint parameter\"}");
}

void handleNotFound() {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
}

void handleEncoderInput() {
    // Route to appropriate handler based on current state
    if (inSettingsMenu) {
        if (currentSubMenu == SUBMENU_IP_EDITOR) {
            handleEncoderInIPEditor();
        } else if (currentSubMenu == SUBMENU_WIFI_SCAN) {
            handleEncoderInWiFiScanner();
        } else if (currentSubMenu == SUBMENU_WIFI_PASSWORD) {
            handleEncoderInPasswordEntry();
        } else {
            handleEncoderInSettings();
        }
        return;
    }

    // Default temperature control behavior
    long newPosition = M5Dial.Encoder.read();

    // Calculate difference from last update
    long diff = newPosition - lastEncoderPosition;

    // Accumulate encoder movement until we hit a detent threshold
    static long encoderAccumulator = 0;

    // Process any change
    if (diff != 0) {
        encoderAccumulator += diff;
        lastEncoderPosition = newPosition;
        recordActivity();

        // Each physical detent is 4 encoder counts
        // Change temperature when we accumulate enough for one step
        if (abs(encoderAccumulator) >= 4) {
            int steps = encoderAccumulator / 4;
            encoderAccumulator = encoderAccumulator % 4;  // Keep remainder

            // Step size: 0.5°C in Celsius mode, ~0.56°C (1°F) in Fahrenheit mode
            // Internal storage is always Celsius
            float stepSize = useFahrenheit ? (5.0f / 9.0f) : 0.5f;  // 1°F = 5/9°C ≈ 0.556°C
            float tempChange = steps * stepSize;

            // Adjust temperature for active setpoint
            float& activeSetpoint = getActiveSetpoint();
            float newTemp = activeSetpoint + tempChange;

            // Clamp to valid range
            if (newTemp < TEMP_MIN) newTemp = TEMP_MIN;
            if (newTemp > TEMP_MAX) newTemp = TEMP_MAX;

            // Round to step size for clean display
            if (useFahrenheit) {
                // Round to nearest 1°F (convert to F, round, convert back)
                float tempF = celsiusToFahrenheit(newTemp);
                tempF = round(tempF);
                newTemp = fahrenheitToCelsius(tempF);
            } else {
                // Round to nearest 0.5°C
                newTemp = round(newTemp * 2.0f) / 2.0f;
            }

            // Only update and redraw if temperature actually changed
            if (newTemp != activeSetpoint) {
                activeSetpoint = newTemp;
                if (useFahrenheit) {
                    Serial.printf("Encoder: %s Temperature: %.0f°F\n",
                                 pillowModeActive ? "Pillow" : "Bed", celsiusToFahrenheit(activeSetpoint));
                } else {
                    Serial.printf("Encoder: %s Temperature: %.1f°C\n",
                                 pillowModeActive ? "Pillow" : "Bed", activeSetpoint);
                }
                drawTemperatureUI();

                // Schedule debounced FreeSleep API update
                lastSetpointChangeTime = millis();
                pendingFreeSleepUpdate = true;
            }
        }
    }

    // Handle encoder button press (reset to default)
    if (M5Dial.BtnA.wasPressed()) {
        getActiveSetpoint() = TEMP_DEFAULT;
        Serial.printf("Reset %s to default: %.1f°C\n",
                     pillowModeActive ? "Pillow" : "Bed", TEMP_DEFAULT);
        recordActivity();
        drawTemperatureUI();

        // Schedule debounced FreeSleep API update
        lastSetpointChangeTime = millis();
        pendingFreeSleepUpdate = true;
    }
}

void handleTouchInput() {
    auto touch = M5Dial.Touch.getDetail();

    // Track touch on center area for duration-based actions
    if (touch.wasPressed()) {
        // Check if this is a center touch - don't call recordActivity yet
        // (we handle wake/dim toggle explicitly on release)
        bool isCenterTouch = !inSettingsMenu &&
                             abs(touch.x - centerX) < 60 &&
                             abs(touch.y - centerY) < 60;

        if (!isCenterTouch) {
            recordActivity();
        }

        // If in settings menu or submenus, handle touch to exit
        if (inSettingsMenu) {
            if (currentSubMenu != SUBMENU_NONE) {
                // Exit submenu, return to main settings menu
                currentSubMenu = SUBMENU_NONE;
                lastEncoderPosition = M5Dial.Encoder.read();  // Sync encoder position
                Serial.println("Exited submenu");
                drawSettingsMenu();
                return;
            } else {
                // Exit settings menu entirely
                inSettingsMenu = false;
                Serial.println("Exited settings menu");
                drawTemperatureUI();
                return;
            }
        }

        // Check if touch started on temperature display (center area)
        if (abs(touch.x - centerX) < 60 && abs(touch.y - centerY) < 60) {
            centerTouchStartTime = millis();
            centerTouchActive = true;
            return;  // Wait for release to determine action
        }

        // Check if touch is on time/IP area (bottom center) - open settings menu
        if (abs(touch.x - centerX) < 60 && touch.y > SCREEN_HEIGHT - 45 && touch.y < SCREEN_HEIGHT) {
            inSettingsMenu = true;
            lastEncoderPosition = M5Dial.Encoder.read();  // Sync encoder position
            Serial.println("Opened settings menu");
            drawSettingsMenu();
            return;
        }

        // Button positions (must match drawTemperatureUI)
        const int buttonY = SCREEN_HEIGHT - 55;  // Position above time/IP
        const int buttonSize = 40;  // Much larger for easier tapping
        const int leftButtonX = 50;
        const int rightButtonX = SCREEN_WIDTH - 50;

        // Check if touch is on pillow button (left)
        if (abs(touch.x - leftButtonX) < buttonSize/2 && abs(touch.y - buttonY) < buttonSize/2) {
            if (!pillowModeActive) {
                pillowModeActive = true;
                Serial.println("Switched to Pillow mode");
                drawTemperatureUI();
            }
            return;
        }

        // Check if touch is on bed button (right)
        if (abs(touch.x - rightButtonX) < buttonSize/2 && abs(touch.y - buttonY) < buttonSize/2) {
            if (pillowModeActive) {
                pillowModeActive = false;
                Serial.println("Switched to Bed mode");
                drawTemperatureUI();
            }
            return;
        }

        // Calculate touch position relative to center
        int dx = touch.x - centerX;
        int dy = touch.y - centerY;

        // Calculate distance from center
        float distance = sqrt(dx * dx + dy * dy);

        // If touch is on the arc area, adjust temperature based on angle
        if (distance > arcRadius - arcThickness - 10 && distance < arcRadius + 30) {
            // Calculate angle from touch position
            float angle = atan2(dy, dx) * 180.0 / PI;

            // Normalize angle to 0-360
            if (angle < 0) angle += 360;

            // Convert angle to temperature
            // Arc goes from 165° to 375° (3:30, wraps around)
            // Valid range is: 165° to 360° OR 0° to 15°
            float newTemp;
            if (angle >= 165 && angle <= 360) {
                // Map 165° to 375° across the arc
                newTemp = mapFloat(angle, 165, 375, TEMP_MIN, TEMP_MAX);
            } else if (angle >= 0 && angle <= 15) {
                // Handle wrap-around: 0° to 15° maps to 360° to 375°
                float normalizedAngle = angle + 360;
                newTemp = mapFloat(normalizedAngle, 165, 375, TEMP_MIN, TEMP_MAX);
            } else {
                // Not in valid arc range
                return;
            }

            // Clamp to valid range
            if (newTemp < TEMP_MIN) newTemp = TEMP_MIN;
            if (newTemp > TEMP_MAX) newTemp = TEMP_MAX;

            // Round to nearest 0.5°C increment
            getActiveSetpoint() = round(newTemp * 2.0) / 2.0;

            Serial.printf("Touch set %s temperature: %.1f°C\n",
                         pillowModeActive ? "Pillow" : "Bed", getActiveSetpoint());
            drawTemperatureUI();

            // Schedule debounced FreeSleep API update
            lastSetpointChangeTime = millis();
            pendingFreeSleepUpdate = true;
        }
        // Note: Center touch is now handled via duration detection above
    }

    // Handle touch release for center area (duration-based actions)
    if (touch.wasReleased() && centerTouchActive) {
        centerTouchActive = false;
        unsigned long now = millis();
        unsigned long touchDuration = now - centerTouchStartTime;

        // Debounce: ignore taps that come too quickly after the last one
        if (now - lastCenterTapTime < TAP_DEBOUNCE_MS) {
            Serial.println("Tap ignored (debounce)");
            return;
        }
        lastCenterTapTime = now;

        if (touchDuration < TAP_MIN_MS) {
            // Very short tap: toggle between wake and sleep brightness
            if (isDimmed) {
                // Wake up
                isDimmed = false;
                lastActivityTime = millis();
                Serial.println("Quick tap - waking up");
            } else {
                // Force dim
                isDimmed = true;
                lastActivityTime = 0;  // Set to long ago so it stays dimmed
                Serial.println("Quick tap - dimming");
            }
            updateBrightness();
        } else if (touchDuration < POWER_MAX_MS) {
            // 200-1000ms: Toggle power for active mode (bed or pillow)
            Serial.printf("Power toggle tap (%lums)\n", touchDuration);
            toggleActivePower();
        } else if (touchDuration < NIGHT_MODE_MAX_MS) {
            // 1000-3000ms: Toggle night mode override
            nightModeOverride = !nightModeOverride;
            Serial.printf("Night mode override: %s (%lums)\n", nightModeOverride ? "ON" : "OFF", touchDuration);
            drawTemperatureUI();
        } else {
            // > 3000ms: Open settings menu
            Serial.printf("Long hold - opening menu (%lums)\n", touchDuration);
            inSettingsMenu = true;
            currentMenuItem = MENU_WIFI_SETTINGS;
            currentSubMenu = SUBMENU_NONE;
            drawSettingsMenu();
        }
    }
}

void drawTemperatureUI() {
    // Determine if we're in night mode
    bool nightMode = isNightTime();

    // Select colors based on mode
    uint16_t bgColor = nightMode ? COLOR_NIGHT_BACKGROUND : COLOR_BACKGROUND;
    uint16_t arcBgColor = nightMode ? COLOR_NIGHT_ARC_BG : COLOR_ARC_BG;
    uint16_t textColor = nightMode ? COLOR_NIGHT_TEXT : COLOR_TEXT;
    uint16_t setpointColor = nightMode ? COLOR_NIGHT_SETPOINT : COLOR_SETPOINT;
    uint16_t minColor = nightMode ? COLOR_NIGHT_ARC_COLD : COLOR_ARC_COLD;
    uint16_t maxColor = nightMode ? COLOR_NIGHT_ARC_HOT : COLOR_ARC_HOT;

    // Clear sprite
    sprite.fillSprite(bgColor);

    // Arc range from 8:30 o'clock to 3:30 o'clock going clockwise
    // Screen coords: 0°=3 o'clock, 90°=6 o'clock, 180°=9 o'clock, 270°=12 o'clock
    // Fine-tuned to 165°
    const int startAngle = 165;   // 8:30 o'clock position (fine-tuned)
    const int endAngle = 375;     // 3:30 o'clock position (wraps around 360°)
    const int totalArcDegrees = endAngle - startAngle;  // 210 degrees

    // Draw tick markers first (before the arc)
    for (float temp = TEMP_MIN; temp <= TEMP_MAX; temp += 1.0) {
        float tempPercent = (temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
        int tickAngle = startAngle + (int)(tempPercent * totalArcDegrees);
        float tickRad = (tickAngle % 360) * PI / 180.0;

        // Determine tick size based on temperature value
        int tickLength;
        int tickThickness;
        if (((int)temp % 10) == 0) {
            // Large tick every 10°C
            tickLength = 18;
            tickThickness = 3;
        } else if (((int)temp % 5) == 0) {
            // Medium tick every 5°C
            tickLength = 12;
            tickThickness = 2;
        } else {
            // Small tick every 1°C
            tickLength = 8;
            tickThickness = 1;
        }

        // Draw tick mark extending outward from arc
        int x1 = centerX + cos(tickRad) * (arcRadius + 2);
        int y1 = centerY + sin(tickRad) * (arcRadius + 2);
        int x2 = centerX + cos(tickRad) * (arcRadius + 2 + tickLength);
        int y2 = centerY + sin(tickRad) * (arcRadius + 2 + tickLength);

        for (int i = 0; i < tickThickness; i++) {
            sprite.drawLine(x1, y1 + i, x2, y2 + i, arcBgColor);
        }
    }

    // Draw background arc (full range)
    for (int angle = startAngle; angle <= endAngle; angle += 2) {
        float rad = (angle % 360) * PI / 180.0;
        int x1 = centerX + cos(rad) * (arcRadius - arcThickness);
        int y1 = centerY + sin(rad) * (arcRadius - arcThickness);
        int x2 = centerX + cos(rad) * arcRadius;
        int y2 = centerY + sin(rad) * arcRadius;
        sprite.drawLine(x1, y1, x2, y2, arcBgColor);
    }

    // Draw colored arc showing current active temperature
    float activeTemp = getActiveSetpoint();
    float tempPercent = (activeTemp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    int currentAngle = startAngle + (int)(tempPercent * totalArcDegrees);

    for (int angle = startAngle; angle <= currentAngle; angle += 2) {
        // Calculate color gradient based on position in arc
        float arcPercent = (float)(angle - startAngle) / (float)totalArcDegrees;
        uint16_t color;
        if (nightMode) {
            // Red gradient for night mode
            color = getTemperatureColorNight(TEMP_MIN + arcPercent * (TEMP_MAX - TEMP_MIN));
        } else {
            // Normal color gradient for day mode
            color = getTemperatureColor(TEMP_MIN + arcPercent * (TEMP_MAX - TEMP_MIN));
        }

        float rad = (angle % 360) * PI / 180.0;
        int x1 = centerX + cos(rad) * (arcRadius - arcThickness);
        int y1 = centerY + sin(rad) * (arcRadius - arcThickness);
        int x2 = centerX + cos(rad) * arcRadius;
        int y2 = centerY + sin(rad) * arcRadius;
        sprite.drawLine(x1, y1, x2, y2, color);
    }

    // Draw bed setpoint indicator (outer marker)
    float bedTempPercent = (bedSetpoint - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    int bedAngle = startAngle + (int)(bedTempPercent * totalArcDegrees);
    float bedRad = (bedAngle % 360) * PI / 180.0;
    int bedIndicatorX = centerX + cos(bedRad) * (arcRadius + 8);
    int bedIndicatorY = centerY + sin(bedRad) * (arcRadius + 8);
    sprite.fillCircle(bedIndicatorX, bedIndicatorY, 5, pillowModeActive ? arcBgColor : setpointColor);
    if (pillowModeActive) {
        // Draw outline when inactive
        sprite.drawCircle(bedIndicatorX, bedIndicatorY, 5, setpointColor);
    }

    // Draw pillow setpoint indicator (inner marker)
    float pillowTempPercent = (pillowSetpoint - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    int pillowAngle = startAngle + (int)(pillowTempPercent * totalArcDegrees);
    float pillowRad = (pillowAngle % 360) * PI / 180.0;
    int pillowIndicatorX = centerX + cos(pillowRad) * (arcRadius - arcThickness - 8);
    int pillowIndicatorY = centerY + sin(pillowRad) * (arcRadius - arcThickness - 8);
    sprite.fillCircle(pillowIndicatorX, pillowIndicatorY, 5, pillowModeActive ? setpointColor : arcBgColor);
    if (!pillowModeActive) {
        // Draw outline when inactive
        sprite.drawCircle(pillowIndicatorX, pillowIndicatorY, 5, setpointColor);
    }

    // Check if active mode is powered off
    bool activePowerOn = pillowModeActive ? pillowPowerOn : bedPowerOn;

    // Draw temperature value in center with large modern font
    sprite.setTextColor(activePowerOn ? textColor : arcBgColor);  // Dim if powered off
    sprite.setTextDatum(middle_center);

    // Use a large font size for modern look
    sprite.setFont(&fonts::FreeSansBold24pt7b);
    char tempStr[10];
    if (useFahrenheit) {
        float tempF = celsiusToFahrenheit(activeTemp);
        snprintf(tempStr, sizeof(tempStr), "%.0f", tempF);
    } else {
        snprintf(tempStr, sizeof(tempStr), "%.1f", activeTemp);
    }
    sprite.drawString(tempStr, centerX, centerY - 10);

    // Draw degree symbol manually as a small circle
    sprite.fillCircle(centerX + 10, centerY + 25, 3, activePowerOn ? textColor : arcBgColor);
    sprite.fillCircle(centerX + 10, centerY + 25, 2, bgColor);

    // Temperature unit symbol in smaller font
    sprite.setFont(&fonts::FreeSans12pt7b);
    sprite.drawString(useFahrenheit ? "F" : "C", centerX + 25, centerY + 35);

    // Draw "OFF" indicator if power is off
    if (!activePowerOn) {
        sprite.setFont(&fonts::FreeSansBold12pt7b);
        sprite.setTextColor(nightMode ? COLOR_NIGHT_ARC_HOT : COLOR_ARC_HOT);  // Red/bright for visibility
        sprite.drawString("OFF", centerX, centerY + 55);
    }

    // Draw min/max labels at the arc endpoints
    sprite.setFont(&fonts::FreeSans9pt7b);

    // Min label at start angle (8 o'clock position)
    float minRad = (startAngle % 360) * PI / 180.0;
    int minX = centerX + cos(minRad) * (arcRadius + 35);
    int minY = centerY + sin(minRad) * (arcRadius + 35);
    sprite.setTextColor(minColor);
    int minDisplay = useFahrenheit ? (int)celsiusToFahrenheit(TEMP_MIN) : (int)TEMP_MIN;
    sprite.drawString(String(minDisplay).c_str(), minX, minY);

    // Max label at end angle (4 o'clock position)
    float maxRad = (endAngle % 360) * PI / 180.0;
    int maxX = centerX + cos(maxRad) * (arcRadius + 35);
    int maxY = centerY + sin(maxRad) * (arcRadius + 35);
    sprite.setTextColor(maxColor);
    int maxDisplay = useFahrenheit ? (int)celsiusToFahrenheit(TEMP_MAX) : (int)TEMP_MAX;
    sprite.drawString(String(maxDisplay).c_str(), maxX, maxY);

    // Draw time and IP address at the bottom
    sprite.setFont(&fonts::Font0);  // Small built-in font
    sprite.setTextColor(textColor);
    sprite.setTextDatum(middle_center);

    // Get current time
    if (timeInitialized) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            char timeStr[10];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            sprite.drawString(timeStr, centerX, SCREEN_HEIGHT - 30);
        }
    }

    // Draw IP address if connected
    if (wifiConnected) {
        String ipStr = WiFi.localIP().toString();
        sprite.drawString(ipStr.c_str(), centerX, SCREEN_HEIGHT - 15);
    } else {
        sprite.drawString("No WiFi", centerX, SCREEN_HEIGHT - 15);
    }

    // Draw pillow button on the left
    const int buttonY = SCREEN_HEIGHT - 55;  // Position above time/IP
    const int buttonSize = 40;  // Much larger for easier tapping
    const int leftButtonX = 50;
    const int rightButtonX = SCREEN_WIDTH - 50;

    // Determine button colors based on active state
    uint16_t pillowBgColor = pillowModeActive ? setpointColor : arcBgColor;
    uint16_t pillowIconColor = pillowModeActive ? bgColor : textColor;
    uint16_t bedBgColor = pillowModeActive ? arcBgColor : setpointColor;
    uint16_t bedIconColor = pillowModeActive ? textColor : bgColor;

    // Pillow button (left)
    sprite.fillRoundRect(leftButtonX - buttonSize/2, buttonY - buttonSize/2, buttonSize, buttonSize, 6, pillowBgColor);
    // Draw pillow icon (fluffy pillow shape with pinched ends)
    // Main pillow body - puffy center
    sprite.fillRoundRect(leftButtonX - 10, buttonY - 6, 20, 12, 5, pillowIconColor);
    // Pinched left end
    sprite.fillRoundRect(leftButtonX - 14, buttonY - 3, 6, 6, 2, pillowIconColor);
    // Pinched right end
    sprite.fillRoundRect(leftButtonX + 8, buttonY - 3, 6, 6, 2, pillowIconColor);

    // Bed button (right)
    sprite.fillRoundRect(rightButtonX - buttonSize/2, buttonY - buttonSize/2, buttonSize, buttonSize, 6, bedBgColor);
    // Draw bed icon (larger, simple bed shape)
    sprite.fillRect(rightButtonX - 12, buttonY, 24, 5, bedIconColor);  // mattress
    sprite.fillRect(rightButtonX - 12, buttonY + 5, 3, 4, bedIconColor);  // left leg
    sprite.fillRect(rightButtonX + 9, buttonY + 5, 3, 4, bedIconColor);  // right leg
    sprite.fillCircle(rightButtonX - 8, buttonY - 4, 3, bedIconColor);  // pillow/head
    sprite.fillCircle(rightButtonX + 6, buttonY - 4, 3, bedIconColor);  // pillow/head

    // Push sprite to display (eliminates flicker)
    sprite.pushSprite(0, 0);
}

void drawSettingsMenu() {
    // Determine if we're in night mode
    bool nightMode = isNightTime();

    // Select colors based on mode
    uint16_t bgColor = nightMode ? COLOR_NIGHT_BACKGROUND : COLOR_BACKGROUND;
    uint16_t textColor = nightMode ? COLOR_NIGHT_TEXT : COLOR_TEXT;
    uint16_t accentColor = nightMode ? COLOR_NIGHT_SETPOINT : COLOR_SETPOINT;
    uint16_t dimTextColor = nightMode ? 0x4000 : 0x4208;  // Dimmed text for non-active items

    // Clear sprite
    sprite.fillSprite(bgColor);

    // Draw title
    sprite.setTextColor(accentColor);
    sprite.setTextDatum(middle_center);
    sprite.setFont(&fonts::FreeSans12pt7b);
    sprite.drawString("Settings", centerX, 25);

    // Circular carousel display
    // Active item in center (larger, bold), others scroll above and below
    const int centerY_menu = SCREEN_HEIGHT / 2;
    const int itemSpacing = 40;

    // Draw menu items in carousel style
    for (int i = -2; i <= 2; i++) {
        int itemIndex = ((int)currentMenuItem + i + MENU_COUNT) % MENU_COUNT;
        int yPos = centerY_menu + (i * itemSpacing);

        // Skip items that are off-screen
        if (yPos < 50 || yPos > SCREEN_HEIGHT - 30) continue;

        MenuItem item = (MenuItem)itemIndex;
        String itemName = getMenuItemName(item);

        // Active item (i == 0) is centered, larger, and bold
        if (i == 0) {
            sprite.setFont(&fonts::FreeSansBold12pt7b);
            sprite.setTextColor(accentColor);
            sprite.setTextDatum(middle_center);
            sprite.drawString(itemName.c_str(), centerX, yPos);

            // Draw current value below for active item
            sprite.setTextColor(textColor);
            String value = "";
            switch (item) {
                case MENU_WIFI_SETTINGS:
                    value = wifiConnected ? WiFi.localIP().toString() : "Not connected";
                    break;
                case MENU_BED_IP:
                    value = bedTargetIP.toString();
                    break;
                case MENU_PILLOW_IP:
                    value = pillowTargetIP.toString();
                    break;
                case MENU_BED_SIDE:
                    value = bedSideRight ? "Right" : "Left";
                    break;
                case MENU_TEMP_UNIT:
                    value = useFahrenheit ? "Fahrenheit" : "Celsius";
                    break;
                case MENU_NIGHT_MODE:
                    value = nightModeOverride ? "Override ON" : "Auto";
                    break;
                case MENU_TEMPERATURE_MODE:
                    value = pillowModeActive ? "Pillow" : "Bed";
                    break;
                default:
                    break;
            }
            sprite.setFont(&fonts::Font0);
            sprite.drawString(value.c_str(), centerX, yPos + 18);

            // Draw selection indicators (arrows)
            sprite.setFont(&fonts::FreeSans9pt7b);
            sprite.setTextColor(accentColor);
            sprite.drawString(">", centerX - 100, yPos);
            sprite.drawString("<", centerX + 100, yPos);
        } else {
            // Non-active items are smaller and dimmed
            sprite.setFont(&fonts::FreeSans9pt7b);
            sprite.setTextColor(dimTextColor);
            sprite.setTextDatum(middle_center);

            // Scale text based on distance from center
            int alpha = 255 - abs(i) * 80;
            if (alpha < 80) alpha = 80;

            sprite.drawString(itemName.c_str(), centerX, yPos);
        }
    }

    // Draw instructions at bottom
    sprite.setTextDatum(middle_center);
    sprite.setFont(&fonts::Font0);
    sprite.setTextColor(textColor);
    sprite.drawString("Turn to navigate | Click to select | Tap to exit", centerX, SCREEN_HEIGHT - 10);

    // Push sprite to display
    sprite.pushSprite(0, 0);
}

void updateClockDisplay() {
    // Determine if we're in night mode
    bool nightMode = isNightTime();
    uint16_t bgColor = nightMode ? COLOR_NIGHT_BACKGROUND : COLOR_BACKGROUND;
    uint16_t textColor = nightMode ? COLOR_NIGHT_TEXT : COLOR_TEXT;

    // Create a small sprite just for the time area (centered, about 80 pixels wide, 15 pixels tall to cover ghost text)
    LGFX_Sprite timeSprite(&M5Dial.Display);
    const int timeWidth = 80;
    const int timeHeight = 15;
    const int timeX = centerX - timeWidth / 2;
    const int timeY = SCREEN_HEIGHT - 30 - (timeHeight / 2);  // Center around Y position used in drawTemperatureUI

    timeSprite.createSprite(timeWidth, timeHeight);
    timeSprite.fillSprite(bgColor);

    // Draw time to sprite
    if (timeInitialized) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            timeSprite.setFont(&fonts::Font0);
            timeSprite.setTextColor(textColor);
            timeSprite.setTextDatum(middle_center);

            char timeStr[10];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                     timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
            // Draw at vertical center of sprite
            timeSprite.drawString(timeStr, timeWidth / 2, timeHeight / 2);
        }
    }

    // Push only the time sprite to the specific location
    timeSprite.pushSprite(timeX, timeY);
    timeSprite.deleteSprite();
}

uint16_t getTemperatureColor(float temp) {
    // Map temperature to color gradient: blue -> cyan -> green -> yellow -> orange -> red
    float percent = (temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);

    uint8_t r, g, b;

    if (percent < 0.25) {
        // Blue to Cyan
        float t = percent / 0.25;
        r = 0;
        g = (uint8_t)(255 * t);
        b = 255;
    } else if (percent < 0.5) {
        // Cyan to Green
        float t = (percent - 0.25) / 0.25;
        r = 0;
        g = 255;
        b = (uint8_t)(255 * (1 - t));
    } else if (percent < 0.75) {
        // Green to Yellow/Orange
        float t = (percent - 0.5) / 0.25;
        r = (uint8_t)(255 * t);
        g = 255;
        b = 0;
    } else {
        // Orange to Red
        float t = (percent - 0.75) / 0.25;
        r = 255;
        g = (uint8_t)(255 * (1 - t));
        b = 0;
    }

    // Convert to RGB565
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t getTemperatureColorNight(float temp) {
    // Map temperature to red gradient: dark red -> medium red -> bright red
    float percent = (temp - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);

    uint8_t r, g, b;

    // Gradient from dark red (64, 0, 0) to bright red (255, 0, 0)
    r = (uint8_t)(64 + 191 * percent);
    g = 0;
    b = 0;

    // Convert to RGB565
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void setupNTP() {
    if (!wifiConnected) {
        Serial.println("Cannot setup NTP: WiFi not connected");
        return;
    }

    Serial.println("Syncing time with NTP server...");
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    // Wait for time to be set
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (attempts < 10) {
        timeInitialized = true;
        Serial.println("\nTime synchronized!");
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

        // Set RTC from system time
        M5Dial.Rtc.setDateTime(&timeinfo);
    } else {
        Serial.println("\nFailed to sync time");
    }
}

bool isNightTime() {
    // Check manual override first
    if (nightModeOverride) {
        return true;
    }

    if (!timeInitialized) {
        return false;  // Default to day mode if time not set
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return false;
    }

    int hour = timeinfo.tm_hour;

    // Night time is from NIGHT_START_HOUR (22:00) to NIGHT_END_HOUR (07:00)
    if (NIGHT_START_HOUR > NIGHT_END_HOUR) {
        // Wraps around midnight (e.g., 22:00 to 07:00)
        return (hour >= NIGHT_START_HOUR || hour < NIGHT_END_HOUR);
    } else {
        // Doesn't wrap (e.g., 01:00 to 06:00)
        return (hour >= NIGHT_START_HOUR && hour < NIGHT_END_HOUR);
    }
}

void recordActivity() {
    lastActivityTime = millis();

    // If screen was dimmed, wake it up immediately
    if (isDimmed) {
        isDimmed = false;
        updateBrightness();
    }
}

void updateBrightness() {
    unsigned long timeSinceActivity = millis() - lastActivityTime;
    uint8_t targetBrightness;

    // Check if we should dim due to inactivity
    if (timeSinceActivity > DIM_TIMEOUT_MS) {
        targetBrightness = BRIGHTNESS_DIM;
        if (!isDimmed) {
            isDimmed = true;
            Serial.println("Dimming display due to inactivity");
        }
    } else {
        // Active - check time of day for brightness
        if (isNightTime()) {
            targetBrightness = BRIGHTNESS_NIGHT;
        } else {
            targetBrightness = BRIGHTNESS_DAY;
        }
        isDimmed = false;
    }

    // Set brightness
    M5Dial.Display.setBrightness(targetBrightness);
}

float& getActiveSetpoint() {
    return pillowModeActive ? pillowSetpoint : bedSetpoint;
}

float& getInactiveSetpoint() {
    return pillowModeActive ? bedSetpoint : pillowSetpoint;
}

String getMenuItemName(MenuItem item) {
    switch (item) {
        case MENU_WIFI_SETTINGS: return "WiFi Settings";
        case MENU_BED_IP: return "Bed Controller IP";
        case MENU_PILLOW_IP: return "Pillow Controller IP";
        case MENU_BED_SIDE: return "Bed Side";
        case MENU_TEMP_UNIT: return "Temperature Unit";
        case MENU_NIGHT_MODE: return "Night Mode";
        case MENU_TEMPERATURE_MODE: return "Temperature Mode";
        default: return "Unknown";
    }
}

void handleEncoderInSettings() {
    long newPosition = M5Dial.Encoder.read();
    long diff = newPosition - lastEncoderPosition;

    // Accumulate encoder movement until we hit a detent threshold
    static long encoderAccumulator = 0;

    if (diff != 0) {
        encoderAccumulator += diff;
        lastEncoderPosition = newPosition;
        recordActivity();

        // Navigate menu when we accumulate enough for one step (4 counts per detent)
        if (abs(encoderAccumulator) >= 4) {
            int steps = encoderAccumulator / 4;
            encoderAccumulator = encoderAccumulator % 4;  // Keep remainder

            currentMenuItem = (MenuItem)(((int)currentMenuItem + steps + MENU_COUNT) % MENU_COUNT);
            drawSettingsMenu();
        }
    }

    // Handle encoder button press - select menu item
    if (M5Dial.BtnA.wasPressed()) {
        recordActivity();
        Serial.printf("Selected: %s\n", getMenuItemName(currentMenuItem).c_str());

        switch (currentMenuItem) {
            case MENU_WIFI_SETTINGS:
                startWiFiScanner();
                break;
            case MENU_BED_IP:
                startIPEditor(true);  // Edit bed IP
                break;
            case MENU_PILLOW_IP:
                startIPEditor(false);  // Edit pillow IP
                break;
            case MENU_BED_SIDE:
                // Toggle bed side (left/right)
                bedSideRight = !bedSideRight;
                preferences.putBool("bedSideRight", bedSideRight);
                Serial.printf("Bed side: %s (saved)\n", bedSideRight ? "Right" : "Left");
                drawSettingsMenu();
                break;
            case MENU_TEMP_UNIT:
                // Toggle temperature unit (Celsius/Fahrenheit)
                useFahrenheit = !useFahrenheit;
                preferences.putBool("useFahrenheit", useFahrenheit);
                Serial.printf("Temp unit: %s (saved)\n", useFahrenheit ? "Fahrenheit" : "Celsius");
                drawSettingsMenu();
                break;
            case MENU_NIGHT_MODE:
                // Toggle night mode override
                nightModeOverride = !nightModeOverride;
                Serial.printf("Night mode override: %s\n", nightModeOverride ? "ON" : "OFF");
                drawSettingsMenu();
                break;
            case MENU_TEMPERATURE_MODE:
                // Toggle temperature mode
                pillowModeActive = !pillowModeActive;
                Serial.printf("Temperature mode: %s\n", pillowModeActive ? "Pillow" : "Bed");
                drawSettingsMenu();
                break;
            default:
                break;
        }
    }
}

void startIPEditor(bool isBedIP) {
    editingBedIP = isBedIP;
    currentSubMenu = SUBMENU_IP_EDITOR;
    ipEditorOctet = 0;
    ipEditorDigit = 0;
    lastEncoderPosition = M5Dial.Encoder.read();  // Sync encoder position

    // Copy current IP to temp array
    IPAddress& targetIP = isBedIP ? bedTargetIP : pillowTargetIP;
    for (int i = 0; i < 4; i++) {
        tempIPOctets[i] = targetIP[i];
    }

    Serial.printf("Editing %s IP: %d.%d.%d.%d\n", isBedIP ? "Bed" : "Pillow",
                  tempIPOctets[0], tempIPOctets[1], tempIPOctets[2], tempIPOctets[3]);

    drawIPEditor();
}

void drawIPEditor() {
    bool nightMode = isNightTime();
    uint16_t bgColor = nightMode ? COLOR_NIGHT_BACKGROUND : COLOR_BACKGROUND;
    uint16_t textColor = nightMode ? COLOR_NIGHT_TEXT : COLOR_TEXT;
    uint16_t accentColor = nightMode ? COLOR_NIGHT_SETPOINT : COLOR_SETPOINT;

    sprite.fillSprite(bgColor);

    // Draw title
    sprite.setTextColor(accentColor);
    sprite.setTextDatum(middle_center);
    sprite.setFont(&fonts::FreeSans12pt7b);
    sprite.drawString(editingBedIP ? "Bed IP Address" : "Pillow IP Address", centerX, centerY - 40);

    // Draw IP address with current octet highlighted
    sprite.setFont(&fonts::FreeSansBold9pt7b);
    sprite.setTextDatum(middle_center);

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%03d.%03d.%03d.%03d",
             tempIPOctets[0], tempIPOctets[1], tempIPOctets[2], tempIPOctets[3]);

    // Calculate positions for each octet (centered display)
    int y = centerY;
    int spacing = 38;
    int startX = centerX - (spacing * 1.5);

    for (int i = 0; i < 4; i++) {
        int x = startX + (i * spacing);

        // Highlight active octet
        if (i == ipEditorOctet) {
            sprite.setTextColor(accentColor);
            // Draw box around active octet
            sprite.drawRect(x - 16, y - 12, 32, 24, accentColor);
        } else {
            sprite.setTextColor(textColor);
        }

        char octetStr[4];
        snprintf(octetStr, sizeof(octetStr), "%03d", tempIPOctets[i]);
        sprite.drawString(octetStr, x, y);

        // Draw dot separator (except after last octet)
        if (i < 3) {
            sprite.setTextColor(textColor);
            sprite.drawString(".", x + 19, y);
        }
    }

    // Draw instructions
    sprite.setFont(&fonts::Font0);
    sprite.setTextColor(textColor);
    sprite.setTextDatum(middle_center);
    sprite.drawString("Turn to change | Click for next", centerX, centerY + 35);
    sprite.drawString("Tap to save and exit", centerX, centerY + 50);

    sprite.pushSprite(0, 0);
}

void handleEncoderInIPEditor() {
    long newPosition = M5Dial.Encoder.read();
    long diff = newPosition - lastEncoderPosition;

    // Accumulate encoder movement until we hit a detent threshold
    static long encoderAccumulator = 0;

    if (diff != 0) {
        encoderAccumulator += diff;
        lastEncoderPosition = newPosition;
        recordActivity();

        // Adjust current octet value when we accumulate enough (4 counts per detent)
        if (abs(encoderAccumulator) >= 4) {
            int steps = encoderAccumulator / 4;
            encoderAccumulator = encoderAccumulator % 4;  // Keep remainder

            int newValue = (int)tempIPOctets[ipEditorOctet] + steps;
            // Wrap around: 0->255 and 255->0
            if (newValue < 0) newValue = 256 + (newValue % 256);
            if (newValue > 255) newValue = newValue % 256;
            tempIPOctets[ipEditorOctet] = newValue;

            drawIPEditor();
        }
    }

    // Handle encoder button press - move to next octet/digit
    if (M5Dial.BtnA.wasPressed()) {
        recordActivity();
        ipEditorOctet++;

        if (ipEditorOctet >= 4) {
            // Finished editing all octets - save and exit
            IPAddress& targetIP = editingBedIP ? bedTargetIP : pillowTargetIP;
            targetIP = IPAddress(tempIPOctets[0], tempIPOctets[1], tempIPOctets[2], tempIPOctets[3]);

            // Save to NVS
            if (editingBedIP) {
                preferences.putUChar("bedIP0", tempIPOctets[0]);
                preferences.putUChar("bedIP1", tempIPOctets[1]);
                preferences.putUChar("bedIP2", tempIPOctets[2]);
                preferences.putUChar("bedIP3", tempIPOctets[3]);
            } else {
                preferences.putUChar("pillowIP0", tempIPOctets[0]);
                preferences.putUChar("pillowIP1", tempIPOctets[1]);
                preferences.putUChar("pillowIP2", tempIPOctets[2]);
                preferences.putUChar("pillowIP3", tempIPOctets[3]);
            }

            Serial.printf("Saved %s IP: %s (to NVS)\n", editingBedIP ? "Bed" : "Pillow", targetIP.toString().c_str());

            // Return to settings menu
            currentSubMenu = SUBMENU_NONE;
            lastEncoderPosition = M5Dial.Encoder.read();  // Sync encoder position
            drawSettingsMenu();
        } else {
            Serial.printf("Editing octet %d\n", ipEditorOctet);
            drawIPEditor();
        }
    }
}

void startWiFiScanner() {
    currentSubMenu = SUBMENU_WIFI_SCAN;
    scannedSSIDCount = 0;
    selectedSSIDIndex = 0;
    lastEncoderPosition = M5Dial.Encoder.read();  // Sync encoder position

    Serial.println("Scanning for WiFi networks...");

    // Perform WiFi scan
    int n = WiFi.scanNetworks();
    scannedSSIDCount = (n > 20) ? 20 : n;  // Limit to 20 networks

    for (int i = 0; i < scannedSSIDCount; i++) {
        scannedSSIDs[i] = WiFi.SSID(i);
        Serial.printf("%d: %s (%d dBm)\n", i, scannedSSIDs[i].c_str(), WiFi.RSSI(i));
    }

    if (scannedSSIDCount == 0) {
        Serial.println("No networks found");
    }

    drawWiFiScanner();
}

void drawWiFiScanner() {
    bool nightMode = isNightTime();
    uint16_t bgColor = nightMode ? COLOR_NIGHT_BACKGROUND : COLOR_BACKGROUND;
    uint16_t textColor = nightMode ? COLOR_NIGHT_TEXT : COLOR_TEXT;
    uint16_t accentColor = nightMode ? COLOR_NIGHT_SETPOINT : COLOR_SETPOINT;
    uint16_t dimTextColor = nightMode ? 0x4000 : 0x4208;

    sprite.fillSprite(bgColor);

    // Draw title
    sprite.setTextColor(accentColor);
    sprite.setTextDatum(middle_center);
    sprite.setFont(&fonts::FreeSans12pt7b);
    sprite.drawString("WiFi Networks", centerX, 25);

    if (scannedSSIDCount == 0) {
        sprite.setFont(&fonts::FreeSans9pt7b);
        sprite.setTextColor(textColor);
        sprite.drawString("No networks found", centerX, centerY);
        sprite.setFont(&fonts::Font0);
        sprite.drawString("Tap to go back", centerX, SCREEN_HEIGHT - 15);
    } else {
        // Carousel display of WiFi networks
        const int centerY_menu = SCREEN_HEIGHT / 2;
        const int itemSpacing = 35;

        for (int i = -2; i <= 2; i++) {
            int networkIndex = selectedSSIDIndex + i;
            if (networkIndex < 0 || networkIndex >= scannedSSIDCount) continue;

            int yPos = centerY_menu + (i * itemSpacing);
            if (yPos < 50 || yPos > SCREEN_HEIGHT - 30) continue;

            if (i == 0) {
                // Active network - centered, larger, bold
                sprite.setFont(&fonts::FreeSansBold12pt7b);
                sprite.setTextColor(accentColor);
                sprite.setTextDatum(middle_center);
                sprite.drawString(scannedSSIDs[networkIndex].c_str(), centerX, yPos);

                // Draw selection arrows
                sprite.setFont(&fonts::FreeSans9pt7b);
                sprite.drawString(">", centerX - 100, yPos);
                sprite.drawString("<", centerX + 100, yPos);
            } else {
                // Non-active networks
                sprite.setFont(&fonts::FreeSans9pt7b);
                sprite.setTextColor(dimTextColor);
                sprite.setTextDatum(middle_center);
                sprite.drawString(scannedSSIDs[networkIndex].c_str(), centerX, yPos);
            }
        }

        // Instructions
        sprite.setFont(&fonts::Font0);
        sprite.setTextColor(textColor);
        sprite.setTextDatum(middle_center);
        sprite.drawString("Turn to select | Click to connect | Tap to cancel", centerX, SCREEN_HEIGHT - 10);
    }

    sprite.pushSprite(0, 0);
}

void handleEncoderInWiFiScanner() {
    long newPosition = M5Dial.Encoder.read();
    long diff = newPosition - lastEncoderPosition;

    // Accumulate encoder movement until we hit a detent threshold
    static long encoderAccumulator = 0;

    if (diff != 0) {
        encoderAccumulator += diff;
        lastEncoderPosition = newPosition;
        recordActivity();

        // Navigate when we accumulate enough (4 counts per detent)
        if (abs(encoderAccumulator) >= 4 && scannedSSIDCount > 0) {
            int steps = encoderAccumulator / 4;
            encoderAccumulator = encoderAccumulator % 4;  // Keep remainder

            selectedSSIDIndex += steps;
            if (selectedSSIDIndex < 0) selectedSSIDIndex = 0;
            if (selectedSSIDIndex >= scannedSSIDCount) selectedSSIDIndex = scannedSSIDCount - 1;

            drawWiFiScanner();
        }
    }

    // Handle encoder button press - select network and enter password
    if (M5Dial.BtnA.wasPressed()) {
        recordActivity();
        if (scannedSSIDCount > 0) {
            Serial.printf("Connecting to: %s\n", scannedSSIDs[selectedSSIDIndex].c_str());
            startPasswordEntry();
        }
    }
}

void startPasswordEntry() {
    currentSubMenu = SUBMENU_WIFI_PASSWORD;
    wifiPasswordInput = "";
    passwordCharIndex = 0;
    lastEncoderPosition = M5Dial.Encoder.read();  // Sync encoder position

    Serial.println("Entering WiFi password");
    drawPasswordEntry();
}

void drawPasswordEntry() {
    bool nightMode = isNightTime();
    uint16_t bgColor = nightMode ? COLOR_NIGHT_BACKGROUND : COLOR_BACKGROUND;
    uint16_t textColor = nightMode ? COLOR_NIGHT_TEXT : COLOR_TEXT;
    uint16_t accentColor = nightMode ? COLOR_NIGHT_SETPOINT : COLOR_SETPOINT;

    sprite.fillSprite(bgColor);

    // Draw title
    sprite.setTextColor(accentColor);
    sprite.setTextDatum(middle_center);
    sprite.setFont(&fonts::FreeSans12pt7b);
    sprite.drawString("WiFi Password", centerX, 25);

    // Draw network name
    sprite.setFont(&fonts::FreeSans9pt7b);
    sprite.setTextColor(textColor);
    sprite.drawString(scannedSSIDs[selectedSSIDIndex].c_str(), centerX, 55);

    // Draw current password (masked)
    sprite.setFont(&fonts::FreeSansBold12pt7b);
    sprite.setTextColor(textColor);
    sprite.setTextDatum(middle_center);
    String maskedPassword = "";
    for (int i = 0; i < wifiPasswordInput.length(); i++) {
        maskedPassword += "*";
    }
    sprite.drawString(maskedPassword.c_str(), centerX, centerY - 20);

    // Draw current character being edited in a carousel
    const int charSpacing = 30;
    const int centerY_char = centerY + 40;

    // Show current character and neighbors
    for (int i = -2; i <= 2; i++) {
        int charIdx = passwordCharIndex + i;
        int alphaLen = strlen(alphaNumeric);
        charIdx = (charIdx + alphaLen) % alphaLen;

        int yPos = centerY_char + (i * charSpacing);

        if (i == 0) {
            // Active character - centered, bold
            sprite.setFont(&fonts::FreeSansBold18pt7b);
            sprite.setTextColor(accentColor);
            char charStr[2] = {alphaNumeric[charIdx], '\0'};
            sprite.drawString(charStr, centerX, yPos);

            // Draw selection box
            sprite.drawRect(centerX - 15, yPos - 18, 30, 36, accentColor);
        } else {
            // Non-active characters
            sprite.setFont(&fonts::FreeSans12pt7b);
            sprite.setTextColor(textColor);
            char charStr[2] = {alphaNumeric[charIdx], '\0'};
            sprite.drawString(charStr, centerX, yPos);
        }
    }

    // Instructions
    sprite.setFont(&fonts::Font0);
    sprite.setTextColor(textColor);
    sprite.setTextDatum(middle_center);
    sprite.drawString("Turn to select char | Click to add | Long press to connect", centerX, SCREEN_HEIGHT - 20);
    sprite.drawString("Tap screen to cancel", centerX, SCREEN_HEIGHT - 10);

    sprite.pushSprite(0, 0);
}

void handleEncoderInPasswordEntry() {
    long newPosition = M5Dial.Encoder.read();
    long diff = newPosition - lastEncoderPosition;

    // Accumulate encoder movement until we hit a detent threshold
    static long encoderAccumulator = 0;

    if (diff != 0) {
        encoderAccumulator += diff;
        lastEncoderPosition = newPosition;
        recordActivity();

        // Navigate when we accumulate enough (4 counts per detent)
        if (abs(encoderAccumulator) >= 4) {
            int steps = encoderAccumulator / 4;
            encoderAccumulator = encoderAccumulator % 4;  // Keep remainder

            int alphaLen = strlen(alphaNumeric);
            passwordCharIndex = (passwordCharIndex + steps + alphaLen) % alphaLen;
            drawPasswordEntry();
        }
    }

    // Handle encoder button press - add character to password
    if (M5Dial.BtnA.wasPressed()) {
        recordActivity();
        wifiPasswordInput += alphaNumeric[passwordCharIndex];
        Serial.printf("Password: %s (length: %d)\n", wifiPasswordInput.c_str(), wifiPasswordInput.length());
        drawPasswordEntry();
    }

    // Handle long press - submit password and connect
    if (M5Dial.BtnA.pressedFor(1000)) {
        recordActivity();
        Serial.printf("Connecting to %s with password: %s\n",
                      scannedSSIDs[selectedSSIDIndex].c_str(), wifiPasswordInput.c_str());

        // Attempt to connect
        WiFi.begin(scannedSSIDs[selectedSSIDIndex].c_str(), wifiPasswordInput.c_str());

        // Get colors for status messages
        bool nightMode = isNightTime();
        uint16_t bgColor = nightMode ? COLOR_NIGHT_BACKGROUND : COLOR_BACKGROUND;
        uint16_t accentColor = nightMode ? COLOR_NIGHT_SETPOINT : COLOR_SETPOINT;

        // Show connecting message
        sprite.fillSprite(bgColor);
        sprite.setTextColor(accentColor);
        sprite.setTextDatum(middle_center);
        sprite.setFont(&fonts::FreeSans12pt7b);
        sprite.drawString("Connecting...", centerX, centerY);
        sprite.pushSprite(0, 0);

        // Wait for connection (with timeout)
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("WiFi connected successfully!");
            Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());

            // Save WiFi credentials to NVS
            savedWifiSSID = scannedSSIDs[selectedSSIDIndex];
            savedWifiPassword = wifiPasswordInput;
            preferences.putString("wifiSSID", savedWifiSSID);
            preferences.putString("wifiPass", savedWifiPassword);
            Serial.println("WiFi credentials saved to NVS");

            // Restart web server with new connection
            server.begin();

            // Show success
            sprite.fillSprite(bgColor);
            sprite.setTextColor(COLOR_SETPOINT);
            sprite.setFont(&fonts::FreeSans12pt7b);
            sprite.drawString("Connected!", centerX, centerY);
            sprite.pushSprite(0, 0);
            delay(2000);
        } else {
            Serial.println("WiFi connection failed");

            // Show error
            sprite.fillSprite(bgColor);
            sprite.setTextColor(0xF800);  // Red
            sprite.setFont(&fonts::FreeSans12pt7b);
            sprite.drawString("Connection Failed", centerX, centerY);
            sprite.pushSprite(0, 0);
            delay(2000);
        }

        // Return to settings menu
        currentSubMenu = SUBMENU_NONE;
        lastEncoderPosition = M5Dial.Encoder.read();  // Sync encoder position
        drawSettingsMenu();
    }
}

// ==================== FreeSleep API Functions ====================

float celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

float fahrenheitToCelsius(float fahrenheit) {
    return (fahrenheit - 32.0f) * 5.0f / 9.0f;
}

// Fetch current temperature setpoint and power state from FreeSleep API
// side should be "left" or "right"
bool fetchFreeSleepTemperature(IPAddress ip, const char* side, float& tempCelsius, bool& isOn) {
    if (!wifiConnected) return false;

    HTTPClient http;
    String url = "http://" + ip.toString() + ":3000/api/deviceStatus";

    http.begin(url);
    http.setTimeout(1000);  // 1 second timeout for local network

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            if (doc[side]["targetTemperatureF"].is<float>()) {
                float tempF = doc[side]["targetTemperatureF"].as<float>();
                tempCelsius = fahrenheitToCelsius(tempF);
                isOn = doc[side]["isOn"].as<bool>();
                Serial.printf("FreeSleep %s: %.1f°F = %.1f°C, power: %s\n",
                             side, tempF, tempCelsius, isOn ? "ON" : "OFF");
                http.end();
                return true;
            }
        }
    } else {
        Serial.printf("FreeSleep GET failed: %d\n", httpCode);
    }

    http.end();
    return false;
}

// Set temperature on FreeSleep API
// side should be "left" or "right"
bool setFreeSleepTemperature(IPAddress ip, const char* side, float tempCelsius) {
    if (!wifiConnected) return false;

    HTTPClient http;
    String url = "http://" + ip.toString() + ":3000/api/deviceStatus";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(1000);  // 1 second timeout for local network

    // Convert to Fahrenheit and round to integer (API requires integer)
    int tempF = (int)round(celsiusToFahrenheit(tempCelsius));

    // Clamp to FreeSleep's valid range (55-110°F)
    if (tempF < 55) tempF = 55;
    if (tempF > 110) tempF = 110;

    // Build JSON payload
    JsonDocument doc;
    doc[side]["targetTemperatureF"] = tempF;

    String payload;
    serializeJson(doc, payload);

    Serial.printf("FreeSleep POST to %s: %s\n", url.c_str(), payload.c_str());

    int httpCode = http.POST(payload);

    if (httpCode == HTTP_CODE_NO_CONTENT || httpCode == HTTP_CODE_OK) {
        Serial.printf("FreeSleep %s set to %d°F (%.1f°C)\n", side, tempF, tempCelsius);
        http.end();
        return true;
    } else {
        Serial.printf("FreeSleep POST failed: %d\n", httpCode);
    }

    http.end();
    return false;
}

// Set power state on FreeSleep API
// side should be "left" or "right"
bool setFreeSleepPower(IPAddress ip, const char* side, bool powerOn) {
    if (!wifiConnected) return false;

    HTTPClient http;
    String url = "http://" + ip.toString() + ":3000/api/deviceStatus";

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(1000);  // 1 second timeout for local network

    // Build JSON payload
    JsonDocument doc;
    doc[side]["isOn"] = powerOn;

    String payload;
    serializeJson(doc, payload);

    Serial.printf("FreeSleep power POST to %s: %s\n", url.c_str(), payload.c_str());

    int httpCode = http.POST(payload);

    if (httpCode == HTTP_CODE_NO_CONTENT || httpCode == HTTP_CODE_OK) {
        Serial.printf("FreeSleep %s power set to %s\n", side, powerOn ? "ON" : "OFF");
        http.end();
        return true;
    } else {
        Serial.printf("FreeSleep power POST failed: %d\n", httpCode);
    }

    http.end();
    return false;
}

// Toggle power for the currently active mode (bed or pillow)
void toggleActivePower() {
    const char* side = bedSideRight ? "right" : "left";

    if (pillowModeActive) {
        // Toggle pillow power
        pillowPowerOn = !pillowPowerOn;
        Serial.printf("Toggling pillow power to %s\n", pillowPowerOn ? "ON" : "OFF");
        setFreeSleepPower(pillowTargetIP, side, pillowPowerOn);
    } else {
        // Toggle bed power
        bedPowerOn = !bedPowerOn;
        Serial.printf("Toggling bed power to %s\n", bedPowerOn ? "ON" : "OFF");
        setFreeSleepPower(bedTargetIP, side, bedPowerOn);
    }

    drawTemperatureUI();
}

// Sync temperatures and power state from FreeSleep on startup
void syncTemperaturesFromFreeSleep() {
    Serial.println("Syncing temperatures from FreeSleep...");

    float temp;
    bool isOn;

    const char* side = bedSideRight ? "right" : "left";

    // Fetch bed temperature and power state
    if (fetchFreeSleepTemperature(bedTargetIP, side, temp, isOn)) {
        bedSetpoint = temp;
        bedPowerOn = isOn;
        Serial.printf("Bed synced: %.1f°C, power: %s\n", bedSetpoint, bedPowerOn ? "ON" : "OFF");
    }

    // Fetch pillow temperature and power state
    if (fetchFreeSleepTemperature(pillowTargetIP, side, temp, isOn)) {
        pillowSetpoint = temp;
        pillowPowerOn = isOn;
        Serial.printf("Pillow synced: %.1f°C, power: %s\n", pillowSetpoint, pillowPowerOn ? "ON" : "OFF");
    }
}

// Periodic sync of temperature and power state from FreeSleep
void syncFromFreeSleep() {
    float temp;
    bool isOn;
    bool needsRedraw = false;
    bool anySuccess = false;

    // Don't sync temperature if user recently changed it (prevents overwriting user input)
    bool allowTempSync = (millis() - lastSetpointChangeTime) > SYNC_COOLDOWN_AFTER_CHANGE_MS;

    const char* side = bedSideRight ? "right" : "left";

    // Fetch bed temperature and power state
    if (fetchFreeSleepTemperature(bedTargetIP, side, temp, isOn)) {
        anySuccess = true;
        if (bedPowerOn != isOn) {
            bedPowerOn = isOn;
            Serial.printf("Bed power state changed: %s\n", bedPowerOn ? "ON" : "OFF");
            needsRedraw = true;
        }
        if (allowTempSync && abs(bedSetpoint - temp) > 0.1f) {
            bedSetpoint = temp;
            Serial.printf("Bed temperature synced: %.1f°C\n", bedSetpoint);
            needsRedraw = true;
        }
    }

    // Fetch pillow temperature and power state
    if (fetchFreeSleepTemperature(pillowTargetIP, side, temp, isOn)) {
        anySuccess = true;
        if (pillowPowerOn != isOn) {
            pillowPowerOn = isOn;
            Serial.printf("Pillow power state changed: %s\n", pillowPowerOn ? "ON" : "OFF");
            needsRedraw = true;
        }
        if (allowTempSync && abs(pillowSetpoint - temp) > 0.1f) {
            pillowSetpoint = temp;
            Serial.printf("Pillow temperature synced: %.1f°C\n", pillowSetpoint);
            needsRedraw = true;
        }
    }

    // Handle backoff logic
    if (anySuccess) {
        // Reset on success
        if (consecutiveFailures > 0) {
            Serial.printf("FreeSleep sync recovered after %d failures\n", consecutiveFailures);
            consecutiveFailures = 0;
            currentSyncInterval = FREESLEEP_SYNC_INTERVAL_MS;
        }
    } else {
        // Exponential backoff on failure
        consecutiveFailures++;
        currentSyncInterval = min(currentSyncInterval * 2, MAX_SYNC_INTERVAL_MS);
        Serial.printf("FreeSleep sync failed (%d consecutive), backing off to %lums\n",
                     consecutiveFailures, currentSyncInterval);
    }

    // Only redraw if something actually changed
    if (needsRedraw) {
        drawTemperatureUI();
    }
}
