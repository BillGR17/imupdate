#include "Updates.hpp"
#include "Utils.hpp"
#include <regex>
#include <fstream>
#include <iostream>
#include <format>
#include <filesystem>

void checkUpdates(bool Debug) {
  std::string UpdateList = "";
  UpdateList += executeCommand("checkupdates", Debug);
  UpdateList += executeCommand("paru -Qua", Debug);

  // Remove ANSI color codes from the output
  static const std::regex AnsiRegex("\x1B\\[[0-9;]*[mK]");
  std::string CleanUpdateList = std::regex_replace(UpdateList, AnsiRegex, "");

  std::ofstream OutFile("/tmp/updates_list");
  if (!OutFile.is_open()) {
    if (Debug) {
      std::cerr << std::format("Error writing to {}\n", "/tmp/updates_list");
    }
    exit(EXIT_FAILURE);
  }
  // Write the "clean" list to the file
  OutFile << CleanUpdateList;
}
