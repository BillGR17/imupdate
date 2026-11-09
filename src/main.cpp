#include "Updates.hpp"
#include "Utils.hpp"
#include "UI.hpp"
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char *argv[]) {
  bool showUi = true;
  bool debug = false;
  bool runInTray = false;

  // Parse arguments
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-cli") {
      showUi = false;
    }
    if (std::string_view(argv[i]) == "-debug") {
      debug = true;
    }
    if (std::string_view(argv[i]) == "-tray") {
      runInTray = true;
    }
  }

  // Check updates before opening the GUI so the UI and tray icon show fresh data
  checkUpdates(debug);

  if (showUi) {
    showUpdateGui(runInTray);
  }

  int Updates = getLineCount("/tmp/updates_list");
  std::cout << Updates << std::endl;

  return 0;
}
