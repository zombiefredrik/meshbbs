#include "BbsEngine.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

BbsEngine::BbsEngine(BbsStorage& storage, const char* bbs_name, uint32_t boot_ts)
  : _st(storage), _boot_ts(boot_ts), _announce_logins(true) {
  snprintf(_name, sizeof(_name), "%s", bbs_name);
  _rng = boot_ts ^ 0x9E3779B9u;
  if (_rng == 0) _rng = 1;
  _last_event[0] = 0;
  memset(_sessions, 0, sizeof(_sessions));
}

// ---------------- helpers ----------------

uint32_t BbsEngine::rnd() {   // xorshift32
  uint32_t x = _rng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  _rng = x ? x : 1;
  return _rng;
}

static void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {   // Howard Hinnant's algorithm
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)(yoe) + (int)(era * 400);
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y++;
}

void BbsEngine::formatTime(uint32_t ts, char* buf, size_t len) {
  int y; unsigned m, d;
  civilFromDays((int64_t)(ts / 86400), y, m, d);
  unsigned secs = ts % 86400;
  snprintf(buf, len, "%02u-%02u %02u:%02u", m, d, secs / 3600, (secs / 60) % 60);
}

bool BbsEngine::validHandle(const char* h) {
  size_t n = strlen(h);
  if (n < 2 || n > BBS_HANDLE_LEN - 1) return false;
  for (size_t i = 0; i < n; i++) {
    char c = h[i];
    if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
  }
  return true;
}

int BbsEngine::areaByName(const char* name) {
  for (int i = 0; i < BBS_MAX_AREAS; i++) {
    if (strcasecmp(BBS_AREAS[i].name, name) == 0) return i;
  }
  if (isdigit((unsigned char)name[0])) {
    int n = atoi(name) - 1;
    if (n >= 0 && n < BBS_MAX_AREAS) return n;
  }
  return -1;
}

void BbsEngine::setEvent(const BbsSession& s, const char* what) {
  snprintf(_last_event, sizeof(_last_event), "%s: %s", s.handle[0] ? s.handle : "?", what);
}

BbsSession* BbsEngine::findSession(const uint8_t* pubkey) {
  for (int i = 0; i < BBS_MAX_SESSIONS; i++) {
    if (_sessions[i].active && memcmp(_sessions[i].pubkey, pubkey, BBS_PUBKEY_LEN) == 0) return &_sessions[i];
  }
  return NULL;
}

BbsSession* BbsEngine::allocSession(const uint8_t* pubkey, uint32_t now) {
  BbsSession* victim = NULL;
  for (int i = 0; i < BBS_MAX_SESSIONS; i++) {
    BbsSession& s = _sessions[i];
    if (!s.active) { victim = &s; break; }
    if (!(s.flags & BBS_USER_SYSOP) && (victim == NULL || s.last_activity < victim->last_activity)) victim = &s;
  }
  if (victim == NULL) victim = &_sessions[0];
  memset(victim, 0, sizeof(*victim));
  memcpy(victim->pubkey, pubkey, BBS_PUBKEY_LEN);
  victim->active = true;
  victim->last_activity = now;
  victim->state = ST_NEW_HANDLE;
  return victim;
}

bool BbsEngine::loadIntoSession(BbsSession& s, BbsUser& u) {
  if (!_st.loadUser(s.pubkey, u)) return false;
  u.handle[BBS_HANDLE_LEN - 1] = 0;
  snprintf(s.handle, BBS_HANDLE_LEN, "%s", u.handle);
  s.calls = u.calls;
  s.flags |= (u.flags & BBS_USER_SYSOP);
  return true;
}

bool BbsEngine::saveSessionUser(BbsSession& s, BbsUser& u) {
  u.flags = (u.flags & ~BBS_USER_SYSOP) | (s.flags & BBS_USER_SYSOP);
  return _st.saveUser(u);
}

