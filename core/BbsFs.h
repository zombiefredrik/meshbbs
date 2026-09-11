#pragma once
// Minimal file abstraction the core needs. Implemented by host (stdio) and firmware (Arduino FS).
#include <stdint.h>
#include <stddef.h>

class BbsFs {
public:
  virtual ~BbsFs() {}
  // Returns file size in bytes, or -1 if it does not exist.
  virtual int  size(const char* path) = 0;
  // Read up to len bytes at offset. Returns bytes read (0 at EOF), -1 on error.
  virtual int  readAt(const char* path, uint32_t offset, void* buf, size_t len) = 0;
  // Write len bytes at offset, creating the file if needed (offset == size appends).
  virtual bool writeAt(const char* path, uint32_t offset, const void* buf, size_t len) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
};
