# **imupdate \- i3blocks Update Notifier with ImGui**

imupdate is a lightweight C++ application designed to integrate with i3blocks. It provides a simple update count for your status bar and a full graphical (ImGui) interface for viewing and running system updates.  
It's built to be a simple, single-file executable that handles both command-line output (for i3blocks) and a graphical interface (for interaction).

## **How it Works**

The application has two modes, triggered by the BLOCK\_BUTTON environment variable set by i3blocks:

1. **Default Mode (No Click / Left Click):**  
   * The app runs checkupdates and paru \-Qua to get a list of all available updates (both official and from the AUR).  
   * It strips any ANSI color codes from the output.  
   * It writes this "clean" list to /tmp/updates\_list.  
   * It counts the number of lines (i.e., the number of pending updates) and prints this number to stdout.  
   * i3blocks captures this number and displays it on your status bar.  
2. **GUI Mode (BLOCK\_BUTTON=2 / Middle Click):**  
   * The app launches a graphical window using **ImGui** and GLFW.  
   * It first loads and displays the list of updates from /tmp/updates\_list.  
   * It provides two buttons: "Update" and "Close".  
   * If you click **"Update"**:  
     * It executes pkexec paru \-Syu 2\>&1 to run the full system upgrade.  
     * It captures the live stdout and stderr from paru *as it runs*.  
     * It streams this live output directly into the ImGui window, so you can see the update progress in real-time without a separate terminal.  
     * The "Update" button is disabled while the process is running.  
   * The GUI uses the Noto Sans font for a clean look and disables the imgui.ini file to prevent saving window state.

## **Dependencies**

To build and run this application, you need:

* cmake / ninja / clang++  
* glfw-x11  
* imgui  
* paru  
* noto-fonts (the path /usr/share/fonts/noto/NotoSans-Regular.ttf is hardcoded).

## **Example i3blocks Configuration**
```
[updater]  
command=/path/to/your/build/imupdate  
interval=1800 # Check every 30 minutes  
signal=10 
```
