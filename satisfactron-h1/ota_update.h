#pragma once
#include <Arduino.h>

class OTAManager {
private:
  const char* currentVersion;
  unsigned long lastCheckTime;
  unsigned long checkInterval;
  unsigned long noVoteTimeThreshold;
  void (*preUpdateCallback)();
  void (*postUpdateCallback)();
  
  String getLatestReleaseVersion();
  bool isVersionNewer(const String& version1, const String& version2);
  String constructDownloadUrl(const String& version);
  String followRedirect(const String& url);  // Add this declaration
  
public:
  void init(const char* version);
  void handle();
  bool checkForUpdate();
  bool forceCheckForUpdate();
  bool performUpdate();
  bool shouldCheckForUpdate();
  void setPreUpdateCallback(void (*callback)());
  void setPostUpdateCallback(void (*callback)());
};
