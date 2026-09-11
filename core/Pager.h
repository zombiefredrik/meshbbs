#pragma once
// Accumulates text and splits it into pages of at most BBS_PAGE_MAX bytes,
// breaking on newlines/spaces and never inside a UTF-8 sequence.
#include "BbsTypes.h"

class Pager {
public:
  explicit Pager(BbsOutput& out);
  void line(const char* s);                       // append s + '\n'
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void text(const char* s);                       // append raw
  void flush();                                   // split into out.pages; called by engine
  static int utf8SafeCut(const char* s, int max); // largest n <= max not splitting a UTF-8 char
private:
  BbsOutput& _out;
  char _buf[BBS_MAX_PAGES * BBS_PAGE_MAX + 64];
  int  _len;
  bool _flushed;
};
