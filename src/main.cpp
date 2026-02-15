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

  // Parse arguments
  for (int i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-cli") {
      showUi = false;
    }
    if (std::string_view(argv[i]) == "-debug") {
      debug = true;
    }
  }

  if (showUi) {
    showUpdateGui();
  }

  // Unconditionally check updates
  checkUpdates(debug);
  int Updates = getLineCount("/tmp/updates_list");
  std::cout << Updates << std::endl;

  return 0;
}
