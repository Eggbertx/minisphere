miniSphere
==========

[![Build Status](https://travis-ci.org/fatcerberus/minisphere.svg?branch=master)]
(https://travis-ci.org/fatcerberus/minisphere)

miniSphere is a drop-in replacement and successor to the Sphere game engine,
written from the ground up in C.  It boasts a high level of compatibility with
most games written for Sphere 1.x, with better performance and new
functionality.  The majority of games will run with no modifications.

Overview
--------

Like Sphere, miniSphere uses JavaScript for game coding.  The engine exposes a
collection of low-level functions through a standardized JavaScript API,
leaving higher-level game logic entirely up to script.  This allows any type
of game to be made with miniSphere; of course, this naturally requires more
expertise than making a game with, say, RPG Maker or even Game Maker, but the
ultimate flexibility you get in return is worth it.

The engine uses Allegro 5 for graphics and sound and Duktape for JavaScript.
As both of these are portable to various platforms, this allows miniSphere to
be compiled successfully on all three major platforms (Windows, Linux, and
OS X)--and possibly others--with no changes to the source.

Powerful JS Debugging
---------------------

miniSphere includes a powerful but easy-to-use command-line debugger, called
SSJ.  The debugger allows you to step through your game's code and inspect the
internal state of the game--variables, call stack, objects, etc.--as it
executes.  And since miniSphere uses JavaScript, the original source files
aren't required to be present--SSJ can download source code directly from the
miniSphere instance being debugged.

A symbolic debugger such as SSJ is an invaluable tool for development and is a
miniSphere exclusive: No similar tool was ever available for Sphere 1.x.


Download
========

The latest stable miniSphere release at the time of this writing is
**minisphere 4.4.4**, released on Monday, February 6, 2017.  miniSphere
binaries are provided through GitHub, and the latest version is always
available for download here:

* <https://github.com/fatcerberus/minisphere/releases>

For an overview of breaking changes in the current major stable release series,
refer to [`RELEASES.md`](RELEASES.md).


License
=======

miniSphere and its accompanying command-line tools are licensed under the terms
of the BSD-3-clause license.  Practically speaking, this means the engine can
be used for any purpose, even commercially, with no restriction other than
maintain the original copyright notice.
