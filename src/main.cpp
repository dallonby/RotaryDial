#include <M5Dial.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "config.h"

// Global variables
float temperatureSetpoint = TEMP_DEFAULT;
float displayedTemperature = TEMP_DEFAULT;
bool wifiConnected = false;
long lastEncoderPosition = 0;
unsigned long lastTouchTime = 0;
unsigned long lastUIUpdate = 0;

// Web server
WebServer server(API_PORT);

// Display dimensions
const int centerX = SCREEN_WIDTH / 2;
const int centerY = SCREEN_HEIGHT / 2;
const int arcRadius = 100;
const int arcThickness = 15;

// Function prototypes
void setupWiFi();
void setupWebServer();
void drawTemperatureUI();
void drawArc(int startAngle, int endAngle, uint16_t color);
void handleEncoderInput();
void handleTouchInput();
void handleAPIRoot();
void handleAPITemperature();
void handleAPISetTemperature();
void handleNotFound();
uint16_t getTemperatureColor(float temp);
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);

void setup() {
    // Initialize M5Dial
    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);  // Enable encoder, disable RFID

    Serial.begin(115200);
    Serial.println("\n\nM5Stack Dial Temperature Controller");
    Serial.println("====================================");

    // Initialize display
    M5Dial.Display.setRotation(0);
    M5Dial.Display.fillScreen(COLOR_BACKGROUND);
    M5Dial.Display.setTextColor(COLOR_TEXT);
    M5Dial.Display.setTextDatum(middle_center);

    // Show startup message
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString("Connecting to WiFi...", centerX, centerY);

    // Connect to WiFi
    setupWiFi();

    // Setup web server
    setupWebServer();

    // Get initial encoder position
    lastEncoderPosition = M5Dial.Encoder.read();

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

    // Update UI periodically for smooth animation
    if (millis() - lastUIUpdate > 50) {
        // Smooth transition for displayed temperature
        if (abs(displayedTemperature - temperatureSetpoint) > 0.01) {
            displayedTemperature += (temperatureSetpoint - displayedTemperature) * 0.3;
            drawTemperatureUI();
        }
        lastUIUpdate = millis();
    }

    delay(10);
}

void setupWiFi() {
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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
    html += "<div class='temp'>" + String(temperatureSetpoint, 1) + "<span class='unit'>&deg;C</span></div>";
    html += "<p class='info'>Use the rotary dial to adjust temperature</p>";
    html += "<p class='info'>API: GET/POST /api/temperature</p>";
    html += "<script>setInterval(()=>location.reload(), 5000);</script>";
    html += "</body></html>";

    server.send(200, "text/html", html);
}

void handleAPITemperature() {
    JsonDocument doc;
    doc["setpoint"] = temperatureSetpoint;
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

            temperatureSetpoint = newTemp;

            Serial.printf("Temperature set via API: %.1f°C\n", temperatureSetpoint);

            // Update display
            drawTemperatureUI();

            // Send response
            JsonDocument responseDoc;
            responseDoc["success"] = true;
            responseDoc["setpoint"] = temperatureSetpoint;

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
    long newPosition = M5Dial.Encoder.read();

    if (newPosition != lastEncoderPosition) {
        long diff = newPosition - lastEncoderPosition;
        lastEncoderPosition = newPosition;

        // Adjust temperature
        temperatureSetpoint += diff * TEMP_STEP;

        // Clamp to valid range
        if (temperatureSetpoint < TEMP_MIN) temperatureSetpoint = TEMP_MIN;
        if (temperatureSetpoint > TEMP_MAX) temperatureSetpoint = TEMP_MAX;

        Serial.printf("Encoder: %ld, Temperature: %.1f°C\n", newPosition, temperatureSetpoint);

        // Immediate UI update for responsiveness
        displayedTemperature = temperatureSetpoint;
        drawTemperatureUI();
    }

    // Handle encoder button press (reset to default)
    if (M5Dial.BtnA.wasPressed()) {
        temperatureSetpoint = TEMP_DEFAULT;
        displayedTemperature = temperatureSetpoint;
        Serial.printf("Reset to default: %.1f°C\n", temperatureSetpoint);
        drawTemperatureUI();
    }
}