int BbsEngine::unreadCount(const BbsUser& u, int area) {
  int total = _st.messageCount(area);
  int floor_seq = _st.firstSeq(area) - 1;
  int read = (int)u.last_read[area];
  if (read < floor_seq) read = floor_seq;
  return total > read ? total - read : 0;
}

bool BbsEngine::canSeeArea(const BbsSession& s, int area) const {
  if (area < 0 || area >= BBS_MAX_AREAS) return false;
  if (area == AREA_BULLETIN) return false;             // read via B only
  if (area == AREA_SYSOP) return (s.flags & BBS_USER_SYSOP) != 0;
  return true;
}

int BbsEngine::onlineCount(uint32_t now) {
  int n = 0;
  for (int i = 0; i < BBS_MAX_SESSIONS; i++) {
    const BbsSession& s = _sessions[i];
    if (s.active && s.state != ST_NEW_HANDLE && now - s.last_activity <= BBS_ONLINE_SECS) n++;
  }
  return n;
}

// ---------------- login ----------------

void BbsEngine::onLogin(const uint8_t* pubkey, bool is_admin, uint32_t now, BbsOutput& out, BbsAction& act) {
  Pager pg(out);
  act.clear();
  BbsSession* s = findSession(pubkey);
  // The app may re-send the login handshake (reconnects, path rediscovery). If the same user was active
  // moments ago, don't count a new call or announce it again; just refresh the banner.
  bool quick_relogin = (s != NULL && s->state != ST_NEW_HANDLE && now - s->last_activity < BBS_RELOGIN_SECS);
  if (s == NULL) s = allocSession(pubkey, now);
  s->last_activity = now;
  if (is_admin) s->flags |= BBS_USER_SYSOP;

  BbsUser u;
  if (loadIntoSession(*s, u)) {
    if (!quick_relogin) {
      u.calls++;
      s->calls = u.calls;
      saveSessionUser(*s, u);
    }
    s->state = ST_MAIN;
    s->area = AREA_GENERAL;
    showBanner(*s, u, pg);
    setEvent(*s, "logged in");
    if (_announce_logins && !quick_relogin) {
      act.broadcast = true;
      snprintf(act.broadcast_text, sizeof(act.broadcast_text), "* %s logged in (call #%lu)", s->handle, (unsigned long)u.calls);
    }
  } else {
    s->state = ST_NEW_HANDLE;
    pg.printf("Welcome to %s!\n", _name);
    pg.line("No account found for your key.");
    pg.line("Enter a handle (2-15 chars, letters/digits/_-):");
    setEvent(*s, "new user");
  }
  pg.flush();
}

void BbsEngine::showBanner(BbsSession& s, BbsUser& u, Pager& out) {
  out.printf("-= %s /X =- Welcome back, %s! Call #%lu\n", _name, s.handle, (unsigned long)u.calls);
  out.text("New: ");
  bool first = true;
  for (int a = 0; a < BBS_MAX_AREAS; a++) {
    if (!canSeeArea(s, a)) continue;
    out.printf("%s%s %d", first ? "" : ", ", BBS_AREAS[a].name, unreadCount(u, a));
    first = false;
  }
  int b = _st.messageCount(AREA_BULLETIN);
  if (b > 0) out.printf("\nBulletins: %d (B)", b);
  out.line("\n? for menu");
}

void BbsEngine::showMenu(BbsSession& s, Pager& out) {
  out.printf("-= %s /X =- [%s]\n", _name, BBS_AREAS[s.area].name);
  out.line("R)ead L)ist E)nter J)oin");
  out.line("W)ho S)tats B)ulletins C)omment");
  out.line("D)oors H)andle G)oodbye");
}

// ---------------- input ----------------

