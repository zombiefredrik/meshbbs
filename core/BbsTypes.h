#pragma once
// MeshBBS core types. Portable C++17, no Arduino dependencies.
#include <stdint.h>
#include <string.h>

#define BBS_PAGE_MAX        150   // max bytes of text per page (one LoRa datagram to the phone)
#define BBS_MAX_PAGES       3     // max pages emitted per command
#define BBS_HANDLE_LEN      16    // 15 chars + NUL
#define BBS_MSG_TEXT_LEN    151   // 150 chars + NUL
#define BBS_MAX_AREAS       5
#define BBS_MAX_SESSIONS    20
#define BBS_AREA_CAP        400   // messages kept per area before rotation
#define BBS_ONLINE_SECS     (15*60)
#define BBS_RELOGIN_SECS    (10*60)  // re-login within this window is not a new call
#define BBS_MAX_HIGHSCORES  5
#define BBS_PUBKEY_LEN      32

#define BBS_USER_SYSOP      0x01

// Area indexes
enum { AREA_GENERAL = 0, AREA_TECH = 1, AREA_MARKET = 2, AREA_SYSOP = 3, AREA_BULLETIN = 4 };

struct BbsUser {
  uint8_t  pubkey[BBS_PUBKEY_LEN];
  char     handle[BBS_HANDLE_LEN];
  uint32_t calls;
  uint32_t created;
  uint32_t last_read[BBS_MAX_AREAS];   // message seq (1-based count) read so far, per area
  uint8_t  flags;
  uint8_t  pad[3];
};

struct BbsMessage {
  uint32_t ts;
  char     handle[BBS_HANDLE_LEN];
  char     text[BBS_MSG_TEXT_LEN];
  uint8_t  pad;   // -> 172 bytes
};

struct BbsHighscore {
  char     handle[BBS_HANDLE_LEN];
  uint32_t score;   // number of guesses (lower is better); 0 = empty slot
};

// Pages of output to send back to ONE client, in order.
struct BbsOutput {
  char pages[BBS_MAX_PAGES][BBS_PAGE_MAX + 1];
  int  count;
  void clear() { count = 0; pages[0][0] = 0; }
};

struct BbsAreaInfo {
  const char* name;
  bool hidden;       // not listed / not joinable by normal users
};

static const BbsAreaInfo BBS_AREAS[BBS_MAX_AREAS] = {
  { "GENERAL",  false },
  { "TECH",     false },
  { "MARKET",   false },
  { "SYSOP",    true  },
  { "BULLETIN", true  },
};
