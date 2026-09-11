#pragma once
#include "BbsFs.h"
#include <string>

// BbsFs over plain files in a directory (used by the REPL and tests).
class StdioFs : public BbsFs {
public:
  explicit StdioFs(const char* dir);
  int  size(const char* path) override;
  int  readAt(const char* path, uint32_t offset, void* buf, size_t len) override;
  bool writeAt(const char* path, uint32_t offset, const void* buf, size_t len) override;
  bool remove(const char* path) override;
  bool rename(const char* from, const char* to) override;
private:
  std::string full(const char* path);
  std::string _dir;
};
