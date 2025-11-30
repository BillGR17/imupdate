#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem> // C++17/20 for permissions and file handling
#include <fstream>
#include <iostream>
#include <memory>
#include <random> // For random filename generation
#include <sstream>
#include <string>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include <regex>

namespace fs = std::filesystem;

/**
 * @brief Executes a shell command and captures its stdout.
 * @param Cmd The command to execute (C-style string).
 * @return The standard output (stdout) of the command as an std::string.
 */
std::string executeCommand(const char *Cmd) {
  std::array<char, 128> Buffer;
  std::string Result = "";
  // Use popen to execute and open a read pipe
  std::unique_ptr<FILE, decltype(&pclose)> Pipe(popen(Cmd, "r"), pclose);
  if (!Pipe) {
    std::cerr << "popen() failed for command: " << Cmd << std::endl;
    return "";
  }
  // Read the output chunk by chunk
  while (fgets(Buffer.data(), Buffer.size(), Pipe.get()) != nullptr) {
    Result += Buffer.data();
  }
  return Result;
}

/**
 * @brief Checks for system updates (pacman & AUR).
 *
 * Runs 'checkupdates' and 'paru -Qua', strips the output
 * of ANSI color codes, and writes the result to /tmp/updates_list.
 */
void checkUpdates() {
  std::string UpdateList = "";
  UpdateList += executeCommand("checkupdates");
  UpdateList += executeCommand("paru -Qua");

  // Remove ANSI color codes from the output
  static const std::regex AnsiRegex("\x1B\\[[0-9;]*[mK]");
  std::string CleanUpdateList = std::regex_replace(UpdateList, AnsiRegex, "");

  std::ofstream OutFile("/tmp/updates_list");
  if (!OutFile.is_open()) {
    std::cerr << "Error writing to /tmp/updates_list" << std::endl;
    exit(EXIT_FAILURE);
  }
  // Write the "clean" list to the file
  OutFile << CleanUpdateList;
}

/**
 * @brief Counts the number of lines in a file.
 *
 * Used primarily for the default (i3blocks) execution
 * to show the *number* of updates.
 * @param Filename The path to the file.
 * @return The number of lines.
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
 * @param Filename The path to the file.
 * @return The content of the file.
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
 * @brief Initializes and displays the GUI (ImGui) window for updates.
 */
