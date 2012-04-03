/******************************* -*- C++ -*- *******************************
 *                                                                         *
 *   file            : fusepod_ipod.cpp                                    *
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
 
#include "fusepod_ipod.h"
#include "fusepod_util.h"
#include "fusepod_constants.h"

#include <fileref.h>
#include <tag.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
}

using namespace std;

Node::~Node () {
    for (iterator i = begin (); i != end (); ++i)
        delete (*i);
    children.clear ();
}

Node * Node::find (const NodeValue & nv) {
    Node n (nv, 0);
    iterator i = children.find (&n);
    if (i == children.end ())
        return 0;
    else
        return *i;
}

Node * Node::find (const char * s) {
    return this->find (NodeValue (s));
}

Node * Node::addChild (const NodeValue & nv) {
    if (find (nv))
        return 0;
    Node * n = new Node (nv, this);
    children.insert (n);

    /* Update nlinks if necessary */
    if (nv.mode == MODE_DIR)
        value.size++;

    return n;
}

void Node::remove_from_parent () {
    if (parent) {
        parent->children.erase (this);

        /* Update nlinks if necessary */
        if (value.mode == MODE_DIR)
            parent->value.size--;

        parent = 0;
    }
}

FUSEPod::FUSEPod (const string & mount_point, vector<string> paths_descs) {
    this->mount_point = mount_point;
    this->paths_descs = paths_descs;
    this->syncing     = false;

    char * text = new char [1];
    text [0] = 0;
    root = new Node (NodeValue (text, MODE_DIR));

    cout << "Reading database in ipod " << mount_point << endl;
    ipod = itdb_parse (mount_point.c_str (), 0);
    if (ipod == 0) {
        cout << "Reading database failed. Creating in memory database" << endl;
        ipod = itdb_new ();
        itdb_set_mountpoint (ipod, mount_point.c_str ());
    }

    add_orphaned_tracks ();

    this->num_tracks    = g_list_length (ipod->tracks);
    this->num_playlists = g_list_length (ipod->playlists);

    add_playlists ();
    add_all_tracks ();
}

FUSEPod::~FUSEPod () {
    delete root;
    itdb_write (ipod, 0);
    itdb_free (ipod);
}

string FUSEPod::discover_ipod () {
    char * ipod_dir = getenv ("IPOD_DIR");
    if (!ipod_dir)
        ipod_dir = getenv ("IPOD_MOUNTPOINT");
    
    if (ipod_dir)
        return string (ipod_dir);

    string mounts = "/proc/mounts";
    if (access (mounts.c_str(), R_OK) != 0)
        mounts = "/etc/mtab";

    if (access (mounts.c_str(), R_OK) != 0)
        return "";

    vector<string> possible; //Store possible places for fusepod to mount
    set<string> already_mounted;

    string dev, dir, type, options, dump, pass;
    std::ifstream fin (mounts.c_str());
    while (fin) {
        fin >> dev >> dir >> type >> options >> dump >> pass;

        struct stat st;
        stat ((dir + ITUNESDB_PATH).c_str (), &st);

        if (S_ISREG(st.st_mode))
            possible.push_back (dir);

        if (dev == "fusepod") {
            ifstream stats ( (dir + "/" + filename_stats).c_str() );
     
            string line;       
            while (stats) {//TODO Check if this causes a crash
                getline (stats, line);
                cout << line << endl;

                if (!fusepod_starts_with (line.c_str(), "Real Mountpoint: "))
                    continue;

                // Gets the path of the mount point
                already_mounted.insert (string (line, strlen("Real Mountpoint: ")));
                break;
            }
            stats.close ();
        }
    }
    fin.close ();

    for (vector<string>::iterator it = possible.begin(); it != possible.end(); ++it)
        if (already_mounted.find (*it) == already_mounted.end())
            return *it;

    if (possible.size () > 0) {
        cout << "WARNING: iPod at " + possible[0] + " has already been mounted by FUSEPod." << endl;
        return possible [0];
    }
    
    return "";
}

