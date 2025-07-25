#include "ota_update.h"
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

#define GITHUB_API_URL "https://api.github.com/repos/heartoken/satisfactron-fw/releases/latest"
#define GITHUB_DOWNLOAD_BASE "https://github.com/heartoken/satisfactron-fw/releases/download/"

extern volatile uint32_t lastAnyVoteTime;

void OTAManager::init(const char* version) {
  this->currentVersion = version;
  this->lastCheckTime = 0;
  this->checkInterval = 5 * 60 * 1000; // 5 minutes
  this->noVoteTimeThreshold = 10 * 60 * 1000; // 10 minutes
  Serial.printf("🔄 OTA Manager initialized with version: %s\n", currentVersion);
}

void OTAManager::handle() {
  if (!shouldCheckForUpdate()) return;
  
  lastCheckTime = millis();
  Serial.println("🔍 Checking for firmware updates...");
  
  if (checkForUpdate()) {
    Serial.println("🆕 Update available, starting download...");
    performUpdate();
  }
}

void OTAManager::setPreUpdateCallback(void (*callback)()) {
  this->preUpdateCallback = callback;
}

void OTAManager::setPostUpdateCallback(void (*callback)()) {
  this->postUpdateCallback = callback;
}

bool OTAManager::shouldCheckForUpdate() {
  unsigned long now = millis();
  
  // Check if enough time has passed since last check
  if (now - lastCheckTime < checkInterval) return false;
  
  // Check if there were recent votes
  if (lastAnyVoteTime > 0 && (now - lastAnyVoteTime) < noVoteTimeThreshold) {
    return false;
  }
  
  return true;
}

String OTAManager::getLatestReleaseVersion() {
  HTTPClient http;
  http.begin(GITHUB_API_URL);
  http.addHeader("User-Agent", "Satisfactron-Device");
  
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("❌ GitHub API request failed: %d\n", httpCode);
    http.end();
    return "";
  }
  
  String payload = http.getString();
  http.end();
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error) {
    Serial.printf("❌ JSON parsing failed: %s\n", error.c_str());
    return "";
  }
  
  String tagName = doc["tag_name"];
  if (tagName.startsWith("v")) {
    tagName = tagName.substring(1); // Remove 'v' prefix
  }
  
  Serial.printf("🏷️ Latest release: %s\n", tagName.c_str());
  return tagName;
}

bool OTAManager::isVersionNewer(const String& version1, const String& version2) {
  // Parse versions in format YY.MM.DD.N
  int v1Parts[4], v2Parts[4];
  sscanf(version1.c_str(), "%d.%d.%d.%d", &v1Parts[0], &v1Parts[1], &v1Parts[2], &v1Parts[3]);
  sscanf(version2.c_str(), "%d.%d.%d.%d", &v2Parts[0], &v2Parts[1], &v2Parts[2], &v2Parts[3]);
  
  // Compare each part
  for (int i = 0; i < 4; i++) {
    if (v1Parts[i] > v2Parts[i]) return true;
    if (v1Parts[i] < v2Parts[i]) return false;
  }
  
  return false; // versions are equal
}

String OTAManager::constructDownloadUrl(const String& version) {
  return String(GITHUB_DOWNLOAD_BASE) + "v" + version + "/satisfactron-fw-h1_" + version + ".bin";
}

bool OTAManager::checkForUpdate() {
  String latestVersion = getLatestReleaseVersion();
  if (latestVersion.length() == 0) return false;
  
  Serial.printf("🔍 Current: %s, Latest: %s\n", currentVersion, latestVersion.c_str());
  
  if (isVersionNewer(latestVersion, currentVersion)) {
    Serial.println("🆕 Update available!");
    return true;
  }
  
  return false;
}

bool OTAManager::performUpdate() {
  String latestVersion = getLatestReleaseVersion();
  if (latestVersion.length() == 0) return false;
  
  String downloadUrl = constructDownloadUrl(latestVersion);
  Serial.printf("🔄 Starting OTA update from: %s\n", downloadUrl.c_str());
  
  // Set LEDs to purple
  if (preUpdateCallback) preUpdateCallback();
  
  // Configure HTTPUpdate
  HTTPUpdate httpUpdate;
  httpUpdate.setLedPin(LED_BUILTIN, LOW);
  
  // Create WiFiClientSecure for HTTPS
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for GitHub
  
  // Perform update - correct function signature
  HTTPUpdateResult result = httpUpdate.update(client, downloadUrl, currentVersion);
  
  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("❌ Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
      if (postUpdateCallback) postUpdateCallback();
      return false;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("ℹ️ No update needed");
      if (postUpdateCallback) postUpdateCallback();
      return false;
      
    case HTTP_UPDATE_OK:
      Serial.println("✅ Update successful! Rebooting...");
      // Device will reboot automatically
      return true;
  }
  
  return false;
}
