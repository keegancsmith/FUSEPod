/******************************* -*- C++ -*- *******************************
 *                                                                         *
 *   file            : fusepod_ipod.h                                      *
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

#ifndef _FUSEPOD_IPOD_H_
#define _FUSEPOD_IPOD_H_

extern "C" {
#include <gpod/itdb.h>
}

#include <string>
#include <cstring>
#include <set>
#include <vector>
#include <iostream>

using std::string;
using std::vector;
using std::set;

typedef Itdb_iTunesDB IPod;
typedef Itdb_Track Track;
typedef Itdb_Playlist Playlist;

struct NodeValue {
    NodeValue(const char *text = 0, mode_t mode = 0, Track *track = 0,
              off_t size = 0)
        : text (text), mode (mode), track (track), size (size) {}
    /** The nodes name. Please get this string from fusepod_get_string */
    const char *text;
    /** File mode. See manpage 2 stat. */
    mode_t mode;
    /** Pointer to libgpod Track item. If 0, Node is not a track */
    Track *track;
    /** File size in bytes. If directory, number of child directories */
    off_t size;
};

/**
 * The filesystem is kept in a tree. This is the node class, which is used for
 * each file/directory in the filesystem. It stores the NodeValue, parent node
 * and the child nodes (for directories).
 */
class Node {
    struct nodecomp {
        bool operator()(const Node *a, const Node *b) {
            return strcasecmp(a->value.text, b->value.text) < 0;
        }
    };
  public:
    /* Constructors/Destructors */
    Node(const NodeValue &value, Node *parent = 0)
        : value(value), parent(parent) {}
    ~Node();

    /* Typedefs */
    typedef set<Node*, nodecomp>::iterator iterator;

    /* Methods */

    /**
     * Searches the node's children.
     * The is method will return the node with matching NodeValue.text
     * If no node is found, the null pointer is returned.
     */
    Node *find(const NodeValue&);
    Node *find(const char*);

    /**
     * Adds a new Node as a child.
     * The new Node's value will be the value of the passed NodeValue.
     * This method returns the added Node.
     * If find (NodeValue) != 0; then this method will not add the child
     * and will return the null pointer.
     */
    Node *addChild (const NodeValue&);
    bool isLeaf() { return children.size() == 0; }
    iterator begin() { return children.begin(); }
    iterator end() { return children.end(); }

    void remove_from_parent();

    /* Operators */
    Node *operator[](const NodeValue &nv) { return this->find(nv); }

    /* Variables */
    /** The nodes value */
    NodeValue value;
    /** The parent node. Points to null if root of tree. */
    Node *parent;
    /** The child nodes */
    set<Node*, nodecomp> children;
};

/**
 * Abstracts iPod API and takes care of the virtual filesystem.
 */
class FUSEPod {
  public:
    FUSEPod(const string &mount_point, vector<string> path_descs);
    ~FUSEPod();

    /**
     * Finds a mounted iPod. It first look at the environment variable
     * IPOD_DIR. If that does not exist, it searches each mounted
     * directory for the itunesdb. If the string is empty, no path
     * was found.
     * @return A string pointing to an iPod directory.
     */
    static string discover_ipod();

    /**
     * This method will create the file layout necessary file libipod to
     * create and sync the ITunesDB.
     * @param dir The directory to create the folders in.
     * @return true on success
     */
    static bool create_itunes_dirs(const string &dir);

    /**
     * This function will upload a song to the iPod.
     * @param copy If false will move song instead.
     */
    Track *upload_song(const string &path, bool copy = true);

    /**
     * This function will remove a song from the FUSEPod filesystem.
     * This function will also remove the song from the inmemory
     * ITunesDB and remove the song from the iPod.
     * @return true if the operation was successful
     */
    bool remove_song(const string &path);

    /**
     * Removes a playlist from the FUSEPod filesystem aswell as the
     * inmemory ITunesDB.
     */
    void remove_playlist(const string &name);

    /**
     * This will flush the iPod and update the FUSEPod filesystem layout.
     * @return false if currently syncing
     */
    bool flush();

    /**
     * Returns the node corresponding to the path, or the null pointer.
     */
    Node *get_node(const char *path);

    /**
     * Returns the real path of a song.
     * This is the path to the song on the mounted iPod.
     * @return An absolute path to the real file, or an empty string
     */
    string get_real_path(const NodeValue &nv);
    string get_real_path(const string &path);

    /**
     * Returns the real path of a song in the transfer directory.
     */
    string get_transfer_path(const char *path);

    /**
     * This method returns a string with various statistics.
     * This method is meant for use as a /proc like file.
     * @return A multiline string with various statistics
     */
    string get_statistics();

    /**
     * Adds a track to FUSEPod filesystem layout.
     */
    void add_track(Track *track);

    IPod *ipod;
    Node *root;
    string mount_point;

  protected:
    string get_track_val(Track *track, char symbol);
    string expand_string(Track *track, const string &format);
    void add_track(Track *track, const string &path_desc);
    void remove_track(Track *track, const string &path_desc);

  private:
    vector<string> paths_descs;
    void add_playlists();
    void add_all_tracks();
    bool move_file(const string &path, Track *track);
    void add_orphaned_tracks();

    bool syncing;
    string syncing_file;
    int num_tracks;
    int num_playlists;
};

#endif
