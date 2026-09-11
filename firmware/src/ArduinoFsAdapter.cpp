#include "ArduinoFsAdapter.h"

int ArduinoFsAdapter::size(const char* path) {
  File f = _fs.open(path, "r");
  if (!f) return -1;
  int n = (int)f.size();
  f.close();
  return n;
}

int ArduinoFsAdapter::readAt(const char* path, uint32_t offset, void* buf, size_t len) {
  File f = _fs.open(path, "r");
  if (!f) return -1;
  if (!f.seek(offset)) { f.close(); return 0; }
  int n = (int)f.read((uint8_t*)buf, len);
  f.close();
  return n;
}

bool ArduinoFsAdapter::writeAt(const char* path, uint32_t offset, const void* buf, size_t len) {
  int cur = size(path);
  File f;
  if (cur < 0) {
    f = _fs.open(path, "w");
  } else if ((int)offset >= cur) {
    f = _fs.open(path, "a");            // append (SPIFFS-friendly)
  } else {
    f = _fs.open(path, "r+");           // overwrite in place
    if (f && !f.seek(offset)) { f.close(); return false; }
  }
  if (!f) return false;
  size_t n = f.write((const uint8_t*)buf, len);
  f.close();
  return n == len;
}
