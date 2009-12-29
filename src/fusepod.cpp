/******************************* -*- C++ -*- *******************************
 *                                                                         *
 *   file            : fusepod.cpp                                         *
 *   date started    : 13 Feb 2006                                         *
 *   author          : Keegan Carruthers-Smith                             *
 *   email           : keegan.csmith@gmail.com                             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

extern "C" {
#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
}

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include "fusepod_ipod.h"
#include "fusepod_constants.h"
#include "fusepod_util.h"

using namespace std;

/* I know globals are regarded as bad, but I'm lazy */
static FUSEPod * fusepod;
static string fuse_mount_point;
static string ipod_mount_point;
static string sync_script;
static string add_files_script;
static char * add_songs;
static bool syncing;
static string syncing_file;
static string currently_syncing;


/** Returns true if the transfer directory is a prefix of path */
inline static bool transfer_in_dir (const char * path) {
    return fusepod_starts_with (&(path[1]), dir_transfer.c_str ());
}

static void transfer_remove (const char * path) {
    Node * node = fusepod->get_node (path);

    if (!node)
        return;

    assert (S_ISREG (node->value.mode));

    node->remove_from_parent ();

    string realpath = fusepod->get_transfer_path (path);

    free ((void*)node->value.text);
    delete node;

    unlink (realpath.c_str());
}

static void transfer_add_songs (Node * node, const string & path) {
    cout <<"Adding files in directory" << path << endl;
    for (Node::iterator it = node->begin(); it != node->end(); ++it) {
        Node * n = *it;
        string new_path = path + "/" + n->value.text;

        if (S_ISDIR(n->value.mode)) {
            transfer_add_songs (n, new_path);
        } else if (!(n->value.mode & S_IWUSR)) {
            //Files that have finished copying are marked not writable
            currently_syncing = fusepod->get_transfer_path (new_path.c_str());
            cout << "Adding track " << currently_syncing << "... ";
            if (fusepod->upload_song (currently_syncing, false))
                cout << "Successful" << endl;
            else
                cout << "Failed" << endl;

            transfer_remove (new_path.c_str());
        }
    }
}

static void transfer_remove_empty_dirs (Node * node, const string & path, bool can_delete=true) {
    for (Node::iterator it = node->begin(); it != node->end(); ++it)
        if (S_ISDIR((*it)->value.mode))
            transfer_remove_empty_dirs (*it, path + "/" + (*it)->value.text);

    if (node->begin() != node->end() || !can_delete ||
        rmdir(fusepod->get_transfer_path (path.c_str()).c_str())) // not empty || cannot delete || can't delete real dir
        return;

    node->remove_from_parent ();

    free ((void*)node->value.text);
    delete node;
}


/** Returns fusepod->get_statistics with syncing info */
static string fusepod_get_stats () {
    if (syncing) /* Add syncing stats */
        return fusepod->get_statistics () +
            "Currently Syncing: " + currently_syncing + "\n";
    else
        return fusepod->get_statistics ();
}

static int fusepod_getattr (const char *path, struct stat *stbuf) {
    Node * tn = fusepod->get_node (path);
    if (tn == 0)
        return -ENOENT;

    if (filename_add == &(path[1])) {
        if (stat (add_songs, stbuf) != 0)
            return -errno;
        return 0;
    }

    /* Update size for statistics file */
    if (filename_stats == &(path[1]))
        tn->value.size = fusepod_get_stats ().length ();

    /* Update size for files in the ipod's transfer dir */
    if (transfer_in_dir (path)) {
        struct stat st;
        stat (fusepod->get_transfer_path (path).c_str (), &st);
        tn->value.size = st.st_size;
    }

    memset (stbuf, 0, sizeof (struct stat));
    stbuf->st_uid   = getuid ();
    stbuf->st_gid   = getgid ();
    stbuf->st_mode  = tn->value.mode;
    stbuf->st_size  = 4096;
    stbuf->st_nlink = 1;

    if (tn->value.mode == MODE_DIR)
        stbuf->st_nlink = tn->value.size + 2; /* +2 for .. and . */
    else
        stbuf->st_size = tn->value.size;

    return 0;
}

