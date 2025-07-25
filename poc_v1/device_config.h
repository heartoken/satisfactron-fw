#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <Arduino.h>

class DeviceConfig {
private:
  static const char* NVS_NAMESPACE;
  static const char* DEVICE_ID_KEY;
  static const char* WIFI_SSID_KEY;
  static const char* WIFI_PASS_KEY;
  String deviceId;
  String wifiSSID;
  String wifiPassword;

public:
  bool init();
  String getDeviceId() const;
  bool setDeviceId(const String& id);
  void printDeviceId();
  
  // WiFi credentials
  String getWiFiSSID() const;
  String getWiFiPassword() const;
  bool setWiFiCredentials(const String& ssid, const String& password);
  void printWiFiConfig();
};

extern DeviceConfig deviceConfig;

#endif
