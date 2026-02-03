#pragma once

#include <string>
#include <string_view>
#include <cstdio>

struct PipeDeleter {
  void operator()(FILE* fp) const { if (fp) pclose(fp); }
};

std::string executeCommand(const char* Cmd);
int getLineCount(std::string_view Filename);
std::string readFile(std::string_view Filename);