void showUpdateGui() {
  // --- 1. Initialize GLFW ---
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    exit(EXIT_FAILURE);
  }

  // Set window hints (OpenGL 3.3 Core)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  // --- 2. Create Window ---
  GLFWwindow *Window = glfwCreateWindow(800, 600, "Update Manager", nullptr, nullptr);
  if (Window == nullptr) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    exit(EXIT_FAILURE);
  }
  glfwMakeContextCurrent(Window);
  glfwSwapInterval(1); // Enable VSync

  // --- 3. Initialize ImGui ---
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();

  // Disable saving/loading of imgui.ini
  io.IniFilename = NULL;

  // Load custom font
  ImFont *Font = io.Fonts->AddFontFromFileTTF("/usr/share/fonts/noto/NotoSans-Regular.ttf", 16.0f);
  if (Font) {
    io.FontDefault = Font;
  }

  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(Window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // --- 4. Load Initial Update List ---
  std::string InitialUpdateList = readFile("/tmp/updates_list");

  // --- 5. GUI State Variables ---
  static std::string LiveOutputBuffer = ""; // Buffer for the live output
  static std::unique_ptr<FILE, decltype(&pclose)> UpdatePipe(nullptr, pclose);
  static bool UpdateRunning = false;  // Is the update in progress?
  static bool UpdateFinished = false; // Has the update finished?
  static int PipeFD = -1;             // File Descriptor of the output pipe

  // Keep track of the temp file to ensure deletion
  static std::string CurrentTempFile = "";

  // --- 6. Main Application Loop ---
  while (!glfwWindowShouldClose(Window)) {
    glfwPollEvents();

    // --- 6a. Check for Live Output (Non-Blocking Read) ---
    if (UpdatePipe) {
      UpdateRunning = true;
      std::array<char, 1024> TmpBuffer;
      // Read from the pipe non-blockingly
      ssize_t BytesRead = read(PipeFD, TmpBuffer.data(), TmpBuffer.size() - 1);

      if (BytesRead > 0) {
        // New data arrived
        TmpBuffer[BytesRead] = '\0';
        std::string RawChunk = TmpBuffer.data();

        // 1. Regex to strip ANSI escape codes (colors, cursor movements, etc.)
        // This removes [25l, [1E, colors, etc.
        static const std::regex AnsiRegex(R"(\x1B\[[0-9;?]*[a-zA-Z])");
        std::string CleanChunk = std::regex_replace(RawChunk, AnsiRegex, "");

        // 2. Remove Carriage Returns (\r) and null bytes
        std::erase(CleanChunk, '\r');
        std::erase(CleanChunk, '\0');

        LiveOutputBuffer += CleanChunk;

      } else if (BytesRead == 0) {
        // End-of-File (EOF) - The command has finished execution.
        FILE *RawPipe = UpdatePipe.release();
        int ExitStatus = pclose(RawPipe);

        UpdateRunning = false;
        UpdateFinished = true;

        // Ensure temp file is deleted even if the shell command failed to do so
        if (!CurrentTempFile.empty() && fs::exists(CurrentTempFile)) {
          fs::remove(CurrentTempFile);
          CurrentTempFile = "";
        }

        if (ExitStatus == 0) {
          LiveOutputBuffer += "\n\n--- UPDATE FINISHED ---";
        } else {
          LiveOutputBuffer += "\n\n--- UPDATE FAILED ---";
          LiveOutputBuffer += "\n(Exit Code: " + std::to_string(ExitStatus) + ")";
          LiveOutputBuffer += "\nPossible causes: Wrong password or network issue.";
        }
      } else {
        // Error or temporarily no data
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          LiveOutputBuffer += "\n\n--- ERROR READING PIPE ---";
          UpdatePipe.reset();
          UpdateRunning = false;
          UpdateFinished = true;
          // Fallback cleanup
          if (!CurrentTempFile.empty() && fs::exists(CurrentTempFile)) {
            fs::remove(CurrentTempFile);
          }
        }
      }
    }

    // --- 6b. Start new ImGui frame ---
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // --- 6c. Draw the UI ---
    {
      ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
      ImGui::SetNextWindowSize(io.DisplaySize);
      ImGui::Begin("Update Window", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

      static char Password[128] = "";

      ImGui::BeginDisabled(UpdateRunning);

      ImGui::Text("Password:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(100);
      ImGui::InputText("##password", Password, std::size(Password), ImGuiInputTextFlags_Password);

      ImGui::SameLine();

      if (ImGui::Button("Update")) {
        if (!UpdateRunning) {
          LiveOutputBuffer = InitialUpdateList;
          UpdateRunning = true;
          UpdateFinished = false;

          // 1. Generate a random temporary filename
          std::random_device RD;
          std::mt19937 Gen(RD());
          std::uniform_int_distribution<> Dis(10000, 99999);
          CurrentTempFile = "/tmp/imupdate_pass_" + std::to_string(Dis(Gen));

          // 2. Write password to the temp file securely
          {
            std::ofstream PassFile(CurrentTempFile);
            if (PassFile.is_open()) {
              PassFile << Password;
              PassFile.close();
              // Set permissions to 600 (Owner Read/Write ONLY) using C++17 filesystem
              fs::permissions(CurrentTempFile, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
            } else {
              LiveOutputBuffer = "Error: Could not create temp password file.";
              UpdateRunning = false;
            }
          }

          if (UpdateRunning) {
            // 3. Construct the command
            // - script: provides fake TTY for sudo
            // - sudo -S -v < file: Reads pass from file, validates credentials
            // - rm file: deletes file immediately
            // - paru ... --noprogressbar: Runs update without garbage output
            std::string InnerCmd = "sudo -S -v < " + CurrentTempFile + " && rm " + CurrentTempFile +
                                   " && stdbuf -oL paru -Syu --noconfirm --color=never --noprogressbar 2>&1";

            std::string Cmd = "script -q -e -c \"" + InnerCmd + "\" /dev/null";

            UpdatePipe.reset(popen(Cmd.c_str(), "r"));

            if (!UpdatePipe) {
              LiveOutputBuffer += "Failed to execute command via popen().";
              UpdateRunning = false;
              // Cleanup if popen fails
              if (fs::exists(CurrentTempFile))
                fs::remove(CurrentTempFile);
            } else {
              PipeFD = fileno(UpdatePipe.get());
              fcntl(PipeFD, F_SETFL, O_NONBLOCK);

              // Clear password from memory for better security
              // memset(Password, 0, sizeof(Password));
            }
          }
        }
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::Button("Close")) {
        // Ensure cleanup on exit
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

    // --- 6d. Render ---
    int DisplayW, DisplayH;
    glfwGetFramebufferSize(Window, &DisplayW, &DisplayH);
    glViewport(0, 0, DisplayW, DisplayH);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(Window);
  }

  // --- 7. Cleanup ---
  // Final safeguard cleanup
  if (!CurrentTempFile.empty() && fs::exists(CurrentTempFile)) {
    fs::remove(CurrentTempFile);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(Window);
  glfwTerminate();
}

/**
 * @brief Main entry point for the program.
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
  default: // Left click or i3blocks execution
    checkUpdates();
    Updates = getLineCount("/tmp/updates_list");
    std::cout << Updates << std::endl;
  }

  return 0;
}