// A line is a command if it is "/x <anything>", a single character, or a letter followed by digits ("J2", "R 12").
// Everything else (words, sentences) is a message post, so casual chat still works in the room.
static bool looksLikeCommand(const char* line, char& cmd, const char*& arg) {
  const char* p = line;
  bool slash = false;
  if (*p == '/') { slash = true; p++; }
  if (*p == 0) return false;
  size_t n = strlen(p);
  bool ok = slash || n == 1;
  if (!ok && isalpha((unsigned char)p[0])) {
    ok = true;
    for (size_t i = 1; i < n; i++) {
      if (!(isdigit((unsigned char)p[i]) || p[i] == ' ' || p[i] == '*')) { ok = false; break; }
    }
    if (n > 4) ok = false;
  }
  if (!ok) return false;
  cmd = (char)toupper((unsigned char)p[0]);
  p++;
  while (*p == ' ') p++;
  arg = p;
  return true;
}

void BbsEngine::onInput(const uint8_t* pubkey, const char* raw, uint32_t now, BbsOutput& out, BbsAction& act) {
  Pager pg(out);
  act.clear();

  // trim
  char line[BBS_MSG_TEXT_LEN + 16];
  while (*raw == ' ' || *raw == '\t') raw++;
  snprintf(line, sizeof(line), "%s", raw);
  int n = (int)strlen(line);
  while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
  if (n == 0) { pg.flush(); return; }

  BbsSession* s = findSession(pubkey);
  BbsUser u;
  if (s == NULL) {
    // e.g. after a reboot: re-attach silently if we know the user
    s = allocSession(pubkey, now);
    if (loadIntoSession(*s, u)) { s->state = ST_MAIN; s->area = AREA_GENERAL; }
    else {
      pg.printf("Welcome to %s! Enter a handle (2-15 chars):", _name);
      pg.flush();
      return;
    }
  } else if (s->state != ST_NEW_HANDLE && !loadIntoSession(*s, u)) {
    s->state = ST_NEW_HANDLE;   // user record vanished (wiped) -> start over
  }
  s->last_activity = now;

  switch (s->state) {
    case ST_NEW_HANDLE:
      applyHandle(*s, u, line, true, now, pg);
      break;

    case ST_CHANGE_HANDLE:
      if (line[0] == '/') { s->state = ST_MAIN; pg.line("Handle unchanged."); break; }
      applyHandle(*s, u, line, false, now, pg);
      break;

    case ST_ENTER_MSG:
      s->state = ST_MAIN;
      if (line[0] == '/') { pg.line("Message aborted."); break; }
      postMessage(*s, u, s->area, line, now, pg, act);
      break;

    case ST_COMMENT:
      s->state = ST_MAIN;
      if (line[0] == '/') { pg.line("Comment aborted."); break; }
      postMessage(*s, u, AREA_SYSOP, line, now, pg, act);
      pg.line("Comment sent to sysop.");
      setEvent(*s, "comment to sysop");
      break;

    case ST_DOOR:
      if (NumberGuessDoor::input(s->door, line, s->handle, _st, pg)) s->state = ST_MAIN;
      break;

    case ST_MAIN:
    default: {
      char cmd; const char* arg;
      if (looksLikeCommand(line, cmd, arg)) handleCommand(*s, u, cmd, arg, now, pg, act);
      else postMessage(*s, u, s->area, line, now, pg, act);
      break;
    }
  }
  pg.flush();
}

bool BbsEngine::applyHandle(BbsSession& s, BbsUser& u, const char* line, bool is_new, uint32_t now, Pager& out) {
  if (!validHandle(line)) {
    out.line("Invalid handle. Use 2-15 letters, digits, _ or -. Try again:");
    return false;
  }
  BbsUser other;
  if (_st.loadUserByHandle(line, other) && memcmp(other.pubkey, s.pubkey, BBS_PUBKEY_LEN) != 0) {
    out.line("That handle is taken. Try another:");
    return false;
  }
  if (is_new) {
    memset(&u, 0, sizeof(u));
    memcpy(u.pubkey, s.pubkey, BBS_PUBKEY_LEN);
    u.calls = 1;
    u.created = now;
  }
  snprintf(u.handle, BBS_HANDLE_LEN, "%s", line);
  snprintf(s.handle, BBS_HANDLE_LEN, "%s", line);
  s.calls = u.calls;
  saveSessionUser(s, u);
  s.state = ST_MAIN;
  s.area = AREA_GENERAL;
  if (is_new) {
    out.printf("Welcome aboard, %s! You are user #%d.\n", s.handle, _st.userCount());
    showMenu(s, out);
    setEvent(s, "registered");
  } else {
    out.printf("Handle changed to %s.", s.handle);
  }
  return true;
}