bool FUSEPod::create_itunes_dirs (const string & dir) {
    if (dir.length () < 1)
        return false;

    if (access (dir.c_str (), W_OK) != 0)
        return false;

    string base = dir;
    if (dir[dir.length ()-1] != '/')
        base += '/';

    string control = base + "iPod_Control";
    if (access (control.c_str (), F_OK) != 0 && mkdir (control.c_str (), 0777) != 0)
        return false;

    string itunes = control + "/iTunes";
    if (access (itunes.c_str (), F_OK) != 0 && mkdir (itunes.c_str (), 0777) != 0)
        return false;

    string music = control + "/Music";
    if (access (music.c_str (), F_OK) != 0 && mkdir (music.c_str (), 0777) != 0)
        return false;

    for (int i = 0; i < 20; i++) {
        string music_dir = music + "/F";
        if (i < 10) //I need sprintf
            music_dir += string ("0") + (char)(i+'0');
        else
            music_dir += string ("1") + (char)((i%10) + '0');

        if (access (music_dir.c_str (), F_OK) != 0 && mkdir (music_dir.c_str (), 0777) != 0)
            return false;
    }

    return true;
}

Track* FUSEPod::upload_song (const string & path, bool copy) {
    struct stat st;

    if (stat (path.c_str (), &st))
        return 0;

    off_t size = st.st_size;

    Track * track = itdb_track_new ();
    if (!track)
        return 0;

    track->size = (gint32) size;

    //Set the filetype
    char types [7][2][5] = { {"wav", "wav"},
                             {"mp3", "mpeg"},
                             {"mpeg", "mpeg"},
                             {"mp4", "mp4"},
                             {"aac", "mp4"},
                             {"m4a", "mp4"},
                             {"m4p", "mp4"} };

    unsigned int pos = path.rfind ('.');
    if (pos == string::npos) {
        itdb_track_free (track);
        return 0;
    }

    track->filetype = 0;
    string ext = string (path, pos + 1);
    for (int i = 0; i < 7; i++) {
        if (strcasecmp (ext.c_str (), types [i][0]) == 0)
            track->filetype = g_strdup (types [i][1]);
    }

    if (!track->filetype) {
        itdb_track_free (track);
        return 0;
    }

    //Get Tags
    TagLib::FileRef f(path.c_str ());
    if(!f.isNull() && f.tag()) {

      TagLib::Tag *tag = f.tag();
      
      track->title    = g_strdup (tag->title   ().toCString (true));
      track->artist   = g_strdup (tag->artist  ().toCString (true));
      track->album    = g_strdup (tag->album   ().toCString (true));
      track->comment  = g_strdup (tag->comment ().toCString (true));
      track->genre    = g_strdup (tag->genre   ().toCString (true));
      track->year     = (gint32)  tag->year();
      track->track_nr = (gint32)  tag->track();
    } else {
        itdb_track_free (track);
        return 0;
    }

    if(f.audioProperties()) {
      TagLib::AudioProperties *props = f.audioProperties();

      track->bitrate    = (gint32) props->bitrate();
      track->samplerate = (gint32) props->sampleRate();
      track->tracklen   = (gint32) props->length()*1000;
    }

    //Add too iTunesDB
    itdb_track_add (this->ipod, track, -1);

    //Add too master playlist
    Playlist *mpl = itdb_playlist_mpl (this->ipod);
    if(!mpl) {
        mpl = itdb_playlist_new ("FUSEPod", false);
        itdb_playlist_add (this->ipod, mpl, -1);
        itdb_playlist_set_mpl (mpl);
    }
    itdb_playlist_add_track (mpl, track, -1);

    if (copy) {
        //Copy across
        gchar * tmp = g_strdup (path.c_str ());
        if (!itdb_cp_track_to_ipod (track, tmp, 0)) {
            g_free (tmp);
            itdb_track_free (track);
            return 0;
        }
        g_free (tmp);
    }
    else if (!this->move_file (path, track)) { //Move
        itdb_track_free (track);
        return 0;
    }


    this->num_tracks++;

    return track;
}

