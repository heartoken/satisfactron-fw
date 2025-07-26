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
  if (!shouldCheckForUpdate()) {
    // Enhanced logging for why we're not checking
    unsigned long now = millis();
    unsigned long timeSinceLastCheck = now - lastCheckTime;
    unsigned long timeSinceLastVote = (lastAnyVoteTime > 0) ? (now - lastAnyVoteTime) : 0;
    
    Serial.printf("🚫 OTA check skipped - Time since last check: %lums (need %lu), Time since vote: %lums (need >%lu)\n", 
                  timeSinceLastCheck, checkInterval, timeSinceLastVote, noVoteTimeThreshold);
    return;
  }
  
  lastCheckTime = millis();
  Serial.println("🔍 Periodic OTA check starting...");
  
  if (checkForUpdate()) {
    Serial.println("🆕 Periodic update found, starting download...");
    performUpdate();
  } else {
    Serial.println("✅ Periodic check: No update needed");
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
  if (now - lastCheckTime < checkInterval) {
    Serial.printf("🕒 Too soon since last check (%lums < %lums)\n", now - lastCheckTime, checkInterval);
    return false;
  }
  
  // Check if there were recent votes
  if (lastAnyVoteTime > 0 && (now - lastAnyVoteTime) < noVoteTimeThreshold) {
    Serial.printf("🗳️ Recent vote activity (%lums < %lums)\n", now - lastAnyVoteTime, noVoteTimeThreshold);
    return false;
  }
  
  Serial.println("✅ OTA check conditions met");
  return true;
}

bool OTAManager::forceCheckForUpdate() {
  Serial.println("🔧 FORCE CHECK: Bypassing time/vote restrictions");
  return checkForUpdate();
}

String OTAManager::getLatestReleaseVersion() {
  Serial.println("📡 Fetching latest release from GitHub API...");
  
  HTTPClient http;
  http.begin(GITHUB_API_URL);
  http.addHeader("User-Agent", "Satisfactron-Device");
  http.setTimeout(15000); // 15 second timeout
  
  Serial.printf("🌐 GET %s\n", GITHUB_API_URL);
  
  int httpCode = http.GET();
  String payload = http.getString();
  http.end();
  
  Serial.printf("📱 HTTP Response: %d\n", httpCode);
  
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("❌ GitHub API request failed: %d\n", httpCode);
    Serial.printf("Response: %s\n", payload.c_str());
    return "";
  }
  
  Serial.printf("📄 Response length: %d bytes\n", payload.length());
  Serial.println("📄 Response preview:");
  Serial.println(payload.substring(0, 200) + "...");
  
  DynamicJsonDocument doc(4096); // Increased size
  DeserializationError error = deserializeJson(doc, payload);
  
  if (error) {
    Serial.printf("❌ JSON parsing failed: %s\n", error.c_str());
    return "";
  }
  
  String tagName = doc["tag_name"];
  bool prerelease = doc["prerelease"];
  
  Serial.printf("🏷️ Found release: %s (prerelease: %s)\n", tagName.c_str(), prerelease ? "yes" : "no");
  
  if (tagName.startsWith("v")) {
    tagName = tagName.substring(1); // Remove 'v' prefix
    Serial.printf("🔄 Cleaned version: %s\n", tagName.c_str());
  }
  
  return tagName;
}

bool OTAManager::isVersionNewer(const String& version1, const String& version2) {
  Serial.printf("🔍 Comparing versions: %s vs %s\n", version1.c_str(), version2.c_str());
  
  // Parse versions in format YY.MM.DD.N
  int v1Parts[4] = {0}, v2Parts[4] = {0};
  int parsed1 = sscanf(version1.c_str(), "%d.%d.%d.%d", &v1Parts[0], &v1Parts[1], &v1Parts[2], &v1Parts[3]);
  int parsed2 = sscanf(version2.c_str(), "%d.%d.%d.%d", &v2Parts[0], &v2Parts[1], &v2Parts[2], &v2Parts[3]);
  
  Serial.printf("📊 Parsed v1 (%d parts): %d.%d.%d.%d\n", parsed1, v1Parts[0], v1Parts[1], v1Parts[2], v1Parts[3]);
  Serial.printf("📊 Parsed v2 (%d parts): %d.%d.%d.%d\n", parsed2, v2Parts[0], v2Parts[1], v2Parts[2], v2Parts[3]);
  
  // Compare each part
  for (int i = 0; i < 4; i++) {
    if (v1Parts[i] > v2Parts[i]) {
      Serial.printf("✅ %s is newer (part %d: %d > %d)\n", version1.c_str(), i, v1Parts[i], v2Parts[i]);
      return true;
    }
    if (v1Parts[i] < v2Parts[i]) {
      Serial.printf("❌ %s is older (part %d: %d < %d)\n", version1.c_str(), i, v1Parts[i], v2Parts[i]);
      return false;
    }
  }
  
  Serial.println("⚖️ Versions are equal");
  return false; // versions are equal
}