// ---------------- commands ----------------

void BbsEngine::handleCommand(BbsSession& s, BbsUser& u, char cmd, const char* arg, uint32_t now, Pager& out, BbsAction& act) {
  switch (cmd) {
    case '?': case 'M':
      showMenu(s, out);
      break;
    case 'R': {
      int seq;
      if (arg[0] == '*') {   // R* = mark all read
        u.last_read[s.area] = _st.messageCount(s.area);
        saveSessionUser(s, u);
        out.printf("All messages in %s marked read.", BBS_AREAS[s.area].name);
        break;
      }
      if (arg[0]) seq = atoi(arg);
      else {
        int floor_seq = _st.firstSeq(s.area) - 1;
        int read = (int)u.last_read[s.area];
        if (read < floor_seq) read = floor_seq;
        seq = read + 1;
      }
      readMessage(s, u, seq, out);
      break;
    }
    case 'N':
      readMessage(s, u, (int)u.last_read[s.area] + 1, out);
      break;
    case 'L':
      listMessages(s, u, arg[0] ? atoi(arg) : 5, out);
      break;
    case 'E':
      s.state = ST_ENTER_MSG;
      out.printf("Enter your message for %s (max 150 chars), or /Q to abort:", BBS_AREAS[s.area].name);
      break;
    case 'J':
      joinArea(s, u, arg, out);
      break;
    case 'W':
      showWho(now, out);
      break;
    case 'S':
      showStats(now, out);
      break;
    case 'B':
      showBulletins(out);
      break;
    case 'C':
      s.state = ST_COMMENT;
      out.line("Comment to sysop (max 150 chars), or /Q to abort:");
      break;
    case 'D':
      if (arg[0] == '1') {
        s.state = ST_DOOR;
        NumberGuessDoor::start(s.door, rnd() ^ now, out);
        setEvent(s, "playing Number Guess");
      } else if (arg[0] == '2') {
        NumberGuessDoor::formatHighscores(_st, out);
      } else {
        showDoors(out);
      }
      break;
    case 'H':
      s.state = ST_CHANGE_HANDLE;
      out.printf("Current handle: %s. Enter new handle, or /Q to keep:", s.handle);
      break;
    case 'G':
      out.printf("Goodbye %s! Thanks for calling %s.", s.handle, _name);
      setEvent(s, "logged off");
      s.active = false;
      act.logout = true;
      break;
    default:
      out.printf("Unknown command '%c'. ? for menu.", cmd);
      break;
  }
}

void BbsEngine::postMessage(BbsSession& s, BbsUser& u, int area, const char* text, uint32_t now, Pager& out, BbsAction& act) {
  BbsMessage m;
  memset(&m, 0, sizeof(m));
  m.ts = now;
  snprintf(m.handle, BBS_HANDLE_LEN, "%s", s.handle);
  int n = Pager::utf8SafeCut(text, BBS_MSG_TEXT_LEN - 1);
  memcpy(m.text, text, n);
  m.text[n] = 0;
  if (!_st.appendMessage(area, m)) { out.line("Storage error, message not saved."); return; }
  int seq = _st.messageCount(area);
  if (area == s.area) {
    if (u.last_read[area] == (uint32_t)(seq - 1)) u.last_read[area] = seq;   // don't show us our own post as unread
    saveSessionUser(s, u);
  }
  if (area != AREA_SYSOP) {
    out.printf("Posted #%d in %s.", seq, BBS_AREAS[area].name);
    setEvent(s, "posted");
    act.broadcast = true;
    char prefix[24];
    if (area == AREA_GENERAL) prefix[0] = 0; else snprintf(prefix, sizeof(prefix), "[%s] ", BBS_AREAS[area].name);
    char tmp[BBS_MSG_TEXT_LEN + 48];
    snprintf(tmp, sizeof(tmp), "%s%s: %s", prefix, s.handle, m.text);
    int cut = Pager::utf8SafeCut(tmp, BBS_MSG_TEXT_LEN - 1);
    memcpy(act.broadcast_text, tmp, cut);
    act.broadcast_text[cut] = 0;
  }
}

