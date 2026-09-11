#include "StdioFs.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

StdioFs::StdioFs(const char* dir) : _dir(dir) { mkdir(dir, 0755); }

std::string StdioFs::full(const char* path) {
  // flatten "/bbs/area0.dat" -> "<dir>/bbs_area0.dat"
  std::string p = path;
  for (auto& c : p) if (c == '/') c = '_';
  return _dir + "/" + p;
}

int StdioFs::size(const char* path) {
  struct stat st;
  if (stat(full(path).c_str(), &st) != 0) return -1;
  return (int)st.st_size;
}

int StdioFs::readAt(const char* path, uint32_t offset, void* buf, size_t len) {
  FILE* f = fopen(full(path).c_str(), "rb");
  if (!f) return -1;
  fseek(f, offset, SEEK_SET);
  size_t n = fread(buf, 1, len, f);
  fclose(f);
  return (int)n;
}

bool StdioFs::writeAt(const char* path, uint32_t offset, const void* buf, size_t len) {
  std::string p = full(path);
  FILE* f = fopen(p.c_str(), "r+b");
  if (!f) f = fopen(p.c_str(), "w+b");
  if (!f) return false;
  fseek(f, offset, SEEK_SET);
  size_t n = fwrite(buf, 1, len, f);
  fclose(f);
  return n == len;
}

bool StdioFs::remove(const char* path) { return ::remove(full(path).c_str()) == 0; }
bool StdioFs::rename(const char* from, const char* to) { return ::rename(full(from).c_str(), full(to).c_str()) == 0; }