String OTAManager::constructDownloadUrl(const String& version) {
  String url = String(GITHUB_DOWNLOAD_BASE) + "v" + version + "/satisfactron-h1-fw_" + version + ".bin";
  Serial.printf("🔗 Download URL: %s\n", url.c_str());
  return url;
}

String OTAManager::followRedirect(const String& url) {
  Serial.printf("🔄 Following redirect for: %s\n", url.c_str());
  
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("User-Agent", "Satisfactron-Device");
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  String finalUrl = http.getLocation();
  
  Serial.printf("🔄 Redirect HTTP Response: %d\n", httpCode);
  Serial.printf("🔄 Final URL: %s\n", finalUrl.c_str());
  
  http.end();
  
  if (httpCode == 200 && finalUrl.length() > 0) {
    return finalUrl;
  }
  
  return url; // Return original if redirect fails
}

bool OTAManager::checkForUpdate() {
  Serial.println("🔍 Starting update check...");
  
  String latestVersion = getLatestReleaseVersion();
  if (latestVersion.length() == 0) {
    Serial.println("❌ Could not fetch latest version");
    return false;
  }
  
  Serial.printf("🔍 Version comparison: Current=%s, Latest=%s\n", currentVersion, latestVersion.c_str());
  
  if (isVersionNewer(latestVersion, currentVersion)) {
    Serial.println("🆕 Update available!");
    return true;
  } else {
    Serial.println("✅ Current version is up to date");  
    return false;
  }
}

bool OTAManager::performUpdate() {
  Serial.println("🚀 Starting OTA update process...");
  
  String latestVersion = getLatestReleaseVersion();
  if (latestVersion.length() == 0) {
    Serial.println("❌ Could not determine latest version for update");
    return false;
  }
  
  String downloadUrl = constructDownloadUrl(latestVersion);
  Serial.printf("🔄 Starting OTA update from: %s\n", downloadUrl.c_str());
  
  // Follow any redirects to get the actual download URL
  String finalUrl = followRedirect(downloadUrl);
  Serial.printf("🎯 Final download URL: %s\n", finalUrl.c_str());
  
  // Set LEDs to purple before update
  if (preUpdateCallback) {
    Serial.println("💜 Calling pre-update callback (purple LEDs)");
    preUpdateCallback();
  }
  
  // Configure HTTPUpdate with redirect following
  HTTPUpdate httpUpdate;
  httpUpdate.setLedPin(LED_BUILTIN, LOW);
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false); // We'll handle reboot manually
  
  // Create WiFiClientSecure for HTTPS
  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification for GitHub
  
  Serial.println("🌐 Starting HTTP update...");
  
  // Use the final URL (after redirects)
  HTTPUpdateResult result = httpUpdate.update(client, finalUrl, currentVersion);
  
  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("❌ OTA Update failed (Error %d): %s\n", 
                    httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      
      // Additional error details
      Serial.printf("🔍 HTTPUpdate Error Code: %d\n", httpUpdate.getLastError());
      
      if (postUpdateCallback) {
        Serial.println("🔄 Calling post-update callback (restore LEDs)");
        postUpdateCallback();
      }
      return false;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("ℹ️ OTA: No update needed (server says current)");
      if (postUpdateCallback) {
        Serial.println("🔄 Calling post-update callback (restore LEDs)");
        postUpdateCallback();
      }
      return false;
      
    case HTTP_UPDATE_OK:
      Serial.println("✅ OTA Update successful! Rebooting in 3 seconds...");
      delay(3000);
      ESP.restart();
      return true;
  }
  
  Serial.println("❓ Unknown OTA result");
  return false;
}
