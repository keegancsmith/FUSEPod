/******************************* -*- C++ -*- *******************************
 *                                                                         *
 *   file            : fusepod_util.cpp                                    *
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
 
#include "fusepod_util.h"

#include <cstring>
#include <set>

static std::set<const char*, ltcasestr> fusepod_strings;

void fusepod_replace_reserved_chars (std::string & ret) {
    for (unsigned int i = 0; i < ret.size (); i++)
        if (ret [i] == '/' || ret [i] == '~')
            ret [i] = '_';
}

string fusepod_strip_string (const string & s) {
    unsigned int l = s.find_first_not_of (" \t\n\r");
    if (l == string::npos)
        return "";
    int r = s.find_first_not_of (" \t\n\r");
    return s.substr (l, s.length () - r);
}

string fusepod_check_string (const string & s, const string & unknown) {
    string ret = fusepod_strip_string (s);
    if (ret == "")
        return unknown;
    fusepod_replace_reserved_chars (ret);
    return ret;
}

const char * fusepod_get_string (const char * s) {
    std::set<const char*, ltcasestr>::iterator i = fusepod_strings.find (s);
    if (i == fusepod_strings.end ()) {
        fusepod_strings.insert (strdup (s));
        return fusepod_get_string (s);
    }
    return *i;
}

vector<char*> fusepod_split_path (char * s, char c) {
    vector<char*> nodes;
    if (*s != c && *s != 0)
        nodes.push_back (s);
    while (*s) {
        if (*s == c) {
            *s = 0;
            if (s[1] != 0)
                nodes.push_back (&(s[1]));
        }
        ++s;
    }
    return nodes;
}

bool fusepod_starts_with (const char * s, const char * prefix) {
    const char *s1 = s - 1;
    const char *p1 = prefix - 1;
    
    while (*(++s1) == *(++p1) && *s1);

    return *p1 == 0;
}

string fusepod_int_to_string (int i) {
    char tmp [10];
    i %= 1000000000;
    sprintf (tmp, "%d", i);
    return string (tmp);
}
