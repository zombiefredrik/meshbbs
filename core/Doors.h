#pragma once
#include "BbsTypes.h"
#include "BbsStorage.h"
#include "Pager.h"

struct DoorState {
  uint8_t id;        // 0 = none
  uint8_t attempts;
  uint8_t target;
  uint8_t pad;
};

enum { DOOR_NONE = 0, DOOR_NUMBER_GUESS = 1 };

class NumberGuessDoor {
public:
  static void start(DoorState& d, uint32_t rnd, Pager& out);
  // Returns true when the door is finished and the user should return to the main menu.
  static bool input(DoorState& d, const char* line, const char* handle, BbsStorage& st, Pager& out);
  static void formatHighscores(BbsStorage& st, Pager& out);
private:
  static void recordScore(BbsStorage& st, const char* handle, uint32_t attempts, Pager& out);
};
