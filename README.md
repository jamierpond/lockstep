# lockstep

lockstep fetches a CMake project's dependencies and pins each one in
`lockstep.lock` to a commit and a hash of its files. A configure on any machine
then gets the same sources or stops with an error; it takes the same calls as
CPM.

## Install

```bash
brew install jamierpond/tap/lockstep
scoop bucket add jamierpond https://github.com/jamierpond/scoop-bucket && scoop install lockstep
```

A project does not need it installed: its `cmake/lockstep.cmake` downloads the
binary it pins. An installed `lockstep` run inside a project runs the version
that project pins.

## Switching from CPM

1. Download `lockstep.cmake` from the
   [latest release](https://github.com/jamierpond/lockstep/releases/latest)'s
   assets (it pins that release and its hashes) into `cmake/` and
   put `set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)`
   before `project()`.
2. Delete `CPM.cmake`. `include(CPM)` lines can stay: lockstep answers them
   with an empty stub, and your `CPMAddPackage` calls work unchanged.
3. Configure as usual. With no lock yet, lockstep writes `lockstep.lock`;
   commit it.

## A 30-second demo

Paste this into an empty directory, with `cmake/lockstep.cmake` taken from the
latest release's assets:

```cmake
cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
project(Demo NONE)

lockstep_add_package(NAME fmt GITHUB fmtlib/fmt GIT_TAG 11.0.2 DOWNLOAD_ONLY)
```

The first configure has no lock, so it writes one, as `npm install` writes
`package-lock.json`:

```console
$ cmake -B build
lockstep: resolving fmt at '11.0.2' from https://github.com/fmtlib/fmt.git
lockstep: locked fmt at 0c9fce2ffefecfdce794e1859584e25877b7b592 (content 9cf326c866562a2f60a8ab2ab847e9f6674918b226eb95aec6de2c7ce0ff6649)
-- lockstep: no lockstep.lock, so wrote one with 1 packages; commit it.
-- lockstep: root 4a88cf6cb8b629abf641a08dc0d1996ed94f0e4d93fa36be901473050ead16f8
```

`lockstep.lock` now holds:

```json
{
  "bootstrap": "cmake/lockstep.cmake",
  "packages": [
    {
      "commit": "0c9fce2ffefecfdce794e1859584e25877b7b592",
      "hash": "9cf326c866562a2f60a8ab2ab847e9f6674918b226eb95aec6de2c7ce0ff6649",
      "kind": "git",
      "name": "fmt",
      "patches": [],
      "requestedRef": "11.0.2",
      "submoduleSelection": {
        "isRecursive": true,
        "isRestricted": false,
        "paths": []
      },
      "submodules": [],
      "url": "https://github.com/fmtlib/fmt.git"
    }
  ],
  "root": "4a88cf6cb8b629abf641a08dc0d1996ed94f0e4d93fa36be901473050ead16f8",
  "version": 1
}
```

Change `GIT_TAG 11.0.2` to `GIT_TAG 11.1.0`. Locked mode refuses the new tag.
`LOCKSTEP_MODE` is a cache variable, so pass `locked` to leave update mode:

```console
$ cmake -B build -DLOCKSTEP_MODE=locked
lockstep: CMakeLists.txt:5 asks for fmt at '11.1.0', but lockstep.lock pins it at '11.0.2'.
  To lock what the CMake asks for, run: cmake -B build -DLOCKSTEP_MODE=update
```

Run what it says, and locked mode passes again:

```console
$ cmake -B build -DLOCKSTEP_MODE=update
lockstep: resolving fmt at '11.1.0' from https://github.com/fmtlib/fmt.git
lockstep: locked fmt at 0e8aad961d66904cfda8d7cc894f6f6eee2d9f30 (content aaed0084d88b1e6f6fd140c91a719dd8cd2b300b457b6f84c42d314c459bc6d2)
-- lockstep: root a98b0b1574cbe1f38954f5f05620b2ea91d6f2512a0195a338fda2f5e2741854
$ cmake -B build -DLOCKSTEP_MODE=locked
-- lockstep: root a98b0b1574cbe1f38954f5f05620b2ea91d6f2512a0195a338fda2f5e2741854
```

`lockstep update fmt` is for the other case: the CMake is unchanged and you
want the lock to follow where its tag or branch points now.

## Reference

Every configure after the first is `locked`: it fetches exactly the locked
commit, checks the content hash, and fails when a package is missing from the
lock or asked for at a different URL or ref. Configure ends with
`lockstep: root <hash>`, one hash over every package.

`lockstep_add_package` takes `NAME`, `GITHUB` (or `GITHUB_REPOSITORY`,
`GITLAB_REPOSITORY`, `GIT_REPOSITORY`), `GIT_TAG` or `VERSION` (meaning tag
`v<VERSION>`), `URL` for `.zip` and tar archives, `OPTIONS` (`"NAME"` alone means ON), `PATCHES` (applied
with `git apply` before hashing), `SOURCE_SUBDIR`, `DOWNLOAD_ONLY`,
`EXCLUDE_FROM_ALL`, `SYSTEM`, `SOURCE_DIR` (alone, a local checkout; beside a
repository or URL, where the locked tree is copied, as CPM clones it there),
`URL_HASH <algorithm>=<hex>` or `URL_MD5` (any algorithm CMake's
`file(DOWNLOAD EXPECTED_HASH)` takes, checked against the archive), `GIT_SUBMODULES` and
`GIT_SUBMODULES_RECURSE`. Submodules are checked out recursively by default,
each at the commit its parent names, and the lock records each one's path, URL
and commit; `GIT_SUBMODULES` with paths limits them, and `GIT_SUBMODULES ""`
turns them off. `GIT_SHALLOW`, `NO_CACHE` and `CUSTOM_CACHE_KEY` are accepted
and change nothing, since lockstep fetches exact commits into a cache keyed by
content. It sets `<Name>_SOURCE_DIR`,
`<Name>_BINARY_DIR` and `<Name>_ADDED`, as CPM does.

A package's targets are left out of `all`, unlike CPM and FetchContent: a plain
`cmake --build` compiles only the targets your project builds and what they
link, and `cmake --install` skips the package's own `install()` rules. To build
and install everything a package declares, pass `EXCLUDE_FROM_ALL NO`:

```cmake
lockstep_add_package(NAME eacp GITHUB eyalamirmusic/eacp GIT_TAG main
        EXCLUDE_FROM_ALL NO)
```

| Variable | Effect |
| --- | --- |
| `LOCKSTEP_MODE` | `locked` (default), `update` (re-resolve every ref and rewrite the lock) or `offline` (use the cache only, never the network) |
| `LOCKSTEP_<Name>_SOURCE` | Use a local checkout of `<Name>` instead; configure prints a warning. `CPM_<Name>_SOURCE` is the same, and `LOCKSTEP_` wins when both are set |
| `LOCKSTEP_CI` | `ON` makes a `<Name>_SOURCE` override an error in locked mode |
| `LOCKSTEP_CACHE` | The shared cache (also an environment variable). Default `~/.cache/lockstep`, `$XDG_CACHE_HOME/lockstep`, or `%LOCALAPPDATA%\lockstep` on Windows |
| `LOCKSTEP_LOCK_FILE` | Default `<top-level source dir>/lockstep.lock` |
| `LOCKSTEP_EXECUTABLE` | Run this `lockstep` binary instead of downloading the pinned release. The environment variable wins over the cached value |
| `LOCKSTEP_LIBRARY_CACHE` | `ON` takes a package's static libraries, the executables it builds and its object libraries' object files from `<cache>/lib` when an earlier build built them from the same inputs, instead of building them again. Also an environment variable, which turns the cache on for every project; `-DLOCKSTEP_LIBRARY_CACHE=ON` or `OFF` on a build directory wins over it, and it wins over `enabled` in [lockstep.json](#lockstepjson). Default `OFF`; see [The library cache](#the-library-cache) |
| `LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE` | Also take stored builds made by a compiler whose version is in this range, such as `>=21.0.0`. Also an environment variable and `compilerRange` in `lockstep.json`; a `-D` on a build directory wins over both, and an empty one turns the range off there. Unset, only this machine's own compiler's builds are taken; see [Builds by another compiler or SDK](#builds-by-another-compiler-or-sdk) |
| `LOCKSTEP_LIBRARY_CACHE_SDK_RANGE` | The same for the Apple SDK that built a stored entry, such as `>=26.5`; `sdkRange` in `lockstep.json` |
| `LOCKSTEP_LIBRARY_CACHE_MAX_SIZE` | The size `<cache>/lib` is kept under by deleting the least recently used entries, such as `10 GiB`. Default `5 GiB`. Also an environment variable and `maxSize` in `lockstep.json`; see [The size limit](#the-size-limit) |
| `LOCKSTEP_LIBRARY_CACHE_LOCATION_MAX_SIZE` | The same for the library cache location. Default `20 GiB`; `locationMaxSize` in `lockstep.json` |
| `LOCKSTEP_LIBRARY_CACHE_LOCATION` | Where the library cache is, so several machines share it: a directory, or `[user@]host:directory` on a Unix host reached over ssh. Also an environment variable; `-DLOCKSTEP_LIBRARY_CACHE_LOCATION=...` on a build directory wins over it, and an empty value turns it off there; `location` in `lockstep.json` comes after both. Unset, the library cache is `<cache>/lib` alone; see [One library cache for several machines](#one-library-cache-for-several-machines) |

### lockstep.json

`lockstep.json` holds a project's lockstep settings, committed beside
`lockstep.lock`. It is read from the directory of the lock file in use, so with
`LOCKSTEP_LOCK_FILE` pointing elsewhere it is the `lockstep.json` beside that
file. No file changes nothing.

```json
{
  "libraryCache": {
    "enabled": true,
    "location": "you@buildbox:.cache/lockstep-library-cache",
    "compilerRange": ">=21.0.0",
    "sdkRange": ">=26.5",
    "maxSize": "5 GiB",
    "locationMaxSize": "20 GiB"
  }
}
```

Each key is the setting of the same meaning in the table above:
`enabled` is `LOCKSTEP_LIBRARY_CACHE` (`true` or `false`), `location`,
`compilerRange`, `sdkRange`, `maxSize` and `locationMaxSize` are
`LOCKSTEP_LIBRARY_CACHE_LOCATION`, `_COMPILER_RANGE`, `_SDK_RANGE`, `_MAX_SIZE`
and `_LOCATION_MAX_SIZE`, all strings. Each setting comes from the first of: a
`-D` on the build directory, the environment variable, `lockstep.json`, the
default. A key lockstep does not know, or a value of the wrong type, stops the
configure with a message naming the key and the file, as an unknown
`lockstep_add_package` option does. A configure runs again when the file
changes. `lockstep cache stats` run inside a project prints each setting, its
value and where it came from (a build directory's `-D` is not visible there).

### The library cache

Without it, every new build directory compiles every package again: a second
checkout of a project, or a wiped `build/`, rebuilds eacp from scratch even
though nothing about it changed. With the library cache on, a package's
static libraries, the executables it builds (such as a code generator the build
runs) and the object files of its object libraries are built once per set of
inputs, and every later build directory on the machine, of any project, copies
them from `<cache>/lib`.

Turn it on for every lockstep project on the machine by setting the
environment variable once, in your shell profile:

```bash
export LOCKSTEP_LIBRARY_CACHE=ON
```

Nothing in a project changes, and a project's committed `cmake/lockstep.cmake`
needs no update: the switch is read by the CMake module the binary writes.
`-DLOCKSTEP_LIBRARY_CACHE=ON` or `-DLOCKSTEP_LIBRARY_CACHE=OFF` turns it on or
off for one build directory, and wins over the environment variable for as
long as it stays in that directory's `CMakeCache.txt`. To turn it on for
everyone who builds a project, commit a `lockstep.json` (see
[lockstep.json](#lockstepjson)).

At the end of the configure, lockstep computes a key for each static library a
locked package declares: a SHA-256 over the package's content hash, the
target's sources and compile settings, the usage requirements of everything it
links, its directory's compile options and `CMAKE_<LANG>_FLAGS`, the compiler's
ID, path and target, the build type, the platform and SDK settings, and the
values of the upper-case variables the package's CMake files name (its
options). What built the library stays out of the key and is recorded beside
the stored library instead (see
[Builds by another compiler or SDK](#builds-by-another-compiler-or-sdk)): the
version of each compiler and a SHA-256 of its executable, the Apple SDK's
version and build, the CMake version and the system version. Compiler and flag settings are read in the directory that declares
the library, as the compiler gets them: a package whose own `project()` enables
C has its C compiler in the key even when the top-level project enables only
C++. The build directory and the cache directory are replaced by fixed
words, and so are the random `cmTC_<hex>` names of `try_compile` projects in a
variable that keeps a check's output, so two build directories of the same
project get the same key. The cache directory's own path then goes into the
key once, on a line of its own, because the compiler writes the paths of the
package sources into the object code (see
[One library cache for several machines](#one-library-cache-for-several-machines)). Left out,
because they cannot change the library's object code, so two projects that
differ only in them share it: the library's own `SYSTEM` property (it changes
only how the library's consumers include its headers), `C_STANDARD_REQUIRED`
and `CXX_STANDARD_REQUIRED` (they decide only whether CMake stops when the
compiler lacks the standard), `ARGC`, `ARGN`, `ARGV` and `ARGV0`, `ARGV1`, ...
(a package's directory inherits them from the `lockstep_add_package` call that
added it, so they hold the text of that call: `GIT_TAG develop` and the same
commit's SHA give one key, as does a different argument order; the call's
meaning is in the key already, through the package's content hash, its option
values and the `SYSTEM` property of linked targets), the generator (Ninja and
Unix Makefiles compile and archive a library with the same commands; it stays
in the key of a library whose C++ sources are scanned for modules, which only
Ninja does, so `CXX_SCAN_FOR_MODULES` off is what lets them share), the
`CMAKE_<LANG>_` settings of every language
the library has no source in, and `CMAKE_OSX_SYSROOT`,
`CMAKE_OSX_DEPLOYMENT_TARGET` and `CMAKE_OSX_ARCHITECTURES` when the target
system is not Apple's.

- A hit keeps the target, so its usage requirements and the CMake functions its
  package defines work as before. The target compiles one empty stub source
  instead of its own, still generates its headers, and its archive step copies
  `<cache>/lib/format-<n>/<key>/<library>` into place.
- A miss builds as usual. When an executable or shared library that links it
  links successfully, lockstep copies the library into
  `<cache>/lib/format-<n>/<key>/`, so only what a build used is stored. The entry's `manifest.json` holds the text
  the key was computed from; `_lockstep/library-cache/inputs/<key>.txt` in the
  build directory holds the same for this configure, to compare against.

Executables a locked package builds, such as ResEmbed's `ResourceGenerator`
that a build runs to generate sources, are cached the same way. The key of an
executable covers everything a library's key covers for its own sources, and
what decides its link: the key of every static and object library it links, its
link options and flags and those of every target it links (`LINK_OPTIONS`,
`LINK_FLAGS`, `INTERFACE_LINK_OPTIONS`, link directories, rpath settings, the
imported libraries' files), `CMAKE_EXE_LINKER_FLAGS`, the linker's path, ID and
version, the libraries the toolchain links from the system
(`CMAKE_<LANG>_STANDARD_LIBRARIES` and the implicit link libraries), and the
target platform. It always covers the cache directory's path, even when the
flags map it away. On a hit the executable compiles one stub, links nothing,
and its link step copies the stored file into place under a temporary name and
renames it over the old one, so a signed macOS executable that ran before is
replaced rather than rewritten. On a miss its own link stores it.

An object library's objects reach their consumers as files, through
`$<TARGET_OBJECTS:...>`, so a hit keeps its sources: each compile step, through
the target's `RULE_LAUNCH_COMPILE`, copies the stored object for that source into
place instead of running the compiler. Its key is a static library's. The
objects are stored, in the order `$<TARGET_OBJECTS:...>` lists them, by the
first link that reads all of them. Each copy starts lockstep once, through
`cmake -E env`: about 20 ms per object on Linux.

Configure prints `lockstep: library cache: <n> hits, <n> misses, <n> skipped`
and adds its hits and misses to the counts in
`<cache>/lib/format-<n>/statistics.json`; the link that stores a library adds
one store there. When the packages build executables or object libraries, the
line counts each kind on its own:

```console
-- lockstep: library cache: static libraries: 15 hits, 18 misses, 0 skipped; executables: 2 hits, 0 misses, 0 skipped; object libraries: 2 hits, 0 misses, 0 skipped (--log-level=VERBOSE names them)
```

`--log-level=VERBOSE` names each library and why one was skipped.
`lockstep cache stats` prints the counts, and `lockstep cache clear` deletes
the stored libraries (see [The command line](#the-command-line)). It does not
cache:

- shared libraries, or a static library whose objects another target takes
  through `$<TARGET_OBJECTS:...>`;
- executables and libraries the project itself declares, outside its
  packages;
- a package from a local `SOURCE_DIR`, a `<Name>_SOURCE` override or a
  `SOURCE_DIR` checkout, or a library that links a target of one, or of the
  project, since no hash covers those files;
- anything when the generator is multi-configuration (Visual Studio, Xcode,
  Ninja Multi-Config);
- libraries using `AUTOMOC`, `AUTOUIC`, `AUTORCC` or C++ module sets;
- with MSVC or another compiler for the MSVC ABI, a library whose compile
  writes its debug information to a separate `.pdb`: `/Zi` or `/ZI` in its
  flags, or `MSVC_DEBUG_INFORMATION_FORMAT` (set from
  `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`) of `ProgramDatabase` or
  `EditAndContinue`, which CMake uses for Debug and RelWithDebInfo builds when
  it is unset. The cache does not store the `.pdb`, so a cached library would
  link with debug information that points into the first build directory.
  `-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` (`/Z7`) keeps the debug
  information in the library, and makes Debug builds cacheable;
- a library compiled with `-gsplit-dwarf`, which writes the debug information
  to a `.dwo` file beside each object, which the cache does not store.
  `-gsplit-dwarf=single` keeps it in the object, and is cached;
- an executable that links a shared library this build directory builds, links
  an executable, or links a library the cache skips;
- an app bundle (`MACOSX_BUNDLE`), or an executable with `ENABLE_EXPORTS`,
  whose link writes an import library beside it;
- an executable MSVC's linker writes a `.pdb` for: `/DEBUG` in its link flags,
  which CMake's Debug and RelWithDebInfo builds have;
- an executable whose link settings name the build directory, such as a
  `BUILD_RPATH` or an imported library built there, so the copy could point
  into the build directory that linked it;
- an executable that links a target a package's directory declares without
  `GLOBAL` (such as `Threads::Threads` from `find_package(Threads)`), whose
  link settings the top-level directory cannot read;
- an executable when CMake reports no linker version
  (`CMAKE_<LANG>_COMPILER_LINKER_VERSION`, which CMake 3.29 and newer set).

The key does not read the contents of headers outside the packages (system
and SDK headers) or of files in the build directory (a header a package writes
with `configure_file` is covered only through the package's files and the
variables it names), and it misses a variable whose name the package's CMake
assembles, such as `${prefix}_OPTION`, and a linked imported target that a
package's directory declares without `GLOBAL`, which the top-level directory
cannot see: its usage requirements stay out of the key, and libraries linked
only through it are never stored. A change to one of those can give a hit
that links a stale library; `lockstep cache clear` rebuilds everything. Nor
does an executable's key read the contents of the system libraries it links,
only their names and paths, as for system headers. On Apple systems the debug
information of a cached executable stays in the object files of the build
directory that linked it, as the linker always leaves it there: once that
directory is deleted, a debugger has no line information for the executable's
own sources. A source a package generates but a project target compiles,
such as the `<Name>_Register.cpp` ResEmbed adds to every target that links a
library with resources, compiles in every build: it is the project target's
compile, which the cache does not store.

### Builds by another compiler or SDK

Two Macs on different Xcode releases never share a library: their compiler and
SDK versions differ, so each builds its own. When a nearby compiler's output is
good enough for you, a range says so, and the library cache takes builds made
by the compilers and SDKs in it:

```bash
export LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=">=21.0.0"
export LOCKSTEP_LIBRARY_CACHE_SDK_RANGE=">=26.5"
```

Beside each stored library, executable and object library lockstep records
what built it: for each language the target compiles, the compiler's version
(`CMAKE_<LANG>_COMPILER_VERSION`) and a SHA-256 of the compiler's executable;
for an executable, the linker's version (`CMAKE_<LANG>_COMPILER_LINKER_VERSION`);
on Apple systems the version and build of the SDK the compiler uses, as `xcrun
--show-sdk-version` and `--show-sdk-build-version` print them (with CMake 4
`CMAKE_OSX_SYSROOT` is usually empty); the CMake version; and, unless
cross-compiling, `CMAKE_SYSTEM_VERSION`, the version of the system that built
it. When cross-compiling, `CMAKE_SYSTEM_VERSION` is the target's, such as an
Android API level, and stays in the key. Everything else is in the key,
including the compiler's ID, so an AppleClang, Clang, GCC or MSVC build is
never taken by another compiler. One key holds several builds side by side,
one per record, in `<cache>/lib/format-<n>/<key>/<build id>/`, where the build
id is the SHA-256 of the record.

With neither range set, a lookup takes only a build whose record is this
machine's own, so the same builds hit and miss as before the record existed. A
range admits builds by others:

- `LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE`: the version of the compiler that
  built the entry must be in the range, for every language. The compiler
  executable's hash and the linker's version are not compared.
- `LOCKSTEP_LIBRARY_CACHE_SDK_RANGE`: the version of the SDK that built the
  entry must be in the range. The SDK's build is not compared.

Without the compiler range the compiler must be this machine's own: the same
version, executable and linker. Without the SDK range the SDK must be the same
version and build. Someone who accepts another compiler's or SDK's output has
accepted another machine's build, so with either range set the CMake version
and system version that built an entry are not compared.

A range is one or more parts separated by spaces, all of which must hold:
`>=21.0.0`, `<=21.0.4`, `>=21.0.0 <=21.0.4` or `=21.0.0.21000101`. A part
compares as many numbers of a version as it has itself: `=21.0.0` holds
21.0.0, 21.0.0.21000101 and 21.0.0.21000401 but not 21.0.1, and `<=21.0.4`
holds 21.0.4.9. Anything else stops the configure with a message naming the
setting. A range is not a filter on this machine's own builds: those are
always taken. Of the other builds a range admits, lookup takes the one made
by the newest compiler, then the newest SDK.

A library or object library compiled with link-time optimisation (`-flto` in
any form, `INTERPROCEDURAL_OPTIMIZATION`, or MSVC's `/GL`) holds the compiler's
intermediate code, which the linker compiles, and its record says so. Whatever
the range says, a build of one by a Clang newer than this machine's is never
taken, since this machine's linker cannot read newer bitcode, and a build by
GCC or MSVC is taken only from the same compiler version, since those read
only their own version's intermediate code. An executable holds machine code
only, so this does not apply to it; an executable is taken only when it linked
the same builds of its libraries as this build directory links.

A hit on another toolchain's build is never silent. The configure line counts
them, `15 hits (3 built by another toolchain), 18 misses, 0 skipped`;
`--log-level=VERBOSE` names what built each, as in `library cache hit for
eacp-core (<key>; built with CXX compiler 21.0.0.21000101, SDK 26.5 (25F70))`,
and names for each miss the stored builds it did not take and why; and
`lockstep cache stats` counts them under `another toolchain`.

The compiler's SHA-256 closes a hole: the key used to hold only a compiler's
name and version, so a patched compiler that reports the same version shared
libraries with the unpatched one. lockstep hashes the files the compiler really
runs, not the launcher CMake names:

- Clang and AppleClang: the binary in the directory `<compiler> --version`
  prints as `InstalledDir`. On macOS that is the toolchain's `clang`, not the
  `/usr/bin` shim that runs `xcrun`, so two Macs with the same Xcode share: its
  bytes are the same, and the time it was installed is not part of the hash.
  Under nix it is the `clang` the wrapper runs.
- GCC: the compiler proper, `cc1plus` for C++ (`cc1` for C), as
  `<compiler> -print-prog-name=cc1plus` prints it.
- MSVC: `cl.exe` and the `c1.dll`, `c1xx.dll` and `c2.dll` beside it.
- Any other compiler, or one that does not answer: the file CMake names.

A compiler CMake names that is a script, such as nix's wrapper, which adds
flags of its own, is hashed too. Each file's SHA-256 is remembered in
`<cache>/lib/compiler-executables.json` by its path, size and modification
time, so a configure rehashes only a compiler that changed. When no file can be
read, the hash is empty, which matches only another empty one, as the version
alone did before. A compiler that keeps its code in shared libraries, such as
a Linux distribution's Clang, is covered through its executable only; under
nix the executable names its libraries' store paths, which covers them too.

On Windows with whole-program optimisation (`/GL`), the stored `.lib` files
hold MSVC's intermediate code rather than machine code, so they are large:
eacp's 8 libraries took 190 MiB, against 5.5 MiB on macOS.

### One library cache for several machines

Each machine compiles every package once on its own, even when another
machine with the same compiler and SDK already has. With
`LOCKSTEP_LIBRARY_CACHE_LOCATION` set, machines share one library cache: a
library, executable or object library one machine built is used on the others
without compiling. Each is one entry, carried the same way.

```bash
export LOCKSTEP_LIBRARY_CACHE=ON
export LOCKSTEP_LIBRARY_CACHE_LOCATION=you@buildbox:.cache/lockstep-library-cache
```

The location is `[user@]host:directory`, reached with the `ssh` on `PATH`, or a
directory path, such as a mounted share. After `host:`, a directory that does
not start with `/` is relative to the user's home directory on the host. The
host must be a Unix machine with `sh` and `tar`, such as macOS or Linux; a
Windows machine can use a location over ssh with the OpenSSH that ships with
Windows, but cannot be the host. lockstep runs `ssh` with `BatchMode=yes` and a
5-second connect timeout, so a build never stops at a password or host-key
prompt: the host must accept your key and be in `known_hosts` already, which
running `ssh <host> true` once by hand gives you.

The linker needs a file on this disk, so lockstep keeps a copy of every library
it fetches or builds in `<cache>/lib`, and looks there first. At the end of a
configure it asks the location, in one ssh connection, for every library this
machine has no copy of, and the host sends back a tar of the ones it holds.
Each is checked before it is used: every file in it must match the SHA-256 its
`manifest.json` records, and the manifest's key inputs must hash to the key. A
library that fails is discarded with a warning and built from source. Configure
prints:

```console
-- lockstep: library cache at you@buildbox:.cache/lockstep-library-cache: fetched 8 of the 8 libraries this machine had no copy of in 1.4 s; sending 0 it lacks
```

A library this machine builds is sent to the location after the link that
copies it into `<cache>/lib`, by a `lockstep send-libraries` process that keeps
running after the link returns, so no link waits on the network. The host
unpacks it under a temporary name and renames it into place, so no machine
ever fetches a half-sent library; when two machines send the same library at
once, the first rename wins and the second copy is deleted. The configure also
sends every library this machine has a copy of that the location lacks, which
covers copies made before the location was set and sends that failed.

When the location cannot be reached, because the host is down, refuses the
key, has a full disk, or the fetch takes longer than 60 seconds, configure prints one
warning and builds with this machine's copies only. A send that fails cannot
print into the build it followed, which has finished by then: it writes the
reason to `_lockstep/library-cache/send-failure.txt` in the build directory,
the next configure prints it once as a warning, and sends the missing
libraries again. Each send writes what it did to
`_lockstep/library-cache/send.log`, and is stopped after 600 seconds.

A location that is a directory works the same way, through the same copies in
`<cache>/lib`: a build links only files on this disk, and a share that goes
away during a build cannot break the link.

Two machines share a library only when its key is the same on both and the
build at the location is one they take (see
[Builds by another compiler or SDK](#builds-by-another-compiler-or-sdk)). With
no range set, a Mac on another Xcode or SDK misses, builds the library and
sends its own build beside the first. With a range set, a configure asks the
location in the same round trip for every build of a key whose own build it
lacks, checks each, and takes one its ranges admit. The key
also covers the path of the cache directory, because the compiler writes the
absolute paths of the package sources into the object code, in `__FILE__`
(asserts and log lines) and in the debug information. Machines share only when
their cache directory is the same path, which it is for the default cache when
the user's home directory is the same. A library whose flags remove that path,
for every language it compiles, with `-ffile-prefix-map=<old>=<new>` (or both
`-fmacro-prefix-map` and `-fdebug-prefix-map`), where `<old>` is the cache
directory or a directory above it, leaves the path out of its key and is shared
across paths. lockstep does not read MSVC's `/pathmap`, so a library MSVC
compiles always keeps the path in its key.

`lockstep cache stats` and `lockstep cache clear` read
the location from the environment and from the `lockstep.json` of the project
they run in. `cache stats` names the location, and counts under `fetched` the hits whose library came from it.
`cache clear` deletes this machine's copies in `<cache>/lib` and their counts,
and leaves every library at the location. The location is kept under its own
size limit (see [The size limit](#the-size-limit)).

### The size limit

The library cache would grow forever, one entry for every set of inputs any
build ever compiled. Each store is kept under a size limit instead: 5 GiB for
this machine's `<cache>/lib` and 20 GiB at a location, as
`LOCKSTEP_LIBRARY_CACHE_MAX_SIZE` and `LOCKSTEP_LIBRARY_CACHE_LOCATION_MAX_SIZE`
(or `maxSize` and `locationMaxSize` in `lockstep.json`) set. A size is a number
and a unit: `5 GiB`, `500 MB`, `1.5 TiB` or a plain number of bytes; KB, MB,
GB and TB count in thousands, KiB, MiB, GiB and TiB in 1024s. Anything else, or
zero, stops the configure with a message naming the setting.

Every entry has a marker file, `format-<n>/used/<key>.<build id>`, whose time
is when the entry was last used. A configure touches the marker of every entry
it hits, a link touches the marker of each entry it stores, a copy into a build
directory touches the marker of the entry it copies, and a fetch touches the
markers at the location of every entry it asks about that the location holds.
No library is rewritten to mark it.

When a link stores an entry, or a configure fetches some, and the entries in
`<cache>/lib` then take more than the limit, lockstep deletes whole entries,
the one whose marker is oldest first, until they fit. It never deletes an entry
used in the last hour: a configure marks every entry it hits, so the build it
configured has an hour to copy them, and a store that has only recent entries
stays over its limit until they age. An entry is moved out of its place before
it is deleted, so no lookup finds half of one, and a second lockstep evicting
at the same time deletes others or nothing; neither fails. Configure prints
`lockstep: library cache: deleted the 3 least recently used entries to stay
under its 5.0 GiB limit` when it deleted some.

A send to a location does the same there with the location's limit, using only
`sh`, `du`, `ls -tr`, `find -mmin`, `mv` and `rm` on the host: when the
location's directory takes more than its limit it deletes the builds with the
oldest markers first, never one used in the last hour. An entry another
lockstep stored there without a marker gets one with its manifest's time
first.

`lockstep cache stats` prints the limit and the evictions under the counts, and
with a location its limit and how many entries this machine's sends deleted
there.

### The command line

```bash
lockstep update            # re-resolve every package's ref and rewrite the lock
lockstep update Miro       # just Miro
lockstep verify            # check each cache entry against its lock hash
lockstep verify --full     # rehash every tree instead of trusting size/mtime stamps
lockstep root              # print the root hash
lockstep cache stats       # stored libraries, their size on disk, and hits and misses
lockstep cache clear       # delete the stored libraries and reset the counts
```

`cache stats` prints:

```console
$ lockstep cache stats
library cache /home/you/.cache/lockstep/lib
  libraries     13        stored now
  size          33.5 MiB  on disk
  hits          9         a configure found the library stored, so it was not built
  misses        62        a configure did not find the library stored:
    stored      13        built, then stored by the first link that used it
    not stored  49        no link used it: the project does not build it, or has not built yet
  limit         5.0 GiB   past it, the entries unused for longest are deleted
  evictions     0         entries deleted to stay under the limit
settings here, and where each comes from (a -D on a build directory wins over them there)
  LOCKSTEP_LIBRARY_CACHE                    ON        /home/you/app/lockstep.json
  LOCKSTEP_LIBRARY_CACHE_LOCATION           (unset)   the default
  LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE     >=21.0.0  the environment
  LOCKSTEP_LIBRARY_CACHE_SDK_RANGE          (unset)   the default
  LOCKSTEP_LIBRARY_CACHE_MAX_SIZE           5 GiB     the default
  LOCKSTEP_LIBRARY_CACHE_LOCATION_MAX_SIZE  20 GiB    the default
```

`libraries` counts the static libraries stored in `<cache>/lib`, `size` is every file
of the current format under it, and the counts add up every configure and link
since the last `cache clear`. A miss is stored only when a link reads the
library, so a library a project configures but never links (a package library
nothing in the project uses) stays under `not stored`, and every configure of
that project counts it again. Two build directories that miss the same library
and both build it store it once, so one of the two counts as `not stored`.
With `LOCKSTEP_LIBRARY_CACHE_LOCATION` set, the first line is
`library cache at <location>; this machine's copies in <cache>/lib`, and a
`fetched` line under `hits` counts the hits whose library was fetched from the
location; the counts are this machine's, kept in `<cache>/lib`. Once
executables or object libraries are stored or counted, the first line reads
`static libraries`, and each kind follows with its own `stored now`, hits and
misses, indented under its name.
`cache clear` deletes `<cache>/lib`, including entries an older lockstep
stored in another format (which no lookup reads), and nothing else: the
package sources in `<cache>/src`, the git mirrors in `<cache>/git` and the
downloaded binaries in `<cache>/bin` stay. Both act on the machine's store,
whichever project you run them in: inside a project that pins another
lockstep release, they still run in the `lockstep` you typed, where every
other command runs the pinned release.

### Dependencies that use CPM or FetchContent

The module defines `CPMAddPackage`, `CPMFindPackage`, `CPMDeclarePackage`,
`CPMGetPackage` and `CPMUsePackageLock`, and sets `CPM_DIRECTORY`, so a
dependency's own `include(CPM.cmake)` returns before defining anything and its
packages land in your lock. `CPMFindPackage` always fetches; it never calls
`find_package`. `FetchContent_MakeAvailable` goes through lockstep as a CMake
dependency provider. In locked mode, a package any of them asks for that is
not in the lock is an error naming the subproject that asked.

## How it works

[docs/Design.md](docs/Design.md) compares lockstep with CPM and covers the
design choices and the roadmap.


`cmake/lockstep.cmake` downloads the `lockstep` binary for the host from this
repository's GitHub releases, checking the SHA-256 it pins, into
`<cache>/bin/<version>/`, then includes the CMake module the binary writes
(`cmake/lockstep-module.cmake` here, embedded in the binary, so it always
matches it). Every command in that module forwards its arguments to the
binary, which parses them, resolves the package and writes the CMake to run:
the option variables, `<Name>_SOURCE_DIR` and the `add_subdirectory`. Resolving
fetches into a bare git mirror in the cache, checks out the commit with
line-ending conversion off, applies the patches, and stores the tree under
`<cache>/src/<merkle hash>`. The Merkle hash covers each entry's name, type
(file, symlink or directory) and contents; a directory's hash is the SHA-256 of
its sorted entries, and `.git` is excluded. Execute bits are not part of it, so
the same lock verifies on Windows.

## Known limits

- A dependency that fetches CPM through `get_cpm.cmake` still downloads
  `CPM.cmake` itself with `file(DOWNLOAD)` before including it. The include
  returns at once and fetches nothing, but that one download happens, and
  needs the network even in offline mode the first time. The script overwrites
  `CPM_DOWNLOAD_LOCATION`, so lockstep cannot redirect it.
- `DOWNLOAD_COMMAND`, `UPDATE_COMMAND`, `PATCH_COMMAND` and the other
  `*_COMMAND` options are refused, since what they produce cannot be locked;
  use `PATCHES`. So are `FORCE` and `URL_MD5`, and any option lockstep does
  not know, each by name.
- Symlinks in a dependency hash differently on Windows when git checks them
  out as plain files.
- A library cache location over ssh needs a Unix host; a Windows machine can
  use one but not be one. A send killed half-way leaves a staging directory, whose name starts with a
  dot or ends in `.staging`, beside the entries, which no fetch reads.
- A library fetched from another machine carries debug information that names
  that machine's build directory for headers generated in it, as a library
  from another build directory on the same machine already does.

## Supported systems

The release binaries run on macOS 11 or newer (one universal binary for arm64
and x86_64; `eacp-core` links UniformTypeIdentifiers, which needs 11),
Windows 10 or newer on x64 and arm64 (C runtime linked statically, so no
VC++ redistributable), and any Linux on x64 and arm64 (statically linked
against musl, with libcurl and mbedTLS built in, so no glibc needed). `git`
must be on `PATH`. On macOS and Windows lockstep downloads through the system's
own network library. On Linux every build, the release and one from source,
links libcurl 8.23.0 and mbedTLS 3.6.7 statically, built for HTTP, HTTPS, WS,
WSS and `file://` only, without HTTP/2, compression, IDN or PSL; a from-source
build needs only the machine's C and C++ runtime libraries. On Linux, HTTPS trusts the certificates in
`/etc/ssl/certs/ca-certificates.crt`, which Debian, Ubuntu, Alpine, Arch and
current Fedora provide; a distribution without that file cannot download
archives or the pinned binary. CI checks these floors on every build.

## Building and releasing

lockstep builds itself with lockstep: its `CMakeLists.txt` loads
`cmake/lockstep.cmake`, which pins the previous release, and `lockstep.lock`
locks eacp and its dependencies, and on Linux the libcurl and mbedTLS source
archives `cmake/linux-static-curl.cmake` builds. Bumping that pin is a manual
commit after each release. `-DLOCKSTEP_EXECUTABLE=<path>` runs a local build
instead.

```bash
just test                 # configure, build and run every test
just install              # cmake --install into ~/.local (just install <prefix>)
just fmt                  # clang-format over src and tests
just clean
```

The tests include configuring sample projects against real GitHub packages,
so they need the network. `.github/workflows/build.yml` builds and tests all
six targets on every push. Pushing a `v<version>` tag also publishes the
binaries as a release with build provenance attestations, prints
`cmake/lockstep.cmake` with that release's version and hashes filled in (also
uploaded as the release's `lockstep.cmake` asset), and, when the
`PACKAGING_TOKEN` secret holds a token that can push to them, updates the
Homebrew formula in `jamierpond/homebrew-tap` and the Scoop manifest in
`jamierpond/scoop-bucket` (repository variables `TAP_REPOSITORY` and
`BUCKET_REPOSITORY` override those). Without the secret, run
`scripts/update-packaging.sh <version> SHA256SUMS` by hand. The templates are
in `packaging/`. The binaries are not code-signed.
