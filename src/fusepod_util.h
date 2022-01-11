/******************************* -*- C++ -*- *******************************
 *                                                                         *
 *   file            : fusepod_util.h                                      *
 *   date started    : 12 Feb 2006                                         *
 *   author          : Keegan Carruthers-Smith                             *
 *   email           : keegan.csmith@gmail.com                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef _FUSEPOD_UTIL_H_
#define _FUSEPOD_UTIL_H_

#include <vector>
#include <string>
#include <cstring>

using std::string;
using std::vector;

/**
 * Function object which return compares 2 strings ignoring case.
 */
struct ltcasestr {
    bool operator()(const char *a, const char *b) const {
        return strcasecmp(a, b) < 0;
    }
};

/**
 * Removes characters not allowed in filenames.
 */
void fusepod_replace_reserved_chars(string &ret);

/**
 * Strips/Trims a string.
 */
string fusepod_strip_string(const string &s);

/**
 * Returns a string which has been trimmed/stripped and has had it's reserved
 * characters removed.  If the string ends up being empty, unknown is
 * returned. unknown defaults to "Unknown".
 */
string fusepod_check_string(const string &s, const string &unknown = "Unknown");

/**
 * Returns a copy of the string. Use this function to save space. All strings
 * called from this function are copied if they have not been asked for before
 * for future reference.
 */
const char *fusepod_get_string(const char *s);

/**
 * Modifies the string that is passed by replacing all occurences of
 * c with the NULL character. It then returns a vector of char pointers
 * which all point inside s.
 * eg: fusepod_split_path (s) (where s = "/home/keegan/ipod") will return:
 * the vector {"home", "keegan", "ipod"}
 */
vector<char*> fusepod_split_path(char *s, char c);

/**
 * Checks whether a string starts with the supplied prefix.
 */
bool fusepod_starts_with(const char *s, const char *prefix);

/**
 * @returns A string representation of a number.
 */
string fusepod_int_to_string(int i);

#endif