void BbsEngine::readMessage(BbsSession& s, BbsUser& u, int seq, Pager& out) {
  BbsMessage m;
  int total = _st.messageCount(s.area);
  if (!_st.readMessage(s.area, seq, m)) {
    if (total == 0) out.printf("%s is empty. E to post the first message!", BBS_AREAS[s.area].name);
    else out.printf("No new messages in %s (%d total, first #%d). R <n> to read one, L to list.",
                    BBS_AREAS[s.area].name, total, _st.firstSeq(s.area));
    return;
  }
  char when[16];
  formatTime(m.ts, when, sizeof(when));
  out.printf("%s #%d/%d %s %s\n%s", BBS_AREAS[s.area].name, seq, total, m.handle, when, m.text);
  if ((uint32_t)seq > u.last_read[s.area]) { u.last_read[s.area] = seq; saveSessionUser(s, u); }
  setEvent(s, "reading");
}

void BbsEngine::listMessages(BbsSession& s, BbsUser& u, int count, Pager& out) {
  (void)u;
  int total = _st.messageCount(s.area);
  if (total == 0) { out.printf("%s is empty.", BBS_AREAS[s.area].name); return; }
  if (count < 1) count = 5;
  if (count > 8) count = 8;
  int first = _st.firstSeq(s.area);
  int from = total - count + 1;
  if (from < first) from = first;
  out.printf("%s #%d-%d of %d:\n", BBS_AREAS[s.area].name, from, total, total);
  BbsMessage m;
  for (int seq = from; seq <= total; seq++) {
    if (!_st.readMessage(s.area, seq, m)) continue;
    char snippet[28];
    int cut = Pager::utf8SafeCut(m.text, 24);
    memcpy(snippet, m.text, cut); snippet[cut] = 0;
    for (char* p = snippet; *p; p++) if (*p == '\n') *p = ' ';
    out.printf("#%d %s: %s%s\n", seq, m.handle, snippet, (int)strlen(m.text) > cut ? ".." : "");
  }
}

void BbsEngine::joinArea(BbsSession& s, BbsUser& u, const char* arg, Pager& out) {
  if (arg[0] == 0) {
    out.line("Areas:");
    for (int a = 0; a < BBS_MAX_AREAS; a++) {
      if (!canSeeArea(s, a)) continue;
      out.printf("%d) %s (%d msgs, %d new)%s\n", a + 1, BBS_AREAS[a].name, _st.messageCount(a), unreadCount(u, a), a == s.area ? " *" : "");
    }
    out.text("J <n> to join.");
    return;
  }
  int a = areaByName(arg);
  if (!canSeeArea(s, a)) { out.line("No such area. J to list."); return; }
  s.area = (uint8_t)a;
  out.printf("Joined %s (%d msgs, %d new). R to read.", BBS_AREAS[a].name, _st.messageCount(a), unreadCount(u, a));
  setEvent(s, BBS_AREAS[a].name);
}

void BbsEngine::showWho(uint32_t now, Pager& out) {
  int n = onlineCount(now);
  out.printf("Online now (%d):", n);
  for (int i = 0; i < BBS_MAX_SESSIONS; i++) {
    const BbsSession& s = _sessions[i];
    if (s.active && s.state != ST_NEW_HANDLE && now - s.last_activity <= BBS_ONLINE_SECS) {
      out.printf(" %s%s", s.handle, (s.flags & BBS_USER_SYSOP) ? "(sysop)" : "");
    }
  }
}

