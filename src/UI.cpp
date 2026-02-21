#include "UI.hpp"
#include "Utils.hpp"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <format>
#include <filesystem>
#include <random>
#include <fstream>
#include <regex>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
#include <array>
#include <cstring>

namespace fs = std::filesystem;

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
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);

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
  static std::unique_ptr<FILE, PipeDeleter> UpdatePipe(nullptr);
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
          LiveOutputBuffer +=
              std::format("\n\n--- UPDATE FAILED ---\n(Exit Code: {})\nPossible causes: Wrong password or network issue.", ExitStatus);
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
      bool EnterPressed =
          ImGui::InputText("##password", Password, std::size(Password), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

      ImGui::SameLine();

      if (ImGui::Button("Update") || EnterPressed) {
        if (!UpdateRunning) {
          LiveOutputBuffer = InitialUpdateList;
          UpdateRunning = true;
          UpdateFinished = false;

          // 1. Generate a random temporary filename
          std::random_device RD;
          std::mt19937 Gen(RD());
          std::uniform_int_distribution<> Dis(10000, 99999);
          CurrentTempFile = std::format("/tmp/imupdate_pass_{}", Dis(Gen));

          // 2. Write askpass script to the temp file securely
          {
            std::ofstream PassFile(CurrentTempFile);
            if (PassFile.is_open()) {
              // This is an executable script that SUDO_ASKPASS will run
              // It reads the password from the IMUPDATE_PASS environment variable
              PassFile << "#!/bin/sh\nprintf '%s\\n' \"$IMUPDATE_PASS\"\n";
              PassFile.close();
              // Set permissions to 700 (Owner Read/Write/Execute ONLY)
              fs::permissions(CurrentTempFile, fs::perms::owner_all, fs::perm_options::replace);
            } else {
              LiveOutputBuffer = "Error: Could not create temp password file.";
              UpdateRunning = false;
            }
          }

          if (UpdateRunning) {
            // Set the password as an environment variable before popen
            setenv("IMUPDATE_PASS", Password, 1);

            // 3. Construct the command
            // - export SUDO_ASKPASS: sets the helper
            // - sudo -A -v: refreshes credentials using the helper
            // - paru ...: runs the update
            // Note: We do NOT delete the file here immediately. Cleanup happens on exit or next run.
            std::string Cmd = std::format("export SUDO_ASKPASS={} && sudo -A -v && stdbuf -oL paru -Syu --noconfirm "
                                          "--color=never --noprogressbar 2>&1",
                                          CurrentTempFile);

            UpdatePipe.reset(popen(Cmd.c_str(), "r"));

            // Clear the environment variable now that the child process has been spawned
            unsetenv("IMUPDATE_PASS");

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
              memset(Password, 0, sizeof(Password));
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

      static bool AutoScroll = true;
      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        AutoScroll = true;
      } else {
        AutoScroll = false;
      }

      const std::string &OutputText = LiveOutputBuffer.empty() ? InitialUpdateList : LiveOutputBuffer;
      ImGui::TextUnformatted(OutputText.c_str());

      if (AutoScroll) {
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