static int fusepod_access (const char *path, int mask) {
    int res;

    struct stat st;
    res = fusepod_getattr (path, &st);
    if (res != 0)
        return res;

    if ((st.st_mode | mask) != st.st_mode)
        return -EROFS;

    return 0;
}

static int fusepod_readdir (const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    Node * tn = fusepod->get_node (path);
    if (tn == 0 || (tn->value.mode & S_IFREG))
        return -ENOENT;

    filler(buf, ".", 0, 0);
    filler(buf, "..", 0, 0);

    for (Node::iterator i = tn->begin (); i != tn->end (); ++i)
        filler (buf, (*i)->value.text, 0, 0);

    return 0;
}

static int fusepod_open(const char *path, struct fuse_file_info *fi) {
    Node * tn = fusepod->get_node (path);
    if (tn == 0)
        return -ENOENT;

    string realpath;

    if (filename_add == &(path[1])) //The special file containing songs to sync
        realpath = add_songs;
    else if (transfer_in_dir (path)) //File in transfer directory
        realpath = fusepod->get_transfer_path (path);
    else if (tn->value.track) //A song
        realpath = fusepod->get_real_path (tn->value);
    else
        return 0;

    int res = open(realpath.c_str(), fi->flags);
    if (res == -1)
        return -errno;

    fi->fh = res;
    close (res);

    return 0;
}

static int fusepod_truncate (const char * path, off_t offset) {
    string realpath;

    if (filename_add == &(path[1]))
        realpath = add_songs;
    else if (transfer_in_dir (path))
        realpath = fusepod->get_transfer_path (path);
    else
        return -EACCES;

    if (truncate (realpath.c_str(), offset) != 0)
        return -errno;

    return 0;
}

static int fusepod_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    string real_path;

    if (filename_add == &(path[1]))
        real_path = add_songs;
    else if (transfer_in_dir (path))
        real_path = fusepod->get_transfer_path (path);
    else
        return -EACCES;

    int fd;
    int res;

    (void) fi;

    fd = open (real_path.c_str (), O_WRONLY);
    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int fusepod_read_string (const string & str, char *buf, size_t size, off_t offset) {
    if (offset >= str.length ())
        return -EINVAL;

    int bytes_read = min ((int) (str.length () - offset), (int) size);
    memcpy (buf, &(str.c_str ()[offset]), bytes_read);
    return bytes_read;
}

