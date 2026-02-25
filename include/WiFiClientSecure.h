#pragma once

// Compatibility shim for libraries that include <WiFiClientSecure.h>.
// Try the common header locations across ESP32 Arduino core variants.

#if __has_include(<WiFiClientSecure.h>)
  #include <WiFiClientSecure.h>
#elif __has_include(<WiFiClientSecure/WiFiClientSecure.h>)
  #include <WiFiClientSecure/WiFiClientSecure.h>
#elif __has_include(<NetworkClientSecure.h>)
  #include <NetworkClientSecure.h>
  using WiFiClientSecure = NetworkClientSecure;
#else
  #error "No secure client header found (WiFiClientSecure / NetworkClientSecure). If you don't need wss://, build with -DWEBSOCKETS_DISABLE_SSL."
#endif