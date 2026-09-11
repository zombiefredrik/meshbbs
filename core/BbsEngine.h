#pragma once
// The BBS state machine. Platform-agnostic: the host (firmware or REPL) feeds it login events
// and text lines per user, and sends back the pages it produces.
#include "BbsTypes.h"
#include "BbsStorage.h"
#include "Pager.h"
#include "Doors.h"

enum BbsState : uint8_t {
  ST_NEW_HANDLE = 0,   // first login: waiting for a handle
  ST_MAIN,             // main menu, accepting commands / posts
  ST_ENTER_MSG,        // waiting for message body
  ST_COMMENT,          // waiting for comment-to-sysop body
  ST_CHANGE_HANDLE,    // waiting for new handle
  ST_DOOR,             // inside a door game
};

struct BbsSession {
  uint8_t   pubkey[BBS_PUBKEY_LEN];
  bool      active;
  uint8_t   state;
  uint8_t   area;
  uint8_t   flags;           // BBS_USER_SYSOP
  uint32_t  last_activity;
  uint32_t  calls;
  char      handle[BBS_HANDLE_LEN];
  DoorState door;
};

// Side effects the host must carry out after a call.
struct BbsAction {
  bool broadcast;                            // add broadcast_text as a room post to everyone
  char broadcast_text[BBS_MSG_TEXT_LEN];
  bool logout;                               // the user said goodbye
  void clear() { broadcast = false; broadcast_text[0] = 0; logout = false; }
};

class BbsEngine {
public:
  BbsEngine(BbsStorage& storage, const char* bbs_name, uint32_t boot_ts);

  // A client completed the room login handshake.
  void onLogin(const uint8_t* pubkey, bool is_admin, uint32_t now, BbsOutput& out, BbsAction& act);
  // A logged-in client sent a line of text.
  void onInput(const uint8_t* pubkey, const char* line, uint32_t now, BbsOutput& out, BbsAction& act);

  int  onlineCount(uint32_t now);
  const char* lastEvent() const { return _last_event; }
  void setAnnounceLogins(bool on) { _announce_logins = on; }

  // ---- sysop API (serial / admin CLI) ----
  void formatStats(char* buf, size_t len, uint32_t now);
  void formatUsers(char* buf, size_t len, int page);
  bool systemPost(int area, const char* text, uint32_t now, BbsAction& act);
  bool kick(const char* handle);
  bool wipeArea(int area) { return _st.wipeArea(area); }
  static int areaByName(const char* name);

  static void formatTime(uint32_t ts, char* buf, size_t len);   // "MM-DD HH:MM" UTC
  static bool validHandle(const char* h);

private:
  BbsSession* findSession(const uint8_t* pubkey);
  BbsSession* allocSession(const uint8_t* pubkey, uint32_t now);
  bool loadIntoSession(BbsSession& s, BbsUser& u);
  bool saveSessionUser(BbsSession& s, BbsUser& u);
  void setEvent(const BbsSession& s, const char* what);
  uint32_t rnd();

  void showBanner(BbsSession& s, BbsUser& u, Pager& out);
  void showMenu(BbsSession& s, Pager& out);
  void handleCommand(BbsSession& s, BbsUser& u, char cmd, const char* arg, uint32_t now, Pager& out, BbsAction& act);
  void postMessage(BbsSession& s, BbsUser& u, int area, const char* text, uint32_t now, Pager& out, BbsAction& act);
  void readMessage(BbsSession& s, BbsUser& u, int seq, Pager& out);
  void listMessages(BbsSession& s, BbsUser& u, int count, Pager& out);
  void joinArea(BbsSession& s, BbsUser& u, const char* arg, Pager& out);
  void showWho(uint32_t now, Pager& out);
  void showStats(uint32_t now, Pager& out);
  void showBulletins(Pager& out);
  void showDoors(Pager& out);
  int  unreadCount(const BbsUser& u, int area);
  bool canSeeArea(const BbsSession& s, int area) const;
  bool applyHandle(BbsSession& s, BbsUser& u, const char* line, bool is_new, uint32_t now, Pager& out);

  BbsStorage& _st;
  char _name[24];
  uint32_t _boot_ts;
  uint32_t _rng;
  bool _announce_logins;
  char _last_event[40];
  BbsSession _sessions[BBS_MAX_SESSIONS];
};