static int fusepod_read (const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int fd;
    int res;

    Node * tn = fusepod->get_node (path);
    if (tn == 0)
        return -ENOENT;
    else if (!(tn->value.mode & S_IFREG))
        return -EACCES;


    /* Checking if reading in memory files */
    if (filename_sync == &(path[1]))
        return fusepod_read_string (sync_script, buf, size, offset);

    else if (filename_add_files == &(path[1]))
        return fusepod_read_string (add_files_script, buf, size, offset);

    else if (filename_stats == &(path[1]))
        return fusepod_read_string (fusepod_get_stats (), buf, size, offset);


    /* Otherwise check if file in really another file on the filesystem */
    string realpath;
    if (filename_add == &(path[1]))
        realpath = add_songs;
    else if (transfer_in_dir (path))
        realpath = fusepod->get_transfer_path (path);
    else
        realpath = fusepod->get_real_path (tn->value).c_str ();


    (void) fi;
    fd = open(realpath.c_str(), O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int fusepod_mknod (const char * path, mode_t mode, dev_t dev) {
    if (filename_sync_do == &(path [1]) && !syncing) {
        /* Synchronize iPod */
        syncing = true;
        ifstream in (add_songs);
        string path;

        /* Read songs from add_songs */
        while (!in.eof ()) {
            getline (in, path);
            fusepod_strip_string (path);

            struct stat st;
            stat (path.c_str (), &st);

            if (S_ISREG(st.st_mode)) {
                cout << "Adding track " << path << "... ";
                currently_syncing = path;
                if (fusepod->upload_song (path))
                    cout << "Successful" << endl;
                else
                    cout << "Failed" << endl;
            }
        }

        in.close ();

        /* Empty add_songs file */
        truncate (add_songs, 0);

        /* Recursively add songs in transfer */
        /*string transfer_path = "/" + dir_transfer;
        Node * transfer_node = fusepod->get_node ( ("/" + dir_transfer).c_str() );

        transfer_add_songs (transfer_node, transfer_path);
        transfer_remove_empty_dirs (transfer_node, transfer_path, false);*/

        currently_syncing = "iTunesDB";
        cout << "Syncing database... ";
        fusepod->flush ();
        cout << "Successful" << endl;

        syncing = false;
        currently_syncing = "";
        return 0;
    }

    if (!transfer_in_dir (path))
        return -EACCES;

    /* Making a file in the transfer directory */

    if (!S_ISREG (mode))
        return -EPERM;

    string ipod_path = fusepod->get_transfer_path (path);

    if (mknod (ipod_path.c_str (), S_IFREG | 0666, 0))
        return -errno;

    struct stat st;
    stat (ipod_path.c_str (), &st);

    Node * parent = fusepod->root;

    char * split_path = strdup (path);
    vector<char*> components = fusepod_split_path (split_path, '/');

    for (size_t i = 0; i < components.size ()-1; i++)
        parent = parent->find (components [i]);

    //NOTE: filename string will have to be freed when syncing
    NodeValue nv (strdup (components[components.size () -1]), st.st_mode, 0, st.st_size);

    free (split_path);

    parent->addChild (nv);

    return 0;
}

static int fusepod_mkdir (const char * path, mode_t mode) {
    //TODO: Add support for adding playlists
    Node * node = fusepod->get_node (path);

    if (node != 0)
        return -EEXIST;

    if (!transfer_in_dir (path))
        return -EACCES;

    string realpath = fusepod->get_transfer_path (path);

    if (mkdir (realpath.c_str(), S_IFDIR | 0777))
        return -errno;

    string parent   (path, 0, string(path).rfind ('/'));
    string filename (path, string(path).rfind('/')+1, strlen(path)-1);

    node = fusepod->get_node (parent.c_str());
    node->addChild (NodeValue (strdup(filename.c_str()), S_IFDIR | 0777));

    return 0;
}

static int fusepod_rmdir (const char * path) {
    Node * node = fusepod->get_node (path);

    if (node == 0)
        return -ENOENT;

    //User can only remove directories in playlist or transfer directories
    if (node->parent && dir_playlists == node->parent->value.text) {

        fusepod->remove_playlist (string (node->value.text));

    }
    else if (transfer_in_dir (path)) {

        if (!S_ISDIR(node->value.mode))
            return -ENOTDIR;

        if (node->begin() != node->end())
            return -ENOTEMPTY;

        if (rmdir (fusepod->get_transfer_path (path).c_str()))
            return -errno;

        node->remove_from_parent ();

        free ((void*)node->value.text);
        delete node;

    }
    else
        return -EACCES;

    return 0;
}

#define XATTRS gint32 _playcount = (gint32) track->playcount; \
    gint32 _rating = track->rating / 20; \
    const void * xattrs [14][2] = { \
        {"tag.title",            track->title}, \
        {"tag.artist",           track->artist}, \
        {"tag.album",            track->album}, \
        {"tag.genre",            track->genre}, \
        {"tag.comment",          track->comment}, \
        {"tag.composer",         track->composer}, \
        {"tag.description",      track->description}, \
        {"tag.podcasturl",       track->podcasturl}, \
        {"tag.podcastrss",       track->podcastrss}, \
\
        {"tag.track",            &track->track_nr}, \
        {"tag.length",           &track->tracklen}, \
        {"tag.year",             &track->year}, \
        {"tag.playcount",        &_playcount}, \
        {"tag.rating",           &_rating} \
    };

#define XATTRS_LEN 14
#define XATTRS_NUM 9


static int fusepod_listxattr (const char * path, char * attrs, size_t size) {
    Node * node = fusepod->get_node (path);
    if (!node)
        return -ENOENT;

    if (!node->value.track)
        return 0;

    size_t count = 0, pos = 0;
    Track * track = node->value.track;

    XATTRS

    for (int i = 0; i < XATTRS_LEN; i++) {
        if (i < XATTRS_NUM && !xattrs [i][1])
            continue;

        size_t len = strlen ((char*)xattrs [i][0]) + 1;

        if (size == 0) {
            count += len;
            continue;
        }

        if (len > size + pos)
            return -ERANGE;

        memcpy (attrs + pos, xattrs [i][0], len);
        pos += len;
    }

    return size == 0 ? count : pos;
}

static int fusepod_getxattr (const char * path, const char * attr, char * buf, size_t size) {
    Node * node = fusepod->get_node (path);
    if (!node)
        return -ENOENT;

    if (!node->value.track)
        return 0;

    Track * track = node->value.track;

    XATTRS

    for (int i = 0; i < XATTRS_LEN; i++) {
        if (strcmp ((char*)xattrs [i][0], attr) != 0)
            continue;

        if (!xattrs [i][1])
            return -EACCES; //-ENOATTR;

        char * val;

        if (i < XATTRS_NUM) {
            val = (char*)xattrs[i][1];
        }
        else {
            val = new char [11]; //Max length of gint32 string is 10
            snprintf (val, 11, "%d", *(gint32*)(xattrs[i][1]));
            val [10] = 0;
        }

        size_t len = strlen (val) + 1;

        if (size == 0) {
            if (i >= XATTRS_NUM) delete val;
            return len;
        }

        if (len > size) {
            if (i >= XATTRS_NUM) delete val;
            return -ERANGE;
        }

        strcpy (buf, val);

        if (i >= XATTRS_NUM)
            delete val;

        return len;
    }

    return -EACCES; //-ENOATTR;
}

static int fusepod_statfs (const char * path, struct statvfs * vfs) {
    int ret = statvfs (fusepod->mount_point.c_str (), vfs);

    if (ret != 0)
        return -ret;

    return ret;
}

static int fusepod_unlink (const char * path) {
    Node * node = fusepod->get_node (path);
    if (node == 0)
        return -ENOENT;

    if (S_ISDIR (node->value.mode))
        return -EISDIR;

    if (transfer_in_dir (path)) {
        transfer_remove (path);
        return 0;
    }

    if (!node->value.track)
        return -EACCES;

    if (!fusepod->remove_song (path))
        return -EACCES;

    return 0;
}

static int fusepod_release (const char * path, struct fuse_file_info * info) {
    if (!transfer_in_dir (path))
        return 0;

    /* Makes files in the Transfer directory read-only */
    Node * node = fusepod->get_node (path);

    if (node)
        node->value.mode = S_IFREG | 0444;

    //TODO: Add the songs here
    currently_syncing = fusepod->get_transfer_path (path);
    cout << "Adding track " << currently_syncing << "... ";
    Track * track = fusepod->upload_song (currently_syncing, false);
    if (track)
        cout << "Successful" << endl;
    else
        cout << "Failed" << endl;

    transfer_remove (path);

    if (track)
        fusepod->add_track (track);

    return 0;
}

static void write_default_config () {
    cout << "FUSEPod configuration file not found. Writing it now" << endl;

    if (!getenv ("HOME"))
        return;

    string home = getenv ("HOME");
    home += "/.fusepod";

    ofstream config (home.c_str ());

    if (!config)
        return;

    config << default_config_file;

    config.close ();
}

static vector<string> get_string_desc () {
    istream * config = 0;

    if (getenv ("HOME")) {
        string home = getenv ("HOME");
        home += "/.fusepod";

        struct stat st;
        stat (home.c_str (), &st);

        if (!S_ISREG(st.st_mode))
            write_default_config ();

        config = new ifstream (home.c_str ());
    }

    if (config)
        cout << "Reading configuration file " << endl;
    else
        config = new istringstream (default_config_file);

    vector<string> paths;
    string line;

    while (!config->eof ()) {
        getline (*config, line);
        line = fusepod_strip_string (line);
        if (line.length () > 0 && line [0] == '/')
            paths.push_back (line);
    }

    delete config;

    return paths;
}

void * fusepod_init () {
    syncing = false;
    syncing_file = "";

    srand (time (0)); //Random is used in FUSEPod::generate_filename()

    vector<string> pd = get_string_desc ();

    cout << "Reading iPod at " << ipod_mount_point << endl;
    fusepod = new FUSEPod (ipod_mount_point, pd);
    cout << "Finished reading iPod" << endl;

    /* These are special extensions to the filesystem for adding songs to the iPod*/
    add_songs = strdup ("/tmp/fusepodXXXXXX");
    mkstemp (add_songs);

    /* Create an empty temp file for add_songs */
    mknod (add_songs, S_IFREG | 0666, 0);

    NodeValue add_song (fusepod_get_string (filename_add.c_str ()), S_IFREG | 0666);
    fusepod->root->addChild (add_song);

    NodeValue sync_ipod (fusepod_get_string (filename_sync.c_str ()), S_IFREG | 0555);

    /* Make sync script */
    sync_script =  "#!/bin/sh\n";
    sync_script += "#FUSEPod sync script\n";
    sync_script += "echo Syncing iPod...\n";
    sync_script += "if [ \"$1\" = '-watch' ]; then\n";
    sync_script += "    stats='" + fuse_mount_point + "/" + filename_stats + "'\n";
    sync_script += "    initial=$(grep 'Track Count' \"$stats\" | cut -b 14-)\n";
    sync_script += "    count=$(grep -c '^.*$' '" + fuse_mount_point + "/" + filename_add + "')\n";
    sync_script += "    touch " + fuse_mount_point + "/" + filename_sync_do + " >/dev/null 2>&1 &\n";
    sync_script += "    sleep 0.2\n";
    sync_script += "    while [ 1 ]; do\n";
    sync_script += "        current=$(grep 'Track Count' \"$stats\" | cut -b 14-)\n";
    sync_script += "        file=$(grep 'Currently Syncing' \"$stats\")\n";
    sync_script += "        if [ $? != 0 ]; then break; fi\n";
    sync_script += "        clear && echo $file && echo Track $[$current-$initial] of \"$count\" && sleep 0.2\n";
    sync_script += "    done\n";
    sync_script += "elif [ $# = 0 ]; then touch " + fuse_mount_point + "/" + filename_sync_do + " >/dev/null 2>&1\n";
    sync_script += "else echo USAGE: $0 '[ -watch ]'\n";
    sync_script += "fi\n";
    sync_script += "echo Finished syncing iPod\n";

    sync_ipod.size = sync_script.length ();
    fusepod->root->addChild (sync_ipod);

    NodeValue add_files (fusepod_get_string (filename_add_files.c_str ()), S_IFREG | 0555);

    /* Make Recursive Directory Add script */
    add_files_script =  "#!/bin/sh\n";
    add_files_script += "#FUSEPod Recursive Directory Add\n";
    add_files_script += "if [ $# = 0 ]; then echo \"USAGE: $0 [ file or directory ] ...\"; exit 1; fi\n";
    add_files_script += "for file in \"$@\"; do\n";
    add_files_script += "    echo $file | grep ^/ &> /dev/null\n";
    add_files_script += "    if [ $? != 0 ]; then file=$PWD/$file; fi\n";
    add_files_script += "    find \"$file\" | egrep -i '(wav|mp3|m4a)$' >> '" + fuse_mount_point + "/" + filename_add + "'\n";
    add_files_script += "done\n";

    add_files.size = add_files_script.length ();
    fusepod->root->addChild (add_files);

    /* Add statistics file */
    NodeValue stats (fusepod_get_string (filename_stats.c_str ()), S_IFREG | 0444);
    fusepod->root->addChild (stats);

    /* Add transfer directory */
    string transfer_dir = fusepod->mount_point + "/" + dir_transfer_ipod;
    if (!mkdir(transfer_dir.c_str(), S_IFDIR | 0777) || errno == EEXIST) {
        NodeValue transfer(fusepod_get_string(dir_transfer.c_str()), S_IFDIR | 0777);
        fusepod->root->addChild(transfer);
    }

    cout << "Starting FUSE layer" << endl;

    return 0;
}

static void fusepod_destroy (void * v) {
    cout << "Cleaning up" << endl;

    if (add_songs)
        remove(add_songs);

    if (fusepod)
        delete fusepod;

    add_songs = 0;
    fuspod = 0;
}

static struct fuse_operations fusepod_oper;

int main (int argc, char **argv) {
    //TODO Apparantly error messages aren't clear enough
    if (argc > 1) {
        /* This finds out were the mount point is so that any app can use it.
           Note that it may contain ../ and ./ in it */
        fuse_mount_point = argv [argc-1];
        if (fuse_mount_point [0] != '/') {
            if (!getenv ("PWD")) {
                cerr << "ERROR: Please supply the PWD environment variable or an absolute mount point" << endl;
                exit (1);
            }
            fuse_mount_point = string (getenv ("PWD")) + "/" + fuse_mount_point;
        }

        /* Fixes subtle bug of when FUSE freezes when it doesn't find the iPod directory.
           Check for help option. If not found and ipod cannot be found, exit.*/
        bool found = false;
        for (int i = 1; i < argc && !found; i++)
            if (strcmp (argv [i], "--help") == 0 || strcmp (argv [i], "-h") == 0)
                found = true;

        ipod_mount_point = FUSEPod::discover_ipod ();

        if (!found && ipod_mount_point == "") {
            cerr << "ERROR: Cannot find the iPod mount point.\n";
            cerr << "This may happen because the iPod is not mounted or you have not created an itunes database yet\n";
            cerr << "Please specifiy the mount point through the enviroment variable IPOD_DIR\n";
            cerr << "Eg: IPOD_DIR=\"/media/ipod\" fusepod /home/keegan/myipod\n";
            cerr << flush;
            exit (1);
        }

        char * ipod_dir = getenv ("IPOD_DIR");
        if (!ipod_dir) ipod_dir = getenv ("IPOD_MOUNTPOINT");

        if (!found && ipod_dir && ipod_mount_point == ipod_dir &&
            access ( (string (ipod_dir) + ITUNESDB_PATH).c_str (), F_OK ) != 0) { // Checks for itunesdb
            cerr << "ERROR: Cannot find the iTunesDB in the directory specified by the IPOD_DIR or IPOD_MOUNTPOINT environment variable.\n";
            cerr << "Do you want to create the iTunesDB in the specified directory? (y/n): ";

            char ans;
            cin >> ans;
            if (ans != 'y') {
                cerr << "\nCannot run FUSEPod without iTunesDB.\nExiting...\n";
                exit (1);
            }

            if (!FUSEPod::create_itunes_dirs (string (ipod_dir))) {
                cerr << "\nERROR: Cannot create iTunesDB directory structure.\n";
                cerr << "Exiting...\n";
                exit (1);
            }
        }
    }

    fusepod_oper.getattr   = fusepod_getattr;
    fusepod_oper.listxattr = fusepod_listxattr;
    fusepod_oper.getxattr  = fusepod_getxattr;
    fusepod_oper.access    = fusepod_access;
    fusepod_oper.readdir   = fusepod_readdir;
    fusepod_oper.open      = fusepod_open;
    fusepod_oper.truncate  = fusepod_truncate;
    fusepod_oper.write     = fusepod_write;
    fusepod_oper.read      = fusepod_read;
    fusepod_oper.unlink    = fusepod_unlink;
    fusepod_oper.statfs    = fusepod_statfs;
    fusepod_oper.mknod     = fusepod_mknod;
    fusepod_oper.mkdir     = fusepod_mkdir;
    fusepod_oper.rmdir     = fusepod_rmdir;
    fusepod_oper.release   = fusepod_release;
    fusepod_oper.init      = fusepod_init;
    fusepod_oper.destroy   = fusepod_destroy;

    return fuse_main(argc, argv, &fusepod_oper);
}
