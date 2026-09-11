#include "Pager.h"
#include <stdio.h>
#include <stdarg.h>

Pager::Pager(BbsOutput& out) : _out(out), _len(0), _flushed(false) { _buf[0] = 0; _out.clear(); }

void Pager::text(const char* s) {
  int room = (int)sizeof(_buf) - 1 - _len;
  if (room <= 0) return;
  int n = (int)strlen(s);
  if (n > room) n = utf8SafeCut(s, room);
  memcpy(_buf + _len, s, n);
  _len += n;
  _buf[_len] = 0;
}

void Pager::line(const char* s) { text(s); text("\n"); }

void Pager::printf(const char* fmt, ...) {
  char tmp[BBS_PAGE_MAX * 2];
  va_list ap; va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  text(tmp);
}

int Pager::utf8SafeCut(const char* s, int max) {
  int n = (int)strlen(s);
  if (n <= max) return n;
  int cut = max;
  while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;   // back up to a char boundary
  return cut;
}

void Pager::flush() {
  if (_flushed) return;
  _flushed = true;
  // trim trailing newlines
  while (_len > 0 && _buf[_len - 1] == '\n') _buf[--_len] = 0;
  _out.count = 0;
  if (_len == 0) return;

  // First pass: decide whether we need a footer at all (single page fits without one).
  if (_len <= BBS_PAGE_MAX) {
    memcpy(_out.pages[0], _buf, _len + 1);
    _out.count = 1;
    return;
  }
  const int footer_len = 6;   // " (n/m)"
  const int body_max = BBS_PAGE_MAX - footer_len;
  int pos = 0;
  int starts[BBS_MAX_PAGES + 1];
  int lens[BBS_MAX_PAGES + 1];
  int pages = 0;
  while (pos < _len && pages < BBS_MAX_PAGES) {
    int remain = _len - pos;
    int take = remain <= body_max ? remain : body_max;
    if (take < remain) {
      // prefer breaking at newline, then space, within the last 40% of the page
      int best = -1;
      for (int i = take; i > body_max * 6 / 10; i--) {
        char c = _buf[pos + i];
        if (c == '\n') { best = i; break; }
        if (c == ' ' && best < 0) best = i;
      }
      if (best > 0) take = best;
      else take = utf8SafeCut(_buf + pos, take);
    }
    starts[pages] = pos;
    lens[pages] = take;
    pages++;
    pos += take;
    while (pos < _len && (_buf[pos] == ' ' || _buf[pos] == '\n')) pos++;   // swallow the separator
  }
  bool truncated = pos < _len;
  for (int p = 0; p < pages; p++) {
    char* dst = _out.pages[p];
    int n = lens[p];
    memcpy(dst, _buf + starts[p], n);
    // strip trailing whitespace
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\n')) n--;
    if (truncated && p == pages - 1) {
      // make room for "..." marker
      while (n > body_max - 3) n = utf8SafeCut(dst, n - 1);
      memcpy(dst + n, "...", 3); n += 3;
    }
    snprintf(dst + n, BBS_PAGE_MAX + 1 - n, " (%d/%d)", p + 1, pages);
  }
  _out.count = pages;
}
