# Design

## What lockstep is for

Builds that come out the same on every machine. `lockstep.lock` pins every
dependency, including the dependencies of dependencies, to a commit and a
content hash. A branch or a moved tag never changes a build; only
`lockstep update` does.

## Compared with CPM

lockstep is better when a build has to be reproducible:

- Every package is built from the commit in the lock, never from where a
  branch or tag points today.
- A Merkle hash checks the fetched sources, and one root hash covers the
  whole dependency tree.
- Dependencies that use CPM themselves are in the same lock, so the whole
  tree is pinned in one place.
- In locked mode nothing is fetched or found that the lock doesn't name.

CPM is still better at these:

- It has years of edge cases from real projects behind it.
- It is one file and needs no binary.
- It can fall back to `find_package` for an installed package.

## Installed packages

lockstep never falls back to `find_package`. A package found on the machine
depends on whatever happens to be installed there, so two machines can
build against different versions without anyone noticing.

If a package really has to come from the system, such as a driver, a
vendor SDK or a distribution's Qt, the planned interface is opt-in per
package:

```cmake
lockstep_add_package(NAME Qt6 SYSTEM_PACKAGE ON)
```

That package alone goes through `find_package`. lockstep writes the version
and path it found into the lock, so the lock shows what was not built from
source, and warns in locked mode when the version changes. Not built yet:
it waits for a project that needs it.

## Everything is opt-in

| Layer | Default | Opt in or out |
|---|---|---|
| Lock on dependency commits | on | `mode = "unlocked"` turns it off |
| Merkle content check | on | `verify = "off"` turns it off |
| Cache of packages' built static libraries, executables and object files | off | `LOCKSTEP_LIBRARY_CACHE=ON` in the environment for every project, or `-DLOCKSTEP_LIBRARY_CACHE=ON` for one build directory |
| One library cache for several machines | off | `LOCKSTEP_LIBRARY_CACHE_LOCATION=<directory>` or `[user@]host:<directory>`, in the environment or as `-D` on a build directory |
| Library cache builds by another compiler | off | `LOCKSTEP_LIBRARY_CACHE_COMPILER_RANGE=<range>`, in the environment, as `-D` on a build directory or as `compilerRange` in `lockstep.json` |
| Library cache builds against another Apple SDK | off | `LOCKSTEP_LIBRARY_CACHE_SDK_RANGE=<range>`, the same ways, or `sdkRange` |
| Library cache size limit | 5 GiB on each machine, 20 GiB at a location | `LOCKSTEP_LIBRARY_CACHE_MAX_SIZE` and `LOCKSTEP_LIBRARY_CACHE_LOCATION_MAX_SIZE`, or `maxSize` and `locationMaxSize` |
| `lockstep.json` settings | off | add the file; each setting above can be in its `libraryCache` |
| Record the toolchain version | off | `toolchain = "record"` |
| Enforce the toolchain version | off | `toolchain = "enforce"` |
| Fetched toolchains | off | `toolchain = { fetch = "llvm-19.1.7" }` |

Upgrading lockstep never turns on a layer, and each layer works without
the others.

## Roadmap

1. Move real projects onto the lock (tamber-web first).
2. Package declarations in `lockstep.json`, which already holds the library
   cache settings: a declarative manifest, with per-platform files
   (`lockstep.macos.json`) merged over it. Objects merge by key, values
   replace, lists append, `"remove": [...]` takes items out. lockstep
   generates CMake from it, so IDEs and packages without a manifest keep
   working. It reads the JSON through Miro, as it reads the lock.
3. Recording the toolchain version.
4. Fetched toolchains, when someone needs them.
