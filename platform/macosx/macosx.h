#pragma once
#include <string>

namespace eve
{
namespace macosx
{

/**
 * Returns the filepath of the first detected love file in the Resources folder
 * in the main bundle
 * Returns an empty string if no file is found.
 **/
std::string getResources();

/**
 * Checks for drop-file events. Returns the filepath if an event occurred, or
 * an empty string otherwise.
 **/
std::string checkDropEvents();

/**
 * Returns the full path to the executable.
 **/
std::string getExecutablePath();

/**
 * Bounce the dock icon, if the app isn't in the foreground.
 **/
void requestAttention(bool continuous);

} // macosx
} // eve
