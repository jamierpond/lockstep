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
3. Configure once with `cmake -B build -DLOCKSTEP_MODE=update`, then commit
   `lockstep.lock`.

## A 30-second demo

Paste this into an empty directory, with `cmake/lockstep.cmake` taken from the
latest release's assets:

```cmake
cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
project(Demo NONE)

lockstep_add_package(NAME fmt GITHUB fmtlib/fmt GIT_TAG 11.0.2 DOWNLOAD_ONLY)
```

With no lock yet, a plain configure refuses and says what to run:

```console
$ cmake -B build
lockstep: CMakeLists.txt:5 asks for fmt, and there is no lockstep.lock yet.
  To write the lock, run: cmake -B build -DLOCKSTEP_MODE=update
```

Write the lock:

```console
$ cmake -B build -DLOCKSTEP_MODE=update
lockstep: resolving fmt at '11.0.2' from https://github.com/fmtlib/fmt.git
lockstep: locked fmt at 0c9fce2ffefecfdce794e1859584e25877b7b592 (content 9cf326c866562a2f60a8ab2ab847e9f6674918b226eb95aec6de2c7ce0ff6649)
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

| Variable | Effect |
| --- | --- |
| `LOCKSTEP_MODE` | `locked` (default), `update` (re-resolve every ref and rewrite the lock) or `offline` (use the cache only, never the network) |
| `LOCKSTEP_<Name>_SOURCE` | Use a local checkout of `<Name>` instead; configure prints a warning. `CPM_<Name>_SOURCE` is the same, and `LOCKSTEP_` wins when both are set |
| `LOCKSTEP_CI` | `ON` makes a `<Name>_SOURCE` override an error in locked mode |
| `LOCKSTEP_CACHE` | The shared cache (also an environment variable). Default `~/.cache/lockstep`, `$XDG_CACHE_HOME/lockstep`, or `%LOCALAPPDATA%\lockstep` on Windows |
| `LOCKSTEP_LOCK_FILE` | Default `<top-level source dir>/lockstep.lock` |
| `LOCKSTEP_EXECUTABLE` | Run this `lockstep` binary instead of downloading the pinned release. The environment variable wins over the cached value |

### The command line

```bash
lockstep update            # re-resolve every package's ref and rewrite the lock
lockstep update Miro       # just Miro
lockstep verify            # check each cache entry against its lock hash
lockstep verify --full     # rehash every tree instead of trusting size/mtime stamps
lockstep root              # print the root hash
```

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

## Supported systems

The release binaries run on macOS 11 or newer (one universal binary for arm64
and x86_64; `eacp-core` links UniformTypeIdentifiers, which needs 11),
Windows 10 or newer on x64 and arm64 (C runtime linked statically, so no
VC++ redistributable), and any Linux on x64 and arm64 (statically linked
against musl, with libcurl and mbedTLS built in, so no glibc needed). `git`
must be on `PATH`. On Linux, HTTPS trusts the certificates in
`/etc/ssl/certs/ca-certificates.crt`, which Debian, Ubuntu, Alpine, Arch and
current Fedora provide; a distribution without that file cannot download
archives or the pinned binary. CI checks these floors on every build.

## Building and releasing

lockstep builds itself with lockstep: its `CMakeLists.txt` loads
`cmake/lockstep.cmake`, which pins the previous release, and `lockstep.lock`
locks eacp and its dependencies. Bumping that pin is a manual commit after
each release. `-DLOCKSTEP_EXECUTABLE=<path>` runs a local build instead.

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
uploaded as the `bootstrap` artifact), and, when the
`PACKAGING_TOKEN` secret holds a token that can push to them, updates the
Homebrew formula in `jamierpond/homebrew-tap` and the Scoop manifest in
`jamierpond/scoop-bucket` (repository variables `TAP_REPOSITORY` and
`BUCKET_REPOSITORY` override those). Without the secret, run
`scripts/update-packaging.sh <version> SHA256SUMS` by hand. The templates are
in `packaging/`. The binaries are not code-signed.
