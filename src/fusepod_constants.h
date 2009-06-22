/******************************* -*- C++ -*- *******************************
 *                                                                         *
 *   file            : fusepod_constants.h                                 *
 *   date started    : 14 Feb 2006                                         *
 *   author          : Keegan Carruthers-Smith                             *
 *   email           : keegan.csmith@gmail.com                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
 
#ifndef _FUSEPOD_CONSTANTS_H_
#define _FUSEPOD_CONSTANTS_H_

extern "C" {
#include <sys/stat.h>
}
#include <string>

const mode_t MODE_DIR = (S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IXUSR | S_IXGRP | S_IXOTH);
const mode_t MODE_FILE = (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH);

const std::string filename_add = "add_songs";
const std::string filename_add_files = "add_files.sh";
const std::string filename_sync = "sync_ipod.sh";
const std::string filename_sync_do = "sync-ipod-now";
const std::string filename_stats = "statistics";

const std::string dir_transfer = "Transfer";
const std::string dir_transfer_ipod = ".fusepod_temp";

const std::string dir_playlists = "Playlists";
const std::string playlist_track_format = "%a - %t.%e";

const std::string default_config_file =
"/All/%a - %t.%e\n"
"/Artists/%a/%A/%T - %t.%e\n"
"/Albums/%A/%T - %a - %t.%e\n"
"/Genre/%g/%a/%A/%T - %t.%e\n";

#define ITUNESDB_PATH "/iPod_Control/iTunes/iTunesDB"

#endif
