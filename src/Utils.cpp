#include "Utils.hpp"
#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <format>

namespace fs = std::filesystem;

std::string executeCommand(const char *Cmd) {
  std::array<char, 128> Buffer;
  std::string Result = "";
  // Use popen to execute and open a read pipe
  std::unique_ptr<FILE, PipeDeleter> Pipe(popen(Cmd, "r"));
  if (!Pipe) {
    std::cerr << std::format("popen() failed for command: {}\n", Cmd);
    return "";
  }
  // Read the output chunk by chunk
  while (fgets(Buffer.data(), Buffer.size(), Pipe.get()) != nullptr) {
    Result += Buffer.data();
  }
  return Result;
}

int getLineCount(std::string_view Filename) {
  std::ifstream File{fs::path(Filename)};
  if (!File.is_open()) {
    std::cerr << std::format("Error opening file: {}\n", Filename);
    return 0;
  }
  int LineCount = 0;
  std::string Line;
  while (std::getline(File, Line)) {
    LineCount++;
  }
  return LineCount;
}

std::string readFile(std::string_view Filename) {
  std::ifstream File{fs::path(Filename)};
  if (!File.is_open()) {
    return std::format("Error: Could not open {}", Filename);
  }
  std::stringstream Buffer;
  Buffer << File.rdbuf();
  return Buffer.str();
}
