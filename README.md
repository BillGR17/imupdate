# imupdate

imupdate is a graphical utility for managing system updates on **Arch Linux**. Built with C++23 and Dear ImGui, it provides a user-friendly interface to check for available updates using `checkupdates` and `paru`, visualize the list of packages, and execute the update process securely.

## Features

-   **Update Checking**: Automatically checks for updates from both official repositories (via `checkupdates`) and the AUR (via `paru`).
-   **Visual Interface**: Displays a clean list of available updates.
-   **Secure Updating**: Handles password input securely via a temporary helper script and `SUDO_ASKPASS` to authorize `sudo`.
-   **Live Progress**: Shows real-time output from the update command (`paru -Syu`).
-   **Ansi Stripping**: Automatically cleans ANSI color codes from terminal output for proper display in the GUI.

## Prerequisites

Before building and running ArchUpdater, ensure you have the following installed:

-   **Arch Linux**
-   **C++ Compiler**: A compiler checking C++23 support (Clang is configured in `CMakeLists.txt`).
-   **CMake**: Version 3.16 or higher.
-   **Make/Ninja**: Build system.
-   **GLFW3**: Windowing library.
-   **OpenGL**: Graphics library.
-   **checkupdates**: Part of the `pacman-contrib` package.
-   **paru**: AUR helper (required for the update command logic).

```bash
sudo pacman -S clang cmake make glfw-x11 pacman-contrib
# Install paru from AUR if not already installed
```

> **Note**: This tool specifically invokes `paru`. If you use another AUR helper or just `pacman`, you will need to modify `src/UI.cpp` and `src/Updates.cpp`.

## Build Instructions

1.  Clone the repository:
    ```bash
    git clone https://github.com/BillGR17/imupdate.git
    cd imupdate
    ```

2.  Create a build directory:
    ```bash
    mkdir build && cd build
    ```

3.  Configure with CMake:
    ```bash
    cmake ..
    ```

4.  Build the project:
    ```bash
    make
    ```

## Usage

Run the executable from the build directory:

```bash
./imupdate
```

### CLI Mode
You can run the tool in CLI-only mode (though the main feature is the GUI) by passing the `-cli` flag, which skips opening the window but currently just runs the check:

```bash
./imupdate -cli
```

### In the GUI
1.  Launch the application.
2.  Review the list of updates in the "Output" section.
3.  Enter your `sudo` password in the password field.
4.  Click **Update** to start the process.
5.  Wait for the process to complete (Success/Fail message will appear).
6.  Click **Close** to exit.
