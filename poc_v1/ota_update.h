#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

class OTAManager {
private:
  const char* currentVersion;
  unsigned long lastCheckTime;
  unsigned long checkInterval;
  unsigned long noVoteTimeThreshold;
  void (*preUpdateCallback)() = nullptr;
  void (*postUpdateCallback)() = nullptr;
  
  String getLatestReleaseVersion();
  bool isVersionNewer(const String& version1, const String& version2);
  String constructDownloadUrl(const String& version);

public:
  void init(const char* currentVersion);
  void setPreUpdateCallback(void (*callback)());
  void setPostUpdateCallback(void (*callback)());
  void handle();
  bool checkForUpdate();
  bool performUpdate();
  bool shouldCheckForUpdate();
};

extern OTAManager ota;
extern volatile unsigned long lastAnyVoteTime;

#endif