void BbsEngine::showStats(uint32_t now, Pager& out) {
  uint32_t calls = 0;
  int users = _st.userCount();
  BbsUser u;
  for (int i = 0; i < users; i++) if (_st.getUserByIndex(i, u)) calls += u.calls;
  uint32_t up = now > _boot_ts ? now - _boot_ts : 0;
  out.printf("%s stats\nUsers: %d  Calls: %lu  Online: %d\nMsgs: ", _name, users, (unsigned long)calls, onlineCount(now));
  bool first = true;
  for (int a = 0; a < BBS_MAX_AREAS; a++) {
    if (BBS_AREAS[a].hidden) continue;
    out.printf("%s%s %d", first ? "" : ", ", BBS_AREAS[a].name, _st.messageCount(a));
    first = false;
  }
  out.printf("\nUp: %lud %luh %lum", (unsigned long)(up / 86400), (unsigned long)((up / 3600) % 24), (unsigned long)((up / 60) % 60));
}

void BbsEngine::showBulletins(Pager& out) {
  int total = _st.messageCount(AREA_BULLETIN);
  if (total == 0) { out.line("No bulletins."); return; }
  BbsMessage m;
  int shown = 0;
  for (int seq = total; seq >= _st.firstSeq(AREA_BULLETIN) && shown < 3; seq--) {
    if (!_st.readMessage(AREA_BULLETIN, seq, m)) continue;
    char when[16]; formatTime(m.ts, when, sizeof(when));
    out.printf("[%s] %s\n", when, m.text);
    shown++;
  }
}

void BbsEngine::showDoors(Pager& out) {
  out.line("Doors:");
  out.line("1) Number Guess");
  out.line("2) High scores");
  out.line("D <n> to enter.");
}

// ---------------- sysop API ----------------

void BbsEngine::formatStats(char* buf, size_t len, uint32_t now) {
  BbsOutput o; Pager pg(o);
  showStats(now, pg);
  pg.flush();
  snprintf(buf, len, "%s", o.count ? o.pages[0] : "");
}

void BbsEngine::formatUsers(char* buf, size_t len, int page) {
  const int per = 8;
  int n = _st.userCount();
  int from = page * per;
  int pos = snprintf(buf, len, "Users %d-%d of %d:", from + 1, from + per > n ? n : from + per, n);
  BbsUser u;
  for (int i = from; i < n && i < from + per && pos < (int)len; i++) {
    if (!_st.getUserByIndex(i, u)) continue;
    pos += snprintf(buf + pos, len - pos, " %s(%lu)%s", u.handle, (unsigned long)u.calls, (u.flags & BBS_USER_SYSOP) ? "*" : "");
  }
}

bool BbsEngine::systemPost(int area, const char* text, uint32_t now, BbsAction& act) {
  act.clear();
  if (area < 0 || area >= BBS_MAX_AREAS || text[0] == 0) return false;
  BbsMessage m;
  memset(&m, 0, sizeof(m));
  m.ts = now;
  snprintf(m.handle, BBS_HANDLE_LEN, "SYSOP");
  int n = Pager::utf8SafeCut(text, BBS_MSG_TEXT_LEN - 1);
  memcpy(m.text, text, n);
  if (!_st.appendMessage(area, m)) return false;
  if (area != AREA_SYSOP) {
    act.broadcast = true;
    char tmp[BBS_MSG_TEXT_LEN + 32];
    snprintf(tmp, sizeof(tmp), "%s%s", area == AREA_BULLETIN ? "* BULLETIN: " : "SYSOP: ", m.text);
    int cut = Pager::utf8SafeCut(tmp, BBS_MSG_TEXT_LEN - 1);
    memcpy(act.broadcast_text, tmp, cut);
    act.broadcast_text[cut] = 0;
  }
  snprintf(_last_event, sizeof(_last_event), "SYSOP: post to %s", BBS_AREAS[area].name);
  return true;
}

bool BbsEngine::kick(const char* handle) {
  for (int i = 0; i < BBS_MAX_SESSIONS; i++) {
    if (_sessions[i].active && strcasecmp(_sessions[i].handle, handle) == 0) { _sessions[i].active = false; return true; }
  }
  return false;
}
