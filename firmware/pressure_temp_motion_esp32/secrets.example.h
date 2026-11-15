// Copy this file to secrets.h and fill in real values. secrets.h is git-ignored.
#pragma once

struct WifiCredentials {
  const char* ssid;
  const char* password;
};

// tried in order until one connects
const WifiCredentials WIFI_NETWORKS[] = {
  {"your-wifi-name", "your-wifi-password"},
  {"backup-hotspot", "backup-password"},
};
const int WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

// HTTPS is recommended. Plain HTTP sends readings unencrypted.
const char* const DATA_SERVER_URL = "http://your-server:5000/api/data";
