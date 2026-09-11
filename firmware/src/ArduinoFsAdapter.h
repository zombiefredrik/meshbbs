#pragma once
// BbsFs implementation over an Arduino fs::FS (SPIFFS on ESP32, LittleFS/InternalFS elsewhere).
#include <Arduino.h>
#include <FS.h>
#include "BbsFs.h"

class ArduinoFsAdapter : public BbsFs {
public:
  explicit ArduinoFsAdapter(fs::FS& fs) : _fs(fs) {}
  int  size(const char* path) override;
  int  readAt(const char* path, uint32_t offset, void* buf, size_t len) override;
  bool writeAt(const char* path, uint32_t offset, const void* buf, size_t len) override;
  bool remove(const char* path) override { return _fs.remove(path); }
  bool rename(const char* from, const char* to) override { return _fs.rename(from, to); }
private:
  fs::FS& _fs;
};
