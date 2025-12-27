#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// --- Shared Resources for Threading ---
struct AsyncUpdateState {
  std::string PendingOutput; // Buffer for data ready to be shown in UI
  std::mutex OutputMutex;    // Protects PendingOutput
  std::atomic<bool> IsRunning{false};
  std::atomic<bool> IsFinished{false};
  std::atomic<int> ExitCode{0};
};

/**
 * @brief Executes a shell command and captures its stdout.
 */
std::string executeCommand(const char *Cmd) {
  std::array<char, 128> Buffer;
  std::string Result = "";
  std::unique_ptr<FILE, decltype(&pclose)> Pipe(popen(Cmd, "r"), pclose);
  if (!Pipe) {
    std::cerr << "popen() failed for command: " << Cmd << std::endl;
    return "";
  }
  while (fgets(Buffer.data(), Buffer.size(), Pipe.get()) != nullptr) {
    Result += Buffer.data();
  }
  return Result;
}

/**
 * @brief Checks for system updates (pacman & AUR).
 */
void checkUpdates() {
  std::string UpdateList = "";
  UpdateList += executeCommand("checkupdates");
  UpdateList += executeCommand("paru -Qua");

  static const std::regex AnsiRegex("\x1B\\[[0-9;]*[mK]");
  std::string CleanUpdateList = std::regex_replace(UpdateList, AnsiRegex, "");

  std::ofstream OutFile("/tmp/updates_list");
  if (!OutFile.is_open()) {
    std::cerr << "Error writing to /tmp/updates_list" << std::endl;
    exit(EXIT_FAILURE);
  }
  OutFile << CleanUpdateList;
}

/**
 * @brief Counts the number of lines in a file.
 */
int getLineCount(const std::string &Filename) {
  std::ifstream File(Filename);
  if (!File.is_open()) {
    std::cerr << "Error opening file: " << Filename << std::endl;
    return 0;
  }
  int LineCount = 0;
  std::string Line;
  while (std::getline(File, Line)) {
    LineCount++;
  }
  return LineCount;
}

/**
 * @brief Reads the entire content of a file into an std::string.
 */
std::string readFile(const std::string &Filename) {
  std::ifstream File(Filename);
  if (!File.is_open()) {
    return "Error: Could not open " + Filename;
  }
  std::stringstream Buffer;
  Buffer << File.rdbuf();
  return Buffer.str();
}

/**
 * @brief Tries to find x11-ssh-askpass or compatible helper.
 */
std::string findAskPass() {
  // 1. Try `which` to find it in PATH
  std::string Path = executeCommand("which x11-ssh-askpass");
  if (!Path.empty()) {
    // Trim newline
    Path.erase(Path.find_last_not_of(" \n\r\t") + 1);
    if (fs::exists(Path))
      return Path;
  }

  // 2. Check Arch Linux absolute path
  std::string ArchPath = "/usr/lib/ssh/x11-ssh-askpass";
  if (fs::exists(ArchPath)) {
    return ArchPath;
  }

  return "";
}

/**
 * @brief Worker function that runs in a separate thread.
 * Handles the execution, reading, cleaning, and queuing of output.
 */
void updateWorker(std::string Cmd, AsyncUpdateState *State) {
  // Open pipe in blocking mode (default for popen)
  // We do NOT need O_NONBLOCK here because we are in a background thread.
  // Blocking read is more CPU efficient.
  FILE *Pipe = popen(Cmd.c_str(), "r");

  if (!Pipe) {
    std::lock_guard<std::mutex> Lock(State->OutputMutex);
    State->PendingOutput += "Error: Failed to launch update command.\n";
    State->IsRunning = false;
    State->IsFinished = true;
    State->ExitCode = -1;
    return;
  }

  std::array<char, 1024> Buffer;
  // Compile regex once
  static const std::regex AnsiRegex(R"(\x1B\[[0-9;?]*[a-zA-Z])");

  while (fgets(Buffer.data(), Buffer.size(), Pipe) != nullptr) {
    std::string RawChunk = Buffer.data();

    // Clean output (Heavy regex operation happens here, off main thread)
    std::string CleanChunk = std::regex_replace(RawChunk, AnsiRegex, "");
    std::erase(CleanChunk, '\r');
    std::erase(CleanChunk, '\0');

    // Send to UI
    if (!CleanChunk.empty()) {
      std::lock_guard<std::mutex> Lock(State->OutputMutex);
      State->PendingOutput += CleanChunk;
    }
  }

  int Status = pclose(Pipe);
  State->ExitCode = Status;
  State->IsRunning = false;
  State->IsFinished = true;
}

/**
 * @brief Initializes and displays the GUI (ImGui) window for updates.
 */