bool FUSEPod::remove_song (const string & path) {
    string real_path = this->get_real_path (path);

    if (real_path != "" && unlink (real_path.c_str ())) //removes if file exists
        return false;
    
    Track * track = this->get_node (path.c_str ())->value.track;

    for (size_t i = 0; i < paths_descs.size (); i++) 
        remove_track (track, paths_descs [i]);

    /* TODO There has to be a better way than this to remove the track
     * from the playlists in fusepod. */
    Node * pnode = get_node (dir_playlists.c_str ());
    for (Node::iterator p = pnode->begin (); p != pnode->end (); ++p) {
        for (Node::iterator t = (*p)->begin (); t != (*p)->end (); ++t) {
            if ((*t)->value.track == track) {
                (*p)->children.erase (t);
                delete *t;
            }
        }
    }

    //Remove from playlists
    for (GList * i = ipod->playlists; i; i=i->next)
        itdb_playlist_remove_track ((Playlist*)i->data, track);

    itdb_track_remove (track);

    this->num_tracks--;

    return true;
}

void FUSEPod::remove_playlist (const string & name) {
    for (GList * i = this->ipod->playlists; i; i=i->next) {
        Playlist * playlist = (Playlist*)i->data;

        if (itdb_playlist_is_mpl (playlist)) //Master playlist
            continue;
        
        const char * pname = fusepod_get_string (fusepod_check_string (playlist->name).c_str ());

        if (name != pname)
            continue;

        Node * node = this->get_node ((dir_playlists + "/" + name).c_str ());
        node->parent->children.erase (node);
        delete node;

        itdb_playlist_remove (playlist);

        this->num_playlists--;

        break;
    }
}

bool FUSEPod::flush () {
    if (this->syncing)
        return false;

    this->syncing = true;

    if (!itdb_write (this->ipod, 0)) {
        this->syncing = false;
        return false;
    }

    add_playlists ();
    add_all_tracks ();

    this->syncing = false;

    return true;
}

Node * FUSEPod::get_node (const char * path) {
    Node * cur = root;
    char * tmp = strdup (path);
    vector<char*> paths = fusepod_split_path (tmp, '/');
    NodeValue node (0);
    
    for (unsigned int i = 0; i < paths.size () && cur; i++) {
        node.text = paths [i];
        cur = cur->find (node);
    }
    
    free (tmp);
    return cur;
}

string FUSEPod::get_real_path (const NodeValue & nv) {
    if (!S_ISREG (nv.mode))
        return "";

    gchar * tmp = itdb_filename_on_ipod (nv.track);
    if (!tmp)
        return "";

    string real_path = string (tmp);
    g_free (tmp);

    return real_path;
}

string FUSEPod::get_real_path (const string & path) {
    Node * node = this->get_node (path.c_str ());
    if (node)
        return this->get_real_path (node->value);
    else
        return "";
}

string FUSEPod::get_transfer_path (const char * path) {
    return mount_point + '/' + dir_transfer_ipod + &(path[dir_transfer.size () + 1]);
}

string FUSEPod::get_statistics () {
    using std::endl;
    std::ostringstream stats;

    stats << "FUSEPod Version: " << PACKAGE_VERSION << endl;
    stats << "ITunesDB Version: " << this->ipod->version << endl;
    stats << "Real Mountpoint: " << itdb_get_mountpoint(this->ipod) << endl;

    Playlist * mpl = itdb_playlist_mpl (ipod);
    stats << "iPod Name: " << (mpl && mpl->name ? string(mpl->name) : "Unknown") << endl; 

    stats << "Track Count: " << this->num_tracks << endl;

    int count = this->num_playlists;
    if (mpl)
        count--;
    stats << "Playlist Count: " << count << endl;

    return stats.str ();
}

void FUSEPod::add_track (Track * track) {
    for (unsigned int a = 0; a < paths_descs.size (); a++)
        add_track (track, paths_descs [a]);
}

string FUSEPod::get_track_val (Track * track, char symbol) {
    string val = "Unknown";
    size_t pos;

    switch (symbol) {
        case 'a': // Artist
            if (track->artist)
                val = track->artist;
            break;
        case 'c': // Artist without compilations
            if (track->compilation) {
		    val = "Compilations"; 
	    } else {
		if (track->artist) 
		    val = track->artist;
	    }		
            break;
        case 'A': // Album
            if (track->album)
                val = track->album;
            break;
        case 't': // Title
            if (track->compilation) {
		    val = string(track->artist) + " - " + string(track->title); //Is it works? 
	    } else {
		if (track->title)
		    val = track->title;
	    }		
            break;
        case 'g': // Genre
            if (track->genre)
                val = track->genre;
            break;
        case 'e': // Extension
            val = string (track->ipod_path);
            pos = val.rfind (".");
            val = pos == string::npos ? "mp3" : string (val, pos + 1);
            break;
        case 'T': // Track
            val = fusepod_int_to_string (track->track_nr);
            val = val.size () < 2 ? "0" + val : val;
            break;
        case 'y': // Year
            val = fusepod_int_to_string (track->year);
            break;
        case 'r': // Rating
            val = fusepod_int_to_string (track->rating / ITDB_RATING_STEP);
            break;
        default:
            std::cerr << "Unrecognized symbol " << symbol << ".\n";
    }
    
    return val;
}

