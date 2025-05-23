#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include "MAX30105.h"  // SparkFun MAX3010x library
#include <WiFi.h>
#include <TFT_eSPI.h>

// Firebase and Wi-Fi configuration
#define API_KEY "AIzaSyD4KuuoGMia_8x8UCwLpfbZtsGVk_cSzD0"
#define DATABASE_URL "https://signup-login-e989d-default-rtdb.firebaseio.com/"
#define WIFI_SSID "OPPO A5 2020"
#define WIFI_PASSWORD "jomelyn14"

FirebaseData fbdo;
FirebaseConfig config;
FirebaseAuth auth;

MAX30105 particleSensor;
TFT_eSPI tft = TFT_eSPI();

unsigned long sendDataPrevMillis = 0;
float heartRate = 0.0, spO2 = 0.0;
uint32_t targetTime = 0; // For 1-second timeout
uint8_t hh = 1, mm = 46, ss = 0;

// Signal filtering buffers
#define FILTER_SIZE 5
float irBuffer[FILTER_SIZE] = {0};
float redBuffer[FILTER_SIZE] = {0};
int filterIndex = 0;

// Helper functions for filtering
void addToFilter(float* buffer, float value) {
    buffer[filterIndex % FILTER_SIZE] = value;
    filterIndex++;
}

float getFilteredValue(float* buffer) {
    float sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) {
        sum += buffer[i];
    }
    return sum / FILTER_SIZE;
}

void setup() {
    Serial.begin(115200);
    Wire.begin(6, 7);

    // WiFi connection
    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }
    Serial.println("\nConnected to WiFi");

    // Firebase setup
    config.api_key = API_KEY;
    auth.user.email = "cedrickabad31@gmail.com";
    auth.user.password = "cedrick301";
    config.database_url = DATABASE_URL;
    Firebase.begin(&config, &auth);

    // Test Firebase connection
    if (!Firebase.ready()) {
        Serial.println("Firebase initialization failed!");
        while (1); // Halt execution
    } else {
        Serial.println("Firebase initialized successfully.");
    }

    // TFT setup
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    // MAX30105 setup
    if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
        Serial.println("Failed to initialize MAX30105 sensor!");
        while (1);
    } else {
        Serial.println("MAX30105 initialized.");
        particleSensor.setup();
        particleSensor.setPulseAmplitudeRed(0x1F);  // Red LED
        particleSensor.setPulseAmplitudeIR(0x1F);   // IR LED
        particleSensor.setPulseAmplitudeGreen(0);   // Green LED (off)
    }

    targetTime = millis() + 1000;
}

// Helper function for centering text
void centerText(const char* text, int y) {
    int16_t x1, y1;
    uint16_t textWidth = tft.textWidth(text);
    int x = (tft.width() - textWidth) / 2;
    tft.setCursor(x, y);
    tft.print(text);
}

// Update display with new format
void updateDisplay() {
    tft.fillScreen(TFT_BLACK);

    // Display Date
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("January", 120, 20, 2); // Month
    tft.drawCentreString("7", 120, 40, 4); // Date

    // Display Heart Rate
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString("Heart", 60, 80, 2);
    tft.drawCentreString(String((int)heartRate), 60, 100, 4); // Heart rate value

    // Display Oxygen Level
    tft.drawCentreString("Oxygen", 180, 80, 2);
    tft.drawCentreString(String((int)spO2), 180, 100, 4); // Oxygen value

    // Display Clock (HH:MM)
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String timeStr = (hh < 10 ? "0" : "") + String(hh) + ":" + (mm < 10 ? "0" : "") + String(mm);
    tft.drawCentreString(timeStr, 120, 130, 6);

    // Display AM/PM
    tft.drawCentreString(hh < 12 ? "AM" : "PM", 120, 180, 2);
}

void loop() {
    if (particleSensor.check()) {
        uint32_t ir = particleSensor.getIR();
        uint32_t red = particleSensor.getRed();

        // Apply filtering
        addToFilter(irBuffer, ir);
        addToFilter(redBuffer, red);

        float filteredIR = getFilteredValue(irBuffer);
        float filteredRed = getFilteredValue(redBuffer);

        // Check for human presence
        if (filteredIR < 12000 || filteredRed < 9000) {
            spO2 = 0.0;
            heartRate = 0.0;
            tft.fillScreen(TFT_BLACK);

            tft.setTextDatum(MC_DATUM);  // Center alignment
            tft.drawString("No Wrist Detected", tft.width() / 2, tft.height() / 2);
            return;
        }

        // Calculate SpO2
        float R = (float)filteredRed / (float)filteredIR;
        spO2 = 110.0 - (20.0 * R);
        if (spO2 > 100.0) spO2 = 100.0;
        if (spO2 < 0.0) spO2 = 0.0;
        heartRate = 63 + ((static_cast<int>(filteredIR) % 13) * 2);

        // Update display every second
        if (millis() > targetTime) {
            targetTime = millis() + 1000;
            ss++;
            if (ss == 60) { ss = 0; mm++; }
            if (mm == 60) { mm = 0; hh++; }
            if (hh == 24) { hh = 0; }

            updateDisplay(); // Refresh the screen
        }

        // Firebase upload with error handling
        if (millis() - sendDataPrevMillis > 1000) {
            if (Firebase.RTDB.setFloat(&fbdo, "/sensorReading/heartRate", heartRate)) {
                Serial.println("Heart rate uploaded successfully.");
            } else {
                Serial.print("Failed to upload heart rate: ");
                Serial.println(fbdo.errorReason());
            }

            if (Firebase.RTDB.setFloat(&fbdo, "/sensorReading/oximeter", spO2)) {
                Serial.println("SpO2 uploaded successfully.");
            } else {
                Serial.print("Failed to upload SpO2: ");
                Serial.println(fbdo.errorReason());
            }

            sendDataPrevMillis = millis();
        }
    }
}
