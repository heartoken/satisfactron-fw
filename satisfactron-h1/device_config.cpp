#include "device_config.h"
#include <nvs_flash.h>
#include <nvs.h>

#define NVS_NAMESPACE "device_config"
#define NVS_KEY_DEVICE_ID "device_id"
#define NVS_KEY_WIFI_SSID "wifi_ssid"
#define NVS_KEY_WIFI_PASS "wifi_pass"

#define DEFAULT_DEVICE_ID "d6e82982-697f-11f0-bb42-fbdbc29338ec"
#define DEFAULT_WIFI_SSID "heartoken_default"
#define DEFAULT_WIFI_PASS "satisfactron_42"

DeviceConfig deviceConfig;

bool DeviceConfig::init() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  
  if (err != ESP_OK) {
    Serial.printf("❌ NVS init failed: %s\n", esp_err_to_name(err));
    return false;
  }
  
  nvs_handle_t handle;
  err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    Serial.printf("❌ NVS open failed: %s\n", esp_err_to_name(err));
    deviceId = DEFAULT_DEVICE_ID;
    wifiSSID = DEFAULT_WIFI_SSID;
    wifiPassword = DEFAULT_WIFI_PASS;
    return true;
  }
  
  // Load Device ID
  size_t required_size = 0;
  err = nvs_get_str(handle, NVS_KEY_DEVICE_ID, NULL, &required_size);
  
  if (err == ESP_OK && required_size > 0) {
    char* stored_id = (char*)malloc(required_size);
    err = nvs_get_str(handle, NVS_KEY_DEVICE_ID, stored_id, &required_size);
    if (err == ESP_OK) {
      deviceId = String(stored_id);
      Serial.printf("📱 Device ID loaded from NVS: %s\n", stored_id);
    }
    free(stored_id);
  } else {
    deviceId = DEFAULT_DEVICE_ID;
    err = nvs_set_str(handle, NVS_KEY_DEVICE_ID, deviceId.c_str());
    if (err == ESP_OK) {
      nvs_commit(handle);
      Serial.printf("📱 Default device ID stored: %s\n", deviceId.c_str());
    }
  }
  
  // Load WiFi SSID
  required_size = 0;
  err = nvs_get_str(handle, NVS_KEY_WIFI_SSID, NULL, &required_size);
  
  if (err == ESP_OK && required_size > 0) {
    char* stored_ssid = (char*)malloc(required_size);
    err = nvs_get_str(handle, NVS_KEY_WIFI_SSID, stored_ssid, &required_size);
    if (err == ESP_OK) {
      wifiSSID = String(stored_ssid);
      Serial.printf("🌐 WiFi SSID loaded from NVS: %s\n", stored_ssid);
    }
    free(stored_ssid);
  } else {
    wifiSSID = DEFAULT_WIFI_SSID;
    if (wifiSSID.length() > 0) {
      err = nvs_set_str(handle, NVS_KEY_WIFI_SSID, wifiSSID.c_str());
      if (err == ESP_OK) {
        nvs_commit(handle);
        Serial.printf("🌐 Default WiFi SSID stored: %s\n", wifiSSID.c_str());
      }
    }
  }
  
  // Load WiFi Password
  required_size = 0;
  err = nvs_get_str(handle, NVS_KEY_WIFI_PASS, NULL, &required_size);
  
  if (err == ESP_OK && required_size > 0) {
    char* stored_pass = (char*)malloc(required_size);
    err = nvs_get_str(handle, NVS_KEY_WIFI_PASS, stored_pass, &required_size);
    if (err == ESP_OK) {
      wifiPassword = String(stored_pass);
      Serial.printf("🔐 WiFi password loaded from NVS (***)\n");
    }
    free(stored_pass);
  } else {
    wifiPassword = DEFAULT_WIFI_PASS;
    if (wifiPassword.length() > 0) {
      err = nvs_set_str(handle, NVS_KEY_WIFI_PASS, wifiPassword.c_str());
      if (err == ESP_OK) {
        nvs_commit(handle);
        Serial.printf("🔐 Default WiFi password stored (***)\n");
      }
    }
  }
  
  nvs_close(handle);
  return true;
}

String DeviceConfig::getDeviceId() const {
  return deviceId;
}

bool DeviceConfig::setDeviceId(const String& newId) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return false;
  
  err = nvs_set_str(handle, NVS_KEY_DEVICE_ID, newId.c_str());
  if (err == ESP_OK) {
    err = nvs_commit(handle);
    if (err == ESP_OK) {
      deviceId = newId;
      Serial.printf("📱 Device ID updated: %s\n", newId.c_str());
    }
  }
  
  nvs_close(handle);
  return (err == ESP_OK);
}

void DeviceConfig::printDeviceId() {
  Serial.printf("📱 Current Device ID: %s\n", deviceId.c_str());
}

String DeviceConfig::getWiFiSSID() const {
  return wifiSSID;
}

String DeviceConfig::getWiFiPassword() const {
  return wifiPassword;
}

bool DeviceConfig::setWiFiCredentials(const String& ssid, const String& password) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return false;
  
  bool success = true;
  
  // Store SSID
  err = nvs_set_str(handle, NVS_KEY_WIFI_SSID, ssid.c_str());
  if (err != ESP_OK) success = false;
  
  // Store Password
  err = nvs_set_str(handle, NVS_KEY_WIFI_PASS, password.c_str());
  if (err != ESP_OK) success = false;
  
  if (success) {
    err = nvs_commit(handle);
    if (err == ESP_OK) {
      wifiSSID = ssid;
      wifiPassword = password;
      Serial.printf("🌐 WiFi credentials updated: %s / ***\n", ssid.c_str());
    } else {
      success = false;
    }
  }
  
  nvs_close(handle);
  return success;
}

void DeviceConfig::printWiFiConfig() {
  Serial.printf("🌐 WiFi SSID: %s\n", wifiSSID.c_str());
  Serial.printf("🔐 WiFi Password: %s\n", (wifiPassword.length() > 0) ? "***" : "(empty)");
}
