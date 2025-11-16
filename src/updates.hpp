#pragma once

#include <string>
/**
 * @brief Executes 'checkupdates' and 'paru -Qua' commands to
 * create the update list at /tmp/updates_list.
 */
void checkUpdates();

/**
 * @brief Counts the number of lines in a file.
 *
 * @param Filename The path to the file (e.g., "/tmp/updates_list").
 * @return The number of lines (updates).
 */
int getLineCount(const std::string &Filename);
