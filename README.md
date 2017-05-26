lsync
=====

Simplified variant of rsync optimized for local backups.  

Features
========

This tool was basically made to implement one of the common use  
cases of rsync in a minimal way. It aims to provide the same function as:  

    rsync -av --link-dest ref src dst

lsync tries to be invokation compatible with rsync while supporting  
Linux and Windows.
Only Windows XP and newer is supported (Unicode enabled).  
The --link-dest option requires a filesystem with [hardlink](https://msdn.microsoft.com/en-us/library/windows/desktop/aa365006%28v%3Dvs.85%29.aspx) support.  

Usage
=====

    lsync [options] [<source> ...] <destination>
    
    -a, --archive
          Archive mode (same as -rlptgoD).
        --devices
          Preserves device files.
    -D
          Same as --devices --specials.
    -g, --group
          Preserves group.
    -h, --help
          Print short usage instruction.
        --link-dest
          Hardlink to files in destination if unchanged.
    -l, --links
          Copy symlinks as symlinks.
    -o, --owner
          Preserves owner.
    -p  --perms
          Preserves permissions.
    -r, --recursive
          Traverses given directories recursive.
        --specials
          Preserves special files.
    -v
          Increases verbosity.
        --version
          Outputs the program version.

Building
========

The following dependencies are given:  
- C99

Edit Makefile to match your target system configuration.  
Building the program:  

    make

[![Linux GCC Build Status](https://img.shields.io/travis/daniel-starke/lsync/master.svg?label=Linux)](https://travis-ci.org/daniel-starke/lsync)
[![Windows LLVM/Clang Build Status](https://img.shields.io/appveyor/ci/danielstarke/lsync/master.svg?label=Windows)](https://ci.appveyor.com/project/danielstarke/lsync)    

Files
=====

|Name           |Meaning
|---------------|--------------------------------------------
|*.mk           |Target specific Makefile setup.
|argp*, getopt* |Command-line parer.
|lsync.*        |Main application files.
|lsync_*        |Platform specific I/O functions.
|mingw-unicode.h|Unicode enabled main() for MinGW targets.
|target.h       |Target specific functions and macros.
|tchar.*        |Functions to simplify ASCII/Unicode support.
|tdir*          |Directory iterator.

License
=======

See [unlicense file](doc/UNLICENSE).  