void handleTouchInput() {
    auto touch = M5Dial.Touch.getDetail();

    if (touch.wasPressed()) {
        lastTouchTime = millis();

        // Calculate touch position relative to center
        int dx = touch.x - centerX;
        int dy = touch.y - centerY;

        // Calculate distance from center
        float distance = sqrt(dx * dx + dy * dy);

        // If touch is on the arc area, adjust temperature based on angle
        if (distance > arcRadius - arcThickness - 10 && distance < arcRadius + 10) {
            // Calculate angle from touch position
            float angle = atan2(dy, dx) * 180.0 / PI;

            // Convert angle to temperature (map -135 to 135 degrees to temp range)
            // Adjust for the arc starting at bottom-left
            angle = angle + 90;  // Rotate reference
            if (angle < -135) angle += 360;
            if (angle > 225) angle -= 360;

            // Map angle to temperature
            if (angle >= -135 && angle <= 135) {
                float newTemp = mapFloat(angle, -135, 135, TEMP_MIN, TEMP_MAX);
                temperatureSetpoint = newTemp;

                // Clamp to valid range
                if (temperatureSetpoint < TEMP_MIN) temperatureSetpoint = TEMP_MIN;
                if (temperatureSetpoint > TEMP_MAX) temperatureSetpoint = TEMP_MAX;

                Serial.printf("Touch set temperature: %.1f°C\n", temperatureSetpoint);
                drawTemperatureUI();
            }
        }
        // If touch is in center, reset to default
        else if (distance < 50) {
            temperatureSetpoint = TEMP_DEFAULT;
            displayedTemperature = temperatureSetpoint;
            Serial.printf("Touch reset to default: %.1f°C\n", temperatureSetpoint);
            drawTemperatureUI();
        }
    }
}

void drawTemperatureUI() {
    M5Dial.Display.fillScreen(COLOR_BACKGROUND);

    // Draw background arc (full range)
    for (int angle = -135; angle <= 135; angle += 2) {
        float rad = angle * PI / 180.0;
        int x1 = centerX + cos(rad) * (arcRadius - arcThickness);
        int y1 = centerY + sin(rad) * (arcRadius - arcThickness);
        int x2 = centerX + cos(rad) * arcRadius;
        int y2 = centerY + sin(rad) * arcRadius;
        M5Dial.Display.drawLine(x1, y1, x2, y2, COLOR_ARC_BG);
    }

    // Draw colored arc showing current temperature
    float tempPercent = (displayedTemperature - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    int endAngle = -135 + (int)(tempPercent * 270);

    for (int angle = -135; angle <= endAngle; angle += 2) {
        // Calculate color gradient based on position in arc
        float arcPercent = (float)(angle + 135) / 270.0;
        uint16_t color = getTemperatureColor(TEMP_MIN + arcPercent * (TEMP_MAX - TEMP_MIN));

        float rad = angle * PI / 180.0;
        int x1 = centerX + cos(rad) * (arcRadius - arcThickness);
        int y1 = centerY + sin(rad) * (arcRadius - arcThickness);
        int x2 = centerX + cos(rad) * arcRadius;
        int y2 = centerY + sin(rad) * arcRadius;
        M5Dial.Display.drawLine(x1, y1, x2, y2, color);
    }

    // Draw setpoint indicator
    float setpointPercent = (temperatureSetpoint - TEMP_MIN) / (TEMP_MAX - TEMP_MIN);
    int setpointAngle = -135 + (int)(setpointPercent * 270);
    float setpointRad = setpointAngle * PI / 180.0;

    // Draw indicator dot
    int indicatorX = centerX + cos(setpointRad) * (arcRadius + 8);
    int indicatorY = centerY + sin(setpointRad) * (arcRadius + 8);
    M5Dial.Display.fillCircle(indicatorX, indicatorY, 5, COLOR_SETPOINT);

    // Draw temperature value in center
    M5Dial.Display.setTextColor(COLOR_TEXT);
    M5Dial.Display.setTextDatum(middle_center);

    // Large temperature display
    M5Dial.Display.setTextSize(2);
    char tempStr[10];
    snprintf(tempStr, sizeof(tempStr), "%.1f", temperatureSetpoint);
    M5Dial.Display.drawString(tempStr, centerX, centerY - 15);

    // Celsius symbol
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.drawString("°C", centerX, centerY + 20);

    // Draw min/max labels
    M5Dial.Display.setTextSize(1);
    M5Dial.Display.setTextColor(COLOR_ARC_COLD);
    M5Dial.Display.drawString(String((int)TEMP_MIN).c_str(), centerX - 70, centerY + 60);
    M5Dial.Display.setTextColor(COLOR_ARC_HOT);
    M5Dial.Display.drawString(String((int)TEMP_MAX).c_str(), centerX + 70, centerY + 60);

    // Draw WiFi status indicator
    if (wifiConnected) {
        M5Dial.Display.setTextColor(COLOR_SETPOINT);
        M5Dial.Display.fillCircle(centerX, 20, 5, COLOR_SETPOINT);
    } else {
        M5Dial.Display.setTextColor(COLOR_ARC_HOT);
        M5Dial.Display.fillCircle(centerX, 20, 5, COLOR_ARC_HOT);
    }
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

float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
