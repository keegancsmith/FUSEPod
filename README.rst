=========
 FUSEPod
=========

:Version: 0.5
:Author: Keegan Carruthers-Smith <keegan.csmith@gmail.com>

https://bitbucket.org/keegan_csmith/fusepod
https://github.com/keegancsmith/FUSEPod

This is the sixth release of FUSEPod.

I would really appreciate it if you could send bug reports, feature request,
praise or dismissal my way at my email address above. I want to make the
program better. Also any patches are welcome. The issue tracker on github is
also a good place to report issues. Pull requests welcome as well.

Introduction
============

FUSEPod is a userspace filesystem which mounts your iPod into a directory
for easy browsing of your songs on your iPod.

Features
========

* Read and Write support
* Viewing/Removing playlists
* Configurable directory layout
* Transparent copying of files onto iPod
* Tracks have tags in extended attributes
* Discovers where your iPod is mounted
* Statistics file

Installation
============

This program should work with any kernel version that FUSE supports. You
require:

* FUSE_ 2.5
* libgpod (www.gtkpod.org) (Tested with 0.3.2 and 0.4.2)
* TagLib_ 1.4

Read the file `INSTALL` for instruction on installing.

To install the compile time dependencies on Ubuntu (tested on 12.10) run::

  $ sudo apt-get install libtag1-dev libgpod-dev libfuse-dev

To install the compile time dependencies on Nix run::

  $ nix-shell -p pkg-config fuse libgpod taglib

.. _FUSE: http://fuse.sourceforge.net/
.. _TagLib http://taglib.github.com/

Usage
=====

To mount your ipod type at the console::

  $ fusepod [mount_to]

If FUSEPod fails to find your iPod you can specify the iPod's mountpoint
through the environment variable IPOD_DIR. Note that the iPod must already be
mounted so that you can read it.

For example, say my iPod is mounted at /media/IPOD and I want to mount the
FUSEPod layer at /home/keegan/ipod You would enter at the console::

  $ IPOD_DIR="/media/IPOD" fusepod /home/keegan/ipod

You can also create the necessary files and directories for your iPod to
work. FUSEPod will prompt you if you specify IPOD_DIR which does not have the
necessary structure.

To unmount FUSEPod type at the console::

  $ fusermount -u [mounted_to]

To add songs copy them to `[mounted_to]/Transfer`. Or add the absolute path of
the song to the file `[mounted_to]/add_songs`. You can also add files and
recursively add directories through::

  $ [mounted_to]/add_files.sh [ files/directories ] ...

To sync the database and copy the files run::

  $ [mounted_to]/sync_ipod.sh [ -watch ]

The switch `-watch` will give you status messages while syncing the iPod.

For example, say I was in the `[mounted_to]` directory. To add a CD to my iPod
I would type at the command line::

  $ find /music/Deftones\ -\ BSides\ and\ Rarities > add_songs

Then if I also wanted to add another album I would type at the command line::

  $ find /music/Darkest\ Hour\ -\ Undoing\ Ruin >> add_songs

or I could enter at the command line (this is should be in one line)::

  $ ./add_files.sh /music/Deftones\ -\ BSides\ and\ Rarities \
                   /music/Darkest\ Hour\ -\ Undoing\ Ruin

or I could copy files over with `cp` or a filemanager (Konqueror, Nautilus...)
into the Transfer directory.

You can the view all the songs that will be added to the iPod by looking in
the Transfer directory and by running the command (Note songs that can't be
added will be ignored)::

  $ less add_songs

If you are happy with the contents of add_songs and the Transfer directory you
can run the command::

  $ ./sync_ipod.sh

Configuration
=============

A file containing the directory layout for fusepod is located in you home
directory in the file `.fusepod`

The configuration file works by substituting %{letter} with the appropriate tag
from the song, according to the following table::

  %a = Artist
  %A = Album
  %t = Title
  %g = Genre
  %T = Track
  %y = Year
  %r = Rating
  %e = File Extension

For example, say you wanted the layout::

  /Testing/1/2/3/{Artist}/{Title}.{Extension}

You would write in the `.fusepod`::

  /Testing/1/2/3/%a/%t.%e

Note that the line has to start with a / character. If you do not have a
`.fusepod` in your home directory just run fusepod. (The default `.fusepod` is
written on the first run)

License
=======

See the file `COPYING`.

Authors
=======

See the file `AUTHORS`.
