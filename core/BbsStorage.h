#pragma once
// Fixed-record storage for users, messages and highscores on top of BbsFs.
#include "BbsTypes.h"
#include "BbsFs.h"

class BbsStorage {
public:
  explicit BbsStorage(BbsFs& fs, const char* root = "/bbs");

  // ---- users ----
  bool loadUser(const uint8_t* pubkey, BbsUser& out);
  bool loadUserByHandle(const char* handle, BbsUser& out);
  bool saveUser(const BbsUser& u);          // insert or update (by pubkey)
  int  userCount();
  bool getUserByIndex(int idx, BbsUser& out);

  // ---- messages ----
  // Messages are numbered by seq 1..N. Old ones expire on rotation: firstSeq() is the lowest still readable.
  int  messageCount(int area);              // == highest seq
  int  firstSeq(int area);
  bool appendMessage(int area, const BbsMessage& m);
  bool readMessage(int area, int seq, BbsMessage& out);
  bool wipeArea(int area);

  // ---- highscores ----
  int  loadHighscores(BbsHighscore* out, int max);
  bool saveHighscores(const BbsHighscore* in, int n);

private:
  bool rotateArea(int area);
  void areaPath(int area, char* buf, size_t len, const char* suffix = "");
  int  findUserIndex(const uint8_t* pubkey);

  BbsFs& _fs;
  char _root[24];
  char _users_path[40];
  char _hs_path[40];
};
