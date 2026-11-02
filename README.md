# lockstep

Hermetic CMake dependencies. Every package is pinned in `lockstep.lock` to a
commit and a SHA-256 Merkle hash of its source tree, so a configure on any
machine gets byte-identical sources or fails. The interface is CPM's, and
dependencies that use CPM or FetchContent themselves go through it too.

## Use

Copy [`cmake/lockstep.cmake`](cmake/lockstep.cmake) into your project. It is
the only file you commit: about 40 lines that pin a lockstep release and its
per-platform SHA-256s. Load it during the top-level `project()`:

```cmake
cmake_minimum_required(VERSION 3.25)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/cmake/lockstep.cmake)
project(MyApp)

lockstep_add_package(NAME Miro GITHUB eyalamirmusic/Miro GIT_TAG main
                     OPTIONS "MIRO_BUILD_TESTS OFF")
lockstep_add_package(NAME zlib URL https://zlib.net/zlib-1.3.1.tar.gz DOWNLOAD_ONLY)
```

The first time, write the lock, then commit `lockstep.lock`:

```bash
cmake -B build -DLOCKSTEP_MODE=update
```

Every configure after that is `locked`: it fetches exactly the locked commit,
checks the content hash, and fails when a package is missing from the lock or
asked for at a different URL or ref. Configure ends with `lockstep: root <hash>`,
one hash over every package.

`lockstep_add_package` takes `NAME`, `GITHUB` (or `GITHUB_REPOSITORY`,
`GITLAB_REPOSITORY`, `GIT_REPOSITORY`), `GIT_TAG` or `VERSION` (meaning tag
`v<VERSION>`), `URL` for `.zip` and tar archives, `OPTIONS`, `PATCHES` (applied
with `git apply` before hashing), `SOURCE_SUBDIR`, `DOWNLOAD_ONLY`,
`EXCLUDE_FROM_ALL`, `SYSTEM`, `SOURCE_DIR` (use a local checkout),
`URL_HASH SHA256=<hex>` (checked against the archive), `GIT_SUBMODULES` and
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
| `LOCKSTEP_<Name>_SOURCE` | Use a local checkout of `<Name>` instead; configure prints a warning |
| `LOCKSTEP_CI` | `ON` makes a `LOCKSTEP_<Name>_SOURCE` override an error in locked mode |
| `LOCKSTEP_CACHE` | The shared cache (also an environment variable). Default `~/.cache/lockstep`, `$XDG_CACHE_HOME/lockstep`, or `%LOCALAPPDATA%\lockstep` on Windows |
| `LOCKSTEP_LOCK_FILE` | Default `<top-level source dir>/lockstep.lock` |
| `LOCKSTEP_EXECUTABLE` | Run this `lockstep` binary instead of downloading the pinned release |

### The command line

```bash
lockstep update            # re-resolve every package's ref and rewrite the lock
lockstep update Miro       # just Miro
lockstep verify            # check each cache entry against its lock hash
lockstep verify --full     # rehash every tree instead of trusting size/mtime stamps
lockstep root              # print the root hash
```

## Install

```bash
brew install jamierpond/tap/lockstep
scoop bucket add jamierpond https://github.com/jamierpond/scoop-bucket && scoop install lockstep
```

A project does not need it installed: its `lockstep.cmake` downloads the binary
it pins. Run inside a project, an installed `lockstep` reads
the version the project's `lockstep.cmake` pins and runs that exact release instead, so
the lock is always written by the version the project builds with.

### Switching from CPM

Add `cmake/lockstep.cmake` as above, delete your `CPM.cmake`, and configure
once with `-DLOCKSTEP_MODE=update`. Your `CPMAddPackage` calls work unchanged.
Your `include(CPM)` lines can go eventually but do not have to: under lockstep
they find a stub that does nothing, so a dependency's own lines never need
editing.

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