string FUSEPod::expand_string (Track * track, const string & format) {
    string ret = "";

    char * tmp = strdup (format.c_str ());
    vector<char*> tokens = fusepod_split_path (tmp, '%');
    
    if (tmp == tokens [0])
        ret += tokens [0];
    else
        ret += fusepod_check_string (get_track_val (track, tokens [0][0])) + (&tokens [0][1]);
    
    for (size_t a = 1; a < tokens.size (); a++) {
        ret += fusepod_check_string (get_track_val (track, tokens [a][0])) + (&tokens [a][1]);
    }

    free (tmp);

    return ret;
}

void FUSEPod::add_track (Track * track, const string & path_desc) {
    char * tmp = strdup (path_desc.c_str ());
    vector<char*> paths = fusepod_split_path (tmp, '/');
    Node * node = root;

    for (unsigned int i = 0; i < paths.size (); i++) {
        string path = expand_string (track, paths [i]);

        NodeValue nv (fusepod_get_string (fusepod_check_string (path.c_str ()).c_str ()), MODE_DIR);
        if (i == paths.size () - 1) {
            nv.mode = MODE_FILE;
            nv.track = track;
            nv.size = track->size;
        }

        Node * n = node->find (nv);
        if (n == 0)
            n = node->addChild (nv);
        node = n;
    }

    free (tmp);
}

void FUSEPod::remove_track (Track * track, const string & path_desc) {
    char * tmp = strdup (path_desc.c_str ());
    vector<char*> paths = fusepod_split_path (tmp, '/');
    Node * node = root;

    for (unsigned int i = 0; i < paths.size (); i++) {
        string path = expand_string (track, paths [i]);

        Node * n = node->find (NodeValue (fusepod_get_string (fusepod_check_string (path.c_str ()).c_str ())));
        if (n == 0)
            break;
        node = n;
    }

    if (node != 0 && node != root && !S_ISDIR (node->value.mode)) {
        while (node->parent->children.size () == 1) //Deletes all directories that will become empty.
            node = node->parent;

        node->parent->children.erase (node);
        delete node;
    }

    free (tmp);
}

void FUSEPod::add_playlists () {
    NodeValue pnv (fusepod_get_string (dir_playlists.c_str ()), MODE_DIR);
    Node * pnode = root->addChild (pnv);
    if (pnode == 0)
        pnode = root->find (pnv);
    
    for (GList * i = this->ipod->playlists; i; i=i->next) {
        Playlist * playlist = (Playlist*) i->data;

        if (itdb_playlist_is_mpl (playlist)) //Master playlist
            continue;
        
        const char * pname = fusepod_get_string (fusepod_check_string (playlist->name).c_str ());
        cout << "Adding playlist " << pname << endl;

        NodeValue nv (pname, MODE_DIR);
        Node * node = pnode->addChild (nv);
        if (node == 0)
            node = pnode->find (nv);

        size_t pos_len = 1;
        int tmp = playlist->num;
        while ((tmp /= 10) > 0) //For padding track number with 0's
            pos_len++;

        tmp = 1;
        for (GList * a = playlist->members; a; a=a->next) {
            Track * track = (Track*)a->data;

            string pos = fusepod_int_to_string ((int)tmp++);
            while (pos.length () < pos_len)
                pos = "0" + pos;

            string filename = pos + " - " + fusepod_check_string (expand_string (track, playlist_track_format));

            node->addChild (NodeValue (fusepod_get_string (filename.c_str()), MODE_FILE, track,
                                       track->size));
        }
    }
}

