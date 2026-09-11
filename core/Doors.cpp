#include "Doors.h"
#include <stdio.h>
#include <stdlib.h>

void NumberGuessDoor::start(DoorState& d, uint32_t rnd, Pager& out) {
  d.id = DOOR_NUMBER_GUESS;
  d.attempts = 0;
  d.target = (uint8_t)(rnd % 100 + 1);
  out.line("*** NUMBER GUESS ***");
  out.line("I'm thinking of a number from 1 to 100.");
  out.line("Fewest guesses wins. /Q quits. Your guess?");
}

bool NumberGuessDoor::input(DoorState& d, const char* line, const char* handle, BbsStorage& st, Pager& out) {
  if (line[0] == '/' || line[0] == 'q' || line[0] == 'Q') {
    out.printf("Chicken! The number was %d. Back to main menu.", d.target);
    d.id = DOOR_NONE;
    return true;
  }
  int g = atoi(line);
  if (g < 1 || g > 100) {
    out.line("Enter a number 1-100, or /Q to quit.");
    return false;
  }
  d.attempts++;
  if (g < d.target) {
    out.printf("%d is too low. Guess #%d:", g, d.attempts + 1);
    return false;
  }
  if (g > d.target) {
    out.printf("%d is too high. Guess #%d:", g, d.attempts + 1);
    return false;
  }
  out.printf("%d is RIGHT! You got it in %d guesses.\n", g, d.attempts);
  recordScore(st, handle, d.attempts, out);
  d.id = DOOR_NONE;
  return true;
}

void NumberGuessDoor::recordScore(BbsStorage& st, const char* handle, uint32_t attempts, Pager& out) {
  BbsHighscore hs[BBS_MAX_HIGHSCORES + 1];
  int n = st.loadHighscores(hs, BBS_MAX_HIGHSCORES);
  // insert sorted (lower attempts first; ties keep older entries ahead)
  int pos = n;
  for (int i = 0; i < n; i++) { if (attempts < hs[i].score) { pos = i; break; } }
  if (pos >= BBS_MAX_HIGHSCORES) { out.line("Not a top-5 score this time."); return; }
  for (int i = n; i > pos; i--) hs[i] = hs[i - 1];
  memset(&hs[pos], 0, sizeof(hs[pos]));
  snprintf(hs[pos].handle, BBS_HANDLE_LEN, "%s", handle);
  hs[pos].score = attempts;
  if (n < BBS_MAX_HIGHSCORES) n++;
  st.saveHighscores(hs, n);
  out.printf("New high score! You are #%d.", pos + 1);
}

void NumberGuessDoor::formatHighscores(BbsStorage& st, Pager& out) {
  BbsHighscore hs[BBS_MAX_HIGHSCORES];
  int n = st.loadHighscores(hs, BBS_MAX_HIGHSCORES);
  if (n == 0) { out.line("No high scores yet."); return; }
  out.line("Number Guess top list:");
  for (int i = 0; i < n; i++) out.printf("%d. %s (%lu)\n", i + 1, hs[i].handle, (unsigned long)hs[i].score);
}
