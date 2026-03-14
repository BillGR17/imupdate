#include "UI.hpp"
#include "Utils.hpp"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "tray.hpp"
#include "Updates.hpp"

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

static GLFWwindow* g_Window = nullptr;
static bool g_WindowVisible = true;
static bool g_ShouldRefresh = false;
static bool g_ShouldClose = false;
static int g_UpdateCount = 0;

void cb_toggle_ui(struct tray *tray) {
  if (g_WindowVisible) {
    glfwHideWindow(g_Window);
    g_WindowVisible = false;
  } else {
    glfwShowWindow(g_Window);
    g_WindowVisible = true;
  }
}

void cb_refresh(struct tray_menu *item) {
  g_ShouldRefresh = true;
}

void cb_close(struct tray_menu *item) {
  g_ShouldClose = true;
}

static struct tray tray_struct = {
    .icon = (char*)"/tmp/imupdate_icon.svg",
    .menu =
        (struct tray_menu[]){
            {(char*)"Refresh", 0, 0, cb_refresh, NULL, NULL},
            {(char*)"Close", 0, 0, cb_close, NULL, NULL},
            {NULL, 0, 0, NULL, NULL, NULL}},
    .cb = cb_toggle_ui
};

void updateTrayIcon(int count) {
  std::ofstream out("/tmp/imupdate_icon.svg");
  std::string color = count > 0 ? "red" : "green";
  out << std::format(
      R"(<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><circle cx="32" cy="32" r="30" fill="{}" /><text x="32" y="44" font-size="34" font-family="sans-serif" font-weight="bold" fill="white" text-anchor="middle">{}</text></svg>)",
      color, count);
  out.close();
  tray_update(&tray_struct);
}

void showUpdateGui(bool runInTray) {
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
  g_Window = Window;
  g_WindowVisible = !runInTray;

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
  static int PipeFD = -1;             // File Descriptor of the output pipe

  // Keep track of the temp file to ensure deletion
  static std::string CurrentTempFile = "";

  if (runInTray) {
    g_UpdateCount = getLineCount("/tmp/updates_list");
    glfwHideWindow(Window);
    g_WindowVisible = false;
    tray_init(&tray_struct);
    updateTrayIcon(g_UpdateCount);
  }

  // --- 6. Main Application Loop ---
  while (!glfwWindowShouldClose(Window)) {
    glfwPollEvents();

    if (runInTray) {
      if (tray_loop(0) == -1) break; // Non-blocking tray loop
    }

    if (g_ShouldClose) {
      break;
    }

    if (g_ShouldRefresh) {
      g_ShouldRefresh = false;
      checkUpdates(false);
      InitialUpdateList = readFile("/tmp/updates_list");
      LiveOutputBuffer = ""; // Reset live buffer to show new updates
      g_UpdateCount = getLineCount("/tmp/updates_list");
      updateTrayIcon(g_UpdateCount);
    }

    if (!g_WindowVisible) {
      // Sleep a bit to prevent high CPU usage when hidden
      usleep(16000); // ~60fps
      continue;
    }

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

        // Ensure temp file is deleted even if the shell command failed to do so
        // Ensure temp file and token are deleted even if the shell command failed to do so
        if (!CurrentTempFile.empty()) {
          if (fs::exists(CurrentTempFile)) fs::remove(CurrentTempFile);
          std::string UsedFile = CurrentTempFile + ".used";
          if (fs::exists(UsedFile)) fs::remove(UsedFile);
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
          // Fallback cleanup
          if (!CurrentTempFile.empty()) {
            if (fs::exists(CurrentTempFile)) fs::remove(CurrentTempFile);
            std::string UsedFile = CurrentTempFile + ".used";
            if (fs::exists(UsedFile)) fs::remove(UsedFile);
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
              // It also checks for a .used file to ensure it only runs once, avoiding sudo lockouts
              PassFile << std::format("#!/bin/sh\n"
                                      "if [ -f \"{0}.used\" ]; then exit 1; fi\n"
                                      "touch \"{0}.used\"\n"
                                      "printf '%s\\n' \"$IMUPDATE_PASS\"\n",
                                      CurrentTempFile);
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
            // - rm -f {}.used: allow the helper to run again for paru, since paru might allocate a PTY and bypass sudo cache
            // - paru ...: runs the update
            // Note: We do NOT delete the file here immediately. Cleanup happens on exit or next run.
            std::string Cmd = std::format("{{ export SUDO_ASKPASS={0} && sudo -A -v && rm -f {0}.used && stdbuf -oL paru -Syu --noconfirm "
                                          "--color=never --noprogressbar; }} 2>&1",
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
              std::string UsedFile = CurrentTempFile + ".used";
              if (fs::exists(UsedFile))
                fs::remove(UsedFile);
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
        if (!CurrentTempFile.empty()) {
          if (fs::exists(CurrentTempFile)) fs::remove(CurrentTempFile);
          std::string UsedFile = CurrentTempFile + ".used";
          if (fs::exists(UsedFile)) fs::remove(UsedFile);
        }
        if (runInTray) {
            glfwHideWindow(Window);
            g_WindowVisible = false;
        } else {
            glfwSetWindowShouldClose(Window, true);
        }
      }

      ImGui::Separator();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Output:");

      ImGui::BeginChild("OutputRegion", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

      static bool AutoScroll = true;
      if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        AutoScroll = true;
      } else {
        AutoScroll = false;
      }

      const std::string &OutputText = LiveOutputBuffer.empty() ? InitialUpdateList : LiveOutputBuffer;

      ImVec2 textSize = ImGui::CalcTextSize(OutputText.c_str());
      float width = std::max(ImGui::GetContentRegionAvail().x, textSize.x);

      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0)); // Transparent background
      ImGui::InputTextMultiline("##text", const_cast<char *>(OutputText.c_str()), OutputText.size() + 1,
                                ImVec2(width, textSize.y + ImGui::GetTextLineHeight() * 2),
                                ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoHorizontalScroll);
      ImGui::PopStyleColor();

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
  if (!CurrentTempFile.empty()) {
    if (fs::exists(CurrentTempFile)) fs::remove(CurrentTempFile);
    std::string UsedFile = CurrentTempFile + ".used";
    if (fs::exists(UsedFile)) fs::remove(UsedFile);
  }

  if (runInTray) {
      tray_exit();
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(Window);
  glfwTerminate();
}