void showUpdateGui() {
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    exit(EXIT_FAILURE);
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  GLFWwindow *Window = glfwCreateWindow(800, 600, "Update Manager", nullptr, nullptr);
  if (Window == nullptr) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    exit(EXIT_FAILURE);
  }
  glfwMakeContextCurrent(Window);
  glfwSwapInterval(1);

  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = NULL;

  ImFont *Font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/noto/NotoSans-Regular.ttf", 16.0f);
  if (Font) {
    io.FontDefault = Font;
  }

  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(Window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  std::string InitialUpdateList = readFile("/tmp/updates_list");

  // --- GUI State & Threading ---
  static std::string LiveOutputBuffer = "";
  static std::string CurrentTempFile = "";
  static AsyncUpdateState UpdateState; // Shared state

  while (!glfwWindowShouldClose(Window)) {
    glfwPollEvents();

    // --- Check for new data from the worker thread ---
    if (UpdateState.IsRunning || UpdateState.IsFinished) {
      std::string NewData;
      {
        // Quickly lock, grab data, and unlock to keep UI smooth
        std::lock_guard<std::mutex> Lock(UpdateState.OutputMutex);
        if (!UpdateState.PendingOutput.empty()) {
          NewData = std::move(UpdateState.PendingOutput);
          UpdateState.PendingOutput.clear(); // Clear after moving
        }
      }
      if (!NewData.empty()) {
        LiveOutputBuffer += NewData;
      }
    }

    // --- Check if finished ---
    if (UpdateState.IsFinished) {
      // Reset flag so we don't process this block multiple times
      UpdateState.IsFinished = false;

      // Cleanup temp file
      if (!CurrentTempFile.empty() && fs::exists(CurrentTempFile)) {
        fs::remove(CurrentTempFile);
        CurrentTempFile = "";
      }

      int Code = UpdateState.ExitCode;
      if (Code == 0) {
        LiveOutputBuffer += "\n\n--- UPDATE FINISHED ---";
      } else {
        LiveOutputBuffer += "\n\n--- UPDATE FAILED ---";
        LiveOutputBuffer += "\n(Exit Code: " + std::to_string(Code) + ")";
        LiveOutputBuffer += "\nPossible causes: Wrong password or network issue.";
      }
    }

    // --- Render Frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
      ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("Update Window", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

      // Disable inputs if running
      bool IsBusy = UpdateState.IsRunning;
      ImGui::BeginDisabled(IsBusy);

      if (ImGui::Button("Update")) {
        if (!IsBusy) {
          LiveOutputBuffer = InitialUpdateList;
          LiveOutputBuffer += "\n\n--- STARTING UPDATE ---\n";

          std::string AskPassPath = findAskPass();
          if (AskPassPath.empty()) {
            LiveOutputBuffer += "Warning: x11-ssh-askpass not found. Sudo might fail if no helper is configured.\n";
            // Fallback: hope SUDO_ASKPASS is set externally or sudo defaults to something
            AskPassPath = "x11-ssh-askpass";
          } else {
            LiveOutputBuffer += "Using AskPass: " + AskPassPath + "\n";
          }

          // Construct Command
          std::string InnerCmd = "export SUDO_ASKPASS=" + AskPassPath +
                                 " && sudo -A -v && "
                                 "stdbuf -oL paru -Syyuu --noconfirm --color=never --noprogressbar 2>&1";
          std::string Cmd = "script -q -e -c \"" + InnerCmd + "\" /dev/null";

          // Reset State
          UpdateState.ExitCode = 0;
          UpdateState.IsFinished = false;
          UpdateState.IsRunning = true;
          UpdateState.PendingOutput.clear();

          // Launch Thread
          std::thread(updateWorker, Cmd, &UpdateState).detach();
        }
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button("Close")) {
        if (!CurrentTempFile.empty() && fs::exists(CurrentTempFile)) {
          fs::remove(CurrentTempFile);
        }
        glfwSetWindowShouldClose(Window, true);
      }

      ImGui::Separator();
      ImGui::Text("Output:");

      ImGui::BeginChild("OutputRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

      if (LiveOutputBuffer.empty()) {
        ImGui::TextUnformatted(InitialUpdateList.c_str());
      } else {
        ImGui::TextUnformatted(LiveOutputBuffer.c_str());
      }

      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
      }

      ImGui::EndChild();
      ImGui::End();
    }

    int DisplayW, DisplayH;
    glfwGetFramebufferSize(Window, &DisplayW, &DisplayH);
    glViewport(0, 0, DisplayW, DisplayH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(Window);
  }

  // Cleanup on exit
  if (!CurrentTempFile.empty() && fs::exists(CurrentTempFile)) {
    fs::remove(CurrentTempFile);
  }
  // If thread is still running, detaching allows it to finish or be killed by OS.
  // Ideally we would join, but detach is fine for simple exit.

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(Window);
  glfwTerminate();
}

/**
 * @brief Main entry point.
 */
int main() {
  const char *Env = getenv("BLOCK_BUTTON");
  int Button = 0;
  if (Env != nullptr) {
    Button = atoi(Env);
  }

  int Updates = 0;
  switch (Button) {
  case 2: // Middle click
    showUpdateGui();
    checkUpdates();
    Updates = getLineCount("/tmp/updates_list");
    std::cout << Updates << std::endl;
    break;
  default: // Left click or i3blocks
    checkUpdates();
    Updates = getLineCount("/tmp/updates_list");
    std::cout << Updates << std::endl;
  }
  return 0;
}
