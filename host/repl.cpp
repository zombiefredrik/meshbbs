// Interactive BBS terminal on the host. Simulates one or more users logging in over the mesh.
//   ./repl [datadir]
// Special lines:  !login <name> [admin]   switch to / log in as a simulated user (name -> fake pubkey)
//                 !time +<secs>           advance the clock
//                 !quit
#include "BbsEngine.h"
#include "StdioFs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void fakeKey(const char* name, uint8_t* key) {
  memset(key, 0, BBS_PUBKEY_LEN);
  uint32_t h = 2166136261u;
  for (const char* p = name; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  for (int i = 0; i < BBS_PUBKEY_LEN; i++) { key[i] = (uint8_t)(h >> (8 * (i % 4))); h = h * 1103515245u + 12345u; }
}

static void show(const BbsOutput& out, const BbsAction& act) {
  for (int i = 0; i < out.count; i++) {
    printf("\033[32m--- page %d (%d bytes) ---\033[0m\n%s\n", i + 1, (int)strlen(out.pages[i]), out.pages[i]);
  }
  if (act.broadcast) printf("\033[33m>>> ROOM POST: %s\033[0m\n", act.broadcast_text);
  if (act.logout) printf("\033[33m>>> session closed\033[0m\n");
}

int main(int argc, char** argv) {
  const char* dir = argc > 1 ? argv[1] : "./bbsdata";
  StdioFs fs(dir);
  BbsStorage st(fs);
  uint32_t now = (uint32_t)time(NULL);
  BbsEngine bbs(st, "MeshXpress", now);
  uint8_t key[BBS_PUBKEY_LEN];
  char who[32] = "";
  BbsOutput out; BbsAction act;
  printf("MeshXpress host REPL. Data in %s. Start with: !login <name> [admin]\n", dir);
  char line[512];
  while (printf("%s> ", who[0] ? who : "(nobody)"), fflush(stdout), fgets(line, sizeof(line), stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    if (strncmp(line, "!login ", 7) == 0) {
      char name[32] = "", flag[16] = "";
      sscanf(line + 7, "%31s %15s", name, flag);
      snprintf(who, sizeof(who), "%s", name);
      fakeKey(name, key);
      bbs.onLogin(key, strcmp(flag, "admin") == 0, now, out, act);
      show(out, act);
    } else if (strncmp(line, "!time ", 6) == 0) {
      now += (uint32_t)atoi(line + 7);
      printf("clock advanced\n");
    } else if (strcmp(line, "!quit") == 0) {
      break;
    } else if (!who[0]) {
      printf("!login <name> first\n");
    } else {
      bbs.onInput(key, line, now, out, act);
      show(out, act);
    }
  }
  return 0;
}
