// Scenario tests for the BBS core. Run via `make test` (fresh ./testdata each run).
#include "BbsEngine.h"
#include "StdioFs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_CONTAINS(out, needle) do { bool f = false; for (int _i = 0; _i < (out).count; _i++) if (strstr((out).pages[_i], needle)) f = true; \
  if (f) g_pass++; else { g_fail++; printf("FAIL %s:%d: output lacks \"%s\"; got: %s\n", __FILE__, __LINE__, needle, (out).count ? (out).pages[0] : "(empty)"); } } while (0)

static void key(int id, uint8_t* k) { memset(k, 0, BBS_PUBKEY_LEN); k[0] = (uint8_t)id; k[1] = 0xAB; }

static void checkPages(const BbsOutput& out) {
  CHECK(out.count >= 0 && out.count <= BBS_MAX_PAGES);
  for (int i = 0; i < out.count; i++) {
    int n = (int)strlen(out.pages[i]);
    CHECK(n <= BBS_PAGE_MAX);
    // must not end inside a UTF-8 sequence: count continuation bytes at the end vs. lead byte
    if (n > 0) {
      int j = n - 1, cont = 0;
      while (j >= 0 && ((uint8_t)out.pages[i][j] & 0xC0) == 0x80) { cont++; j--; }
      if (j >= 0 && cont > 0) {
        uint8_t lead = (uint8_t)out.pages[i][j];
        int need = (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : (lead & 0xF8) == 0xF0 ? 3 : 0;
        CHECK(need == cont);
      }
    }
  }
}

struct World {
  StdioFs fs;
  BbsStorage st;
  BbsEngine bbs;
  BbsOutput out;
  BbsAction act;
  uint32_t now;
  World() : fs("./testdata"), st(fs), bbs(st, "TestBBS", 1700000000u), now(1700000100u) {}
  void login(int id, bool admin = false) { uint8_t k[32]; key(id, k); bbs.onLogin(k, admin, now, out, act); checkPages(out); }
  void in(int id, const char* line) { uint8_t k[32]; key(id, k); bbs.onInput(k, line, now, out, act); checkPages(out); }
};

static void testPager() {
  BbsOutput o;
  { Pager p(o); p.text("short"); p.flush(); CHECK(o.count == 1); CHECK(strcmp(o.pages[0], "short") == 0); }
  { Pager p(o); p.flush(); CHECK(o.count == 0); }
  {
    Pager p(o);
    for (int i = 0; i < 12; i++) p.line("The quick brown fox jumps over the lazy dog.");
    p.flush();
    CHECK(o.count == 3);
    for (int i = 0; i < o.count; i++) CHECK((int)strlen(o.pages[i]) <= BBS_PAGE_MAX);
    CHECK(strstr(o.pages[0], "(1/3)") != NULL);
    CHECK(strstr(o.pages[2], "...") != NULL);   // 12 lines don't fit in 3 pages
  }
  {
    // UTF-8: Swedish text with åäö must never be split mid-character
    Pager p(o);
    for (int i = 0; i < 8; i++) p.text("Räksmörgås på Åre är gött åäöÅÄÖ ");
    p.flush();
    CHECK(o.count >= 2);
    checkPages(o);
  }
  CHECK(Pager::utf8SafeCut("abc", 10) == 3);
  CHECK(Pager::utf8SafeCut("ab\xC3\xA5", 3) == 2);   // don't cut inside å
  CHECK(Pager::utf8SafeCut("ab\xC3\xA5", 4) == 4);
}

static void testTime() {
  char b[16];
  BbsEngine::formatTime(1700000100u, b, sizeof(b));   // 2023-11-14 22:15:00 UTC
  CHECK(strcmp(b, "11-14 22:15") == 0);
  CHECK(BbsEngine::validHandle("Zombie"));
  CHECK(BbsEngine::validHandle("a-b_c1"));
  CHECK(!BbsEngine::validHandle("x"));
  CHECK(!BbsEngine::validHandle("has space"));
  CHECK(!BbsEngine::validHandle("waytoolonghandle1"));
  CHECK(BbsEngine::areaByName("tech") == AREA_TECH);
  CHECK(BbsEngine::areaByName("2") == AREA_TECH);
  CHECK(BbsEngine::areaByName("nope") == -1);
}

static void testNewUserFlow(World& w) {
  w.login(1);
  CHECK_CONTAINS(w.out, "Enter a handle");
  CHECK(!w.act.broadcast);
  w.in(1, "x");
  CHECK_CONTAINS(w.out, "Invalid handle");
  w.in(1, "Zombie");
  CHECK_CONTAINS(w.out, "Welcome aboard, Zombie");
  CHECK_CONTAINS(w.out, "R)ead");
  BbsUser u; uint8_t k[32]; key(1, k);
  CHECK(w.st.loadUser(k, u));
  CHECK(strcmp(u.handle, "Zombie") == 0);
  CHECK(u.calls == 1);

  // second user can't take the same handle
  w.login(2);
  w.in(2, "zombie");
  CHECK_CONTAINS(w.out, "taken");
  w.in(2, "Kalle");
  CHECK_CONTAINS(w.out, "Welcome aboard, Kalle");
  CHECK(w.st.userCount() == 2);
}

static void testPostAndRead(World& w) {
  w.in(1, "Hello mesh, first post!");
  CHECK_CONTAINS(w.out, "Posted #1 in GENERAL");
  CHECK(w.act.broadcast);
  CHECK(strcmp(w.act.broadcast_text, "Zombie: Hello mesh, first post!") == 0);

  w.in(1, "R");
  CHECK_CONTAINS(w.out, "No new messages");   // own post is not unread

  w.in(2, "R");
  CHECK_CONTAINS(w.out, "GENERAL #1/1 Zombie");
  CHECK_CONTAINS(w.out, "Hello mesh, first post!");
  w.in(2, "R");
  CHECK_CONTAINS(w.out, "No new messages");

  // two-letter words are posts, not commands
  w.in(2, "hi");
  CHECK_CONTAINS(w.out, "Posted #2");
  w.in(2, "ok");
  CHECK_CONTAINS(w.out, "Posted #3");

  // E flow
  w.in(1, "E");
  CHECK_CONTAINS(w.out, "Enter your message");
  w.in(1, "/q");
  CHECK_CONTAINS(w.out, "aborted");
  w.in(1, "E");
  w.in(1, "Entered via E");
  CHECK_CONTAINS(w.out, "Posted #4");

  w.in(1, "L");
  CHECK_CONTAINS(w.out, "#2 Kalle: hi");
  CHECK_CONTAINS(w.out, "#4 Zombie: Entered via E");

  w.in(1, "R 2");
  CHECK_CONTAINS(w.out, "GENERAL #2/4 Kalle");
  w.in(1, "R*");
  CHECK_CONTAINS(w.out, "marked read");

  // a 150-char post survives intact and is readable
  char big[200];
  memset(big, 'x', 150); big[150] = 0;
  w.in(2, big);
  CHECK_CONTAINS(w.out, "Posted #5");
  w.in(1, "R 5");
  CHECK(w.out.count == 2);   // header + 150 chars needs two pages
  // full text present across pages
  char joined[400] = "";
  for (int i = 0; i < w.out.count; i++) strcat(joined, w.out.pages[i]);
  int xs = 0; for (char* p = joined; *p; p++) if (*p == 'x') xs++;
  CHECK(xs == 150);
}

static void testAreas(World& w) {
  w.in(1, "J");
  CHECK_CONTAINS(w.out, "1) GENERAL");
  CHECK_CONTAINS(w.out, "3) MARKET");
  CHECK(strstr(w.out.pages[0], "SYSOP") == NULL);   // hidden for non-sysop
  w.in(1, "J 2");
  CHECK_CONTAINS(w.out, "Joined TECH");
  w.in(1, "Anyone running Heltec v3?");
  CHECK(strcmp(w.act.broadcast_text, "[TECH] Zombie: Anyone running Heltec v3?") == 0);
  w.in(1, "J 4");
  CHECK_CONTAINS(w.out, "No such area");
  w.in(1, "/J general");
  CHECK_CONTAINS(w.out, "Joined GENERAL");
}

static void testCommentSysopBulletins(World& w) {
  w.in(2, "C");
  CHECK_CONTAINS(w.out, "Comment to sysop");
  w.in(2, "Nice board!");
  CHECK_CONTAINS(w.out, "Comment sent");
  CHECK(!w.act.broadcast);
  CHECK(w.st.messageCount(AREA_SYSOP) == 1);

  // sysop logs in, sees SYSOP area
  w.login(3, true);
  w.in(3, "Sysop");
  w.in(3, "J");
  CHECK_CONTAINS(w.out, "4) SYSOP");
  w.in(3, "J 4");
  w.in(3, "R");
  CHECK_CONTAINS(w.out, "Kalle");
  CHECK_CONTAINS(w.out, "Nice board!");

  w.in(1, "B");
  CHECK_CONTAINS(w.out, "No bulletins");
  CHECK(w.bbs.systemPost(AREA_BULLETIN, "Board maintenance Sunday", w.now, w.act));
  CHECK(w.act.broadcast);
  CHECK_CONTAINS(w.out, "No bulletins");   // out untouched by systemPost
  CHECK(strcmp(w.act.broadcast_text, "* BULLETIN: Board maintenance Sunday") == 0);
  w.in(1, "B");
  CHECK_CONTAINS(w.out, "Board maintenance Sunday");

  char buf[200];
  w.bbs.formatStats(buf, sizeof(buf), w.now);
  CHECK(strstr(buf, "Users: 3") != NULL);
  w.bbs.formatUsers(buf, sizeof(buf), 0);
  CHECK(strstr(buf, "Zombie(1)") != NULL);
  CHECK(strstr(buf, "Sysop(1)*") != NULL);
}

static void testWhoStatsGoodbye(World& w) {
  w.in(1, "W");
  CHECK_CONTAINS(w.out, "Online now (3)");
  CHECK_CONTAINS(w.out, "Sysop(sysop)");
  w.in(1, "S");
  CHECK_CONTAINS(w.out, "TestBBS stats");
  CHECK_CONTAINS(w.out, "Users: 3");
  w.in(2, "G");
  CHECK_CONTAINS(w.out, "Goodbye Kalle");
  CHECK(w.act.logout);
  w.in(1, "W");
  CHECK_CONTAINS(w.out, "Online now (2)");
  // idle users drop off the who list
  w.now += BBS_ONLINE_SECS + 1;
  w.in(1, "W");
  CHECK_CONTAINS(w.out, "Online now (1)");

  // returning user gets the banner with call count + unread
  w.login(2);
  CHECK_CONTAINS(w.out, "Welcome back, Kalle! Call #2");
  CHECK_CONTAINS(w.out, "Bulletins: 1");
  CHECK(w.act.broadcast);
  CHECK(strstr(w.act.broadcast_text, "Kalle logged in") != NULL);
  // immediate re-login (app reconnect) is quiet: no announcement, same call number
  w.login(2);
  CHECK_CONTAINS(w.out, "Call #2");
  CHECK(!w.act.broadcast);
  w.in(2, "?");
  CHECK_CONTAINS(w.out, "-= TestBBS =- [GENERAL]");
  w.in(2, "Z");
  CHECK_CONTAINS(w.out, "Unknown command 'Z'");
  w.in(2, "H");
  w.in(2, "Kalle-2");
  CHECK_CONTAINS(w.out, "Handle changed to Kalle-2");
}

static void testDoor(World& w) {
  w.in(1, "D");
  CHECK_CONTAINS(w.out, "1) Number Guess");
  w.in(1, "D 1");
  CHECK_CONTAINS(w.out, "NUMBER GUESS");
  // binary search always wins within 7 guesses
  int lo = 1, hi = 100, guesses = 0;
  bool won = false;
  for (int i = 0; i < 10 && !won; i++) {
    int g = (lo + hi) / 2;
    char s[8]; snprintf(s, sizeof(s), "%d", g);
    w.in(1, s); guesses++;
    if (strstr(w.out.pages[0], "too low")) lo = g + 1;
    else if (strstr(w.out.pages[0], "too high")) hi = g - 1;
    else if (strstr(w.out.pages[0], "RIGHT")) won = true;
  }
  CHECK(won);
  CHECK(guesses <= 7);
  CHECK_CONTAINS(w.out, "high score");
  w.in(1, "D 2");
  CHECK_CONTAINS(w.out, "1. Zombie");
  w.in(1, "D 1");
  w.in(1, "/q");
  CHECK_CONTAINS(w.out, "Chicken");
  w.in(1, "?");
  CHECK_CONTAINS(w.out, "R)ead");
}

static void testRotation(World& w) {
  // fill TECH beyond the cap and make sure old messages expire but the newest stay readable
  BbsMessage m; memset(&m, 0, sizeof(m));
  snprintf(m.handle, BBS_HANDLE_LEN, "Bot");
  int before = w.st.messageCount(AREA_TECH);
  for (int i = 0; i < BBS_AREA_CAP + 10 - before; i++) {
    m.ts = w.now + i;
    snprintf(m.text, BBS_MSG_TEXT_LEN, "msg %d", before + i + 1);
    CHECK(w.st.appendMessage(AREA_TECH, m));
  }
  int total = w.st.messageCount(AREA_TECH);
  CHECK(total == BBS_AREA_CAP + 10);
  CHECK(w.st.firstSeq(AREA_TECH) > 1);
  CHECK(w.st.firstSeq(AREA_TECH) <= total - BBS_AREA_CAP / 2 + 1);
  CHECK(!w.st.readMessage(AREA_TECH, 1, m));
  CHECK(w.st.readMessage(AREA_TECH, total, m));
  CHECK(strcmp(m.text, "msg 410") == 0);
  w.in(1, "J 2");
  w.in(1, "R");   // user's last_read is far below firstSeq -> should read the oldest surviving message
  char expect[32]; snprintf(expect, sizeof(expect), "#%d/", w.st.firstSeq(AREA_TECH));
  CHECK_CONTAINS(w.out, expect);
}

static void testRebootReattach() {
  // A fresh engine (simulating reboot) with the same storage: known user re-attaches silently on input
  StdioFs fs("./testdata");
  BbsStorage st(fs);
  BbsEngine bbs(st, "TestBBS", 1700009000u);
  BbsOutput out; BbsAction act;
  uint8_t k[32]; key(1, k);
  bbs.onInput(k, "S", 1700009100u, out, act);
  checkPages(out);
  CHECK_CONTAINS(out, "TestBBS stats");
  bbs.onInput(k, "still here after reboot", 1700009101u, out, act);
  CHECK(act.broadcast);
  CHECK(strstr(act.broadcast_text, "Zombie: still here") != NULL);
}

int main() {
  testPager();
  testTime();
  {
    World w;
    testNewUserFlow(w);
    testPostAndRead(w);
    testAreas(w);
    testCommentSysopBulletins(w);
    testWhoStatsGoodbye(w);
    testDoor(w);
    testRotation(w);
  }
  testRebootReattach();
  printf("%d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