void FUSEPod::add_all_tracks () {
    for (GList * i = this->ipod->tracks; i; i=i->next) {
        Track * track = (Track*)i->data;

        for (unsigned int a = 0; a < paths_descs.size (); a++)
            add_track (track, paths_descs [a]);
    }
}

/**
 * Adds all files that are not in the itdb but are in the music dir
 */
void FUSEPod::add_orphaned_tracks () {
    //TODO FIX THIS CODE
    if (true)
        return;
    set<string> files_in_itdb;

    // Iterate through all the tracks in the iTunesDB. Add these to files_in_itdb
    for (GList * i=ipod->tracks; i; i=i->next) {
        Track * track = (Track*) i->data;
        
        if (track->ipod_path) {
            gchar * str = strdup (track->ipod_path);
            int pos = strlen (str);
            int count = 0;

            for (; pos >= 0 && count < 2; pos--)
                if (str[pos] == ':') {
                    count++;
                    str [pos] = '/';
                }

            pos++;

            string s = &(str[pos]);
            free (str);

            if (count != 2)
                continue;

            files_in_itdb.insert (s);
        }
    }

    DIR * music = opendir ( (mount_point + "/iPod_Control/Music").c_str() );

    struct dirent * dir_ent;

    // Now traverse the Music directory looking at each mp3
    while ((dir_ent = readdir(music))) {
        if (strcmp(dir_ent->d_name, ".")  == 0 ||
            strcmp(dir_ent->d_name, "..") == 0)
            continue;

        DIR * subdir = opendir ( (mount_point + "/iPod_Control/Music/" + dir_ent->d_name).c_str() );
        
        if (!subdir)
            continue;

        struct dirent * subdir_ent;
        
        // These files should be mp3s in the db. If they are not in files_in_itdb,
        // the song gets added to the database.
        while ((subdir_ent = readdir(subdir))) {
            if (strcmp(subdir_ent->d_name, ".")  == 0 ||
                strcmp(subdir_ent->d_name, "..") == 0)
                continue;

            string path = "/" + string(dir_ent->d_name) + "/" + subdir_ent->d_name;

            if (files_in_itdb.find (path) == files_in_itdb.end ()) {
                upload_song (mount_point + "/iPod_Control/Music" + path, false);
                cout << path << " is not in the iTunesDB. Adding" << endl;
            }
        }

        closedir (subdir);
    }

    closedir (music);
}

bool FUSEPod::move_file (const string & path, Track * track) {
    string subdir = mount_point + "/iPod_Control/Music/F";
    string ext (path, path.rfind('.')+1, path.size()-1);
    char * tmp = new char [subdir.size () + 3];
    bool found = false;
    int musicdirs = itdb_musicdirs_number (ipod);
    int dirnum = rand () % musicdirs;
    struct stat st;

    //Find a directory that exists from dirnum -> end
    for (int i = dirnum; i < musicdirs && !found; i++) {
        sprintf (tmp, "%s%02d", subdir.c_str(), i);
        if ((found = !stat(tmp, &st)))
            dirnum = i;
    }

    //Find a directory that exists from 0 -> dirnum
    for (int i = 0; i < dirnum && !found; i++) {
        sprintf (tmp, "%s%02d", subdir.c_str(), i);
        if ((found = !stat(tmp, &st)))
            dirnum = i;
    }

    subdir = string (tmp) + "/fusepod";
    delete tmp;

    if (!found)
        return false;

    tmp = new char [subdir.size () + 8 + ext.size ()];
    int filenum;

    //Search for unused file
    do {
        filenum = rand () % 1000000;
        sprintf (tmp, "%s%06d.%s", subdir.c_str(), filenum, ext.c_str());
    } while (!stat (tmp, &st)); //Returns false when found file

    sprintf (tmp, ":iPod_Control:Music:F%02d:fusepod%06d.%s", dirnum, filenum, ext.c_str ());
    track->ipod_path = g_strdup (tmp);

    sprintf (tmp, "%s/iPod_Control/Music/F%02d/fusepod%06d.%s", mount_point.c_str(), dirnum, filenum, ext.c_str ());
    if (rename (path.c_str(), tmp)) {
        delete tmp;
        return false;
    }

    delete tmp;
    return true;
}
