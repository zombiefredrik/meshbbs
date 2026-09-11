#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>

class UITask {
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[32];
  const char* _bbs_line;

  void renderCurrScreen();
public:
  UITask(DisplayDriver& display) : _display(&display) { _next_read = _next_refresh = 0; _bbs_line = ""; }
  void setBbsLine(const char* line) { _bbs_line = line; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version);

  void loop();
};