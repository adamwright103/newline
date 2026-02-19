#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "secret.h"

// Set an interval to fetch data (e.g., every 10 minutes)
const unsigned long fetchInterval = 600000; 
unsigned long previousMillis = 0;

void setup() {
  Serial.begin(115200);
  while (Serial.available() == 0) {
    // Optional: Print a waiting message or an indicator
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("\n--- ESP32 Weather Fetcher ---");

  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Do an initial fetch immediately on startup
  fetchWeatherData();
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Non-blocking timer to fetch data periodically
  if (currentMillis - previousMillis >= fetchInterval) {
    previousMillis = currentMillis;
  }
}

void fetchWeatherData() {
  if (WiFi.status() == WL_CONNECTED) {
    // 1. Setup a secure client and tell it to ignore SSL certificate validation
    WiFiClientSecure client;
    client.setInsecure(); 
    
    HTTPClient http;
    
    // Construct the target URL
    String url = scriptUrl + "?key=" + scriptKey;
    Serial.println("\nFetching data from URL...");
    
    // 2. Begin the HTTP request using the secure client
    http.begin(client, url);
    
    // 3. THIS IS THE MAGIC LINE: Tell HTTPClient to follow the 302 redirect
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_FOUND) {
        String payload = http.getString();
        
        // Parse the JSON payload (ArduinoJson v7 syntax)
        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
          Serial.print("JSON Deserialization failed: ");
          Serial.println(error.c_str());
          return;
        }
        
        // Extract the required values
        float maxTemp = doc["weather"]["max_temp"];
        const char* sunrise = doc["weather"]["sunrise"];
        const char* sunset = doc["weather"]["sunset"];
        
        // Print the extracted data to the Serial Monitor
        Serial.println("=== Weather Data ===");
        Serial.print("Max Temp: ");
        Serial.println(maxTemp);
        Serial.print("Sunrise:  ");
        Serial.println(sunrise);
        Serial.print("Sunset:   ");
        Serial.println(sunset);
        Serial.println("====================");
        
      } else {
        Serial.printf("HTTP GET failed with code: %d\n", httpCode);
      }
    } else {
      Serial.printf("HTTP Connection failed: %s\n", http.errorToString(httpCode).c_str());
    }
    
    // Free resources
    http.end();
  } else {
    Serial.println("WiFi Disconnected. Unable to fetch data.");
  }
}