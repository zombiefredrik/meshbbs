#include "BbsStorage.h"
#include <stdio.h>

struct AreaHeader { uint32_t first_seq; };   // seq of record #0 in the file

BbsStorage::BbsStorage(BbsFs& fs, const char* root) : _fs(fs) {
  snprintf(_root, sizeof(_root), "%s", root);
  snprintf(_users_path, sizeof(_users_path), "%s/users.dat", _root);
  snprintf(_hs_path, sizeof(_hs_path), "%s/hiscore.dat", _root);
}

void BbsStorage::areaPath(int area, char* buf, size_t len, const char* suffix) {
  snprintf(buf, len, "%s/area%d.dat%s", _root, area, suffix);
}

// ---------------- users ----------------

int BbsStorage::userCount() {
  int sz = _fs.size(_users_path);
  return sz < 0 ? 0 : sz / (int)sizeof(BbsUser);
}

bool BbsStorage::getUserByIndex(int idx, BbsUser& out) {
  if (idx < 0 || idx >= userCount()) return false;
  return _fs.readAt(_users_path, (uint32_t)idx * sizeof(BbsUser), &out, sizeof(BbsUser)) == (int)sizeof(BbsUser);
}

int BbsStorage::findUserIndex(const uint8_t* pubkey) {
  int n = userCount();
  BbsUser u;
  for (int i = 0; i < n; i++) {
    if (getUserByIndex(i, u) && memcmp(u.pubkey, pubkey, BBS_PUBKEY_LEN) == 0) return i;
  }
  return -1;
}

bool BbsStorage::loadUser(const uint8_t* pubkey, BbsUser& out) {
  int i = findUserIndex(pubkey);
  return i >= 0 && getUserByIndex(i, out);
}

bool BbsStorage::loadUserByHandle(const char* handle, BbsUser& out) {
  int n = userCount();
  for (int i = 0; i < n; i++) {
    if (getUserByIndex(i, out) && strcasecmp(out.handle, handle) == 0) return true;
  }
  return false;
}

bool BbsStorage::saveUser(const BbsUser& u) {
  int i = findUserIndex(u.pubkey);
  if (i < 0) i = userCount();
  return _fs.writeAt(_users_path, (uint32_t)i * sizeof(BbsUser), &u, sizeof(BbsUser));
}

// ---------------- messages ----------------

int BbsStorage::firstSeq(int area) {
  char path[48]; areaPath(area, path, sizeof(path));
  AreaHeader h;
  if (_fs.readAt(path, 0, &h, sizeof(h)) != (int)sizeof(h)) return 1;
  return (int)h.first_seq;
}

int BbsStorage::messageCount(int area) {
  char path[48]; areaPath(area, path, sizeof(path));
  int sz = _fs.size(path);
  if (sz < (int)sizeof(AreaHeader)) return 0;
  int records = (sz - (int)sizeof(AreaHeader)) / (int)sizeof(BbsMessage);
  return firstSeq(area) - 1 + records;
}

bool BbsStorage::appendMessage(int area, const BbsMessage& m) {
  if (area < 0 || area >= BBS_MAX_AREAS) return false;
  char path[48]; areaPath(area, path, sizeof(path));
  int sz = _fs.size(path);
  if (sz < (int)sizeof(AreaHeader)) {
    AreaHeader h = { 1 };
    if (!_fs.writeAt(path, 0, &h, sizeof(h))) return false;
    sz = sizeof(AreaHeader);
  }
  if (!_fs.writeAt(path, (uint32_t)sz, &m, sizeof(m))) return false;
  int records = (sz - (int)sizeof(AreaHeader)) / (int)sizeof(BbsMessage) + 1;
  if (records > BBS_AREA_CAP) rotateArea(area);
  return true;
}

bool BbsStorage::readMessage(int area, int seq, BbsMessage& out) {
  if (area < 0 || area >= BBS_MAX_AREAS) return false;
  int first = firstSeq(area);
  if (seq < first || seq > messageCount(area)) return false;
  char path[48]; areaPath(area, path, sizeof(path));
  uint32_t off = sizeof(AreaHeader) + (uint32_t)(seq - first) * sizeof(BbsMessage);
  if (_fs.readAt(path, off, &out, sizeof(out)) != (int)sizeof(out)) return false;
  out.handle[BBS_HANDLE_LEN - 1] = 0;
  out.text[BBS_MSG_TEXT_LEN - 1] = 0;
  return true;
}

// Keep the newest half of the area, copying record by record through a temp file (no big buffers).
bool BbsStorage::rotateArea(int area) {
  char path[48], tmp[48];
  areaPath(area, path, sizeof(path));
  areaPath(area, tmp, sizeof(tmp), ".tmp");
  int first = firstSeq(area), last = messageCount(area);
  int keep = BBS_AREA_CAP / 2;
  int new_first = last - keep + 1;
  if (new_first <= first) return true;
  _fs.remove(tmp);
  AreaHeader h = { (uint32_t)new_first };
  if (!_fs.writeAt(tmp, 0, &h, sizeof(h))) return false;
  BbsMessage m;
  uint32_t off = sizeof(AreaHeader);
  for (int seq = new_first; seq <= last; seq++) {
    if (!readMessage(area, seq, m)) return false;
    if (!_fs.writeAt(tmp, off, &m, sizeof(m))) return false;
    off += sizeof(m);
  }
  _fs.remove(path);
  return _fs.rename(tmp, path);
}

bool BbsStorage::wipeArea(int area) {
  char path[48]; areaPath(area, path, sizeof(path));
  return _fs.remove(path);
}

// ---------------- highscores ----------------

int BbsStorage::loadHighscores(BbsHighscore* out, int max) {
  int sz = _fs.size(_hs_path);
  if (sz <= 0) return 0;
  int n = sz / (int)sizeof(BbsHighscore);
  if (n > max) n = max;
  int got = _fs.readAt(_hs_path, 0, out, n * sizeof(BbsHighscore));
  return got < 0 ? 0 : got / (int)sizeof(BbsHighscore);
}

bool BbsStorage::saveHighscores(const BbsHighscore* in, int n) {
  _fs.remove(_hs_path);
  return _fs.writeAt(_hs_path, 0, in, n * sizeof(BbsHighscore));
}
