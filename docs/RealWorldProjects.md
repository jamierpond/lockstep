# Real-world CPM projects

Run on 2026-11-02 on macOS 27 (arm64, AppleClang 21, CMake 4.4) against
lockstep `origin/main` at 83ad77b, built in a separate clone. That commit does
not compile on AppleClang (see the last paragraph), so the binary has one
local change: a defaulted `operator==` on `LockedSubmodule`.

Each project was configured unchanged, with
`-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<lockstep>/cmake/lockstep.cmake
-DLOCKSTEP_EXECUTABLE=<build>/lockstep`; its own `CPM.cmake` stayed in place.
Steps: plain CPM configure, `-DLOCKSTEP_MODE=update`, a clean locked configure,
a build, then `update` in a second clone at another path, comparing lock bytes
and root hash. "Deps" is the lock's package count; every lock that was written
matched the packages plain CPM fetched, except audio-plugin-template. The three
Boost rows are small consumers written for this test; AddBoost uses
`add_boost()` from Arniiiii/AddBoost.cmake 3.7.3.

| Project | Deps | Root hash | Update | Locked | Build | Same lock in 2nd clone |
| --- | --- | --- | --- | --- | --- | --- |
| TheLartians/ModernCppStarter (`all/`) | 6 | `9321ee11…` | pass | pass | pass | yes |
| cpp-best-practices/cmake_template | 6 | `4a50c1a5…` | pass | pass | fails, as plain CPM does (cppcheck `-Werror`) | yes |
| MethanePowered/MethaneKit | 14 | `6fe536d3…` | pass | pass | fails, as plain CPM does (no Metal toolchain) | yes |
| gameguild-gg/SDL3-CPM-CMake-Example | 4 | `1a249449…` | pass | pass | pass | yes |
| cpm-cmake/CPMLicenses.cmake (`test/project`) | 5 | `ab283697…` | pass | pass | pass | yes |
| adrianderstroff/opengl-starter | 4 | `3f1e087c…` | pass | pass | fails, as plain CPM does (no Python `jinja2`) | yes |
| gameguild-gg/raylib-cpm-cmake-boilerplate | 2 | `8b334379…` | pass | pass | pass | yes |
| simdjson/simdjson_cpm | 1 | `a3f0d730…` | pass | pass | pass | yes |
| eyalamirmusic/cpp-cmake-starter | 1 | `190377da…` | pass | pass | pass | yes |
| Klebert-Engineering/openssl-cmake (`test/`, FetchContent) | 1 | `b2027d6c…` | pass | pass | pass | yes |
| Boost 1.88.0 tarball, `URL` + `URL_HASH SHA256` | 1 | `d369d646…` | pass | pass | pass | yes |
| Boost 1.88.0 git, `GIT_SUBMODULES` with 6 paths | 1 (6 submodules) | `5f72698b…` | pass | pass | pass | yes |
| Boost 1.88.0 git, all submodules | – | – | fails | – | – | – |
| JanWilczek/audio-plugin-template | 0, should be 2 | – | wrong lock | – | pass in 1st clone only | no: 2nd clone fails |
| wolfpld/tracy (`profiler/`) | – | – | fails | – | – | – |
| dcoeurjo/GeometryProcessing-cmake-recipes | – | – | fails | – | – | – |
| Arniiiii/AddBoost.cmake | – | – | fails | – | – | – |
| sillydan1/aaltitoad | – | – | fails | – | – | – |
| jhasse/jngl | – | – | fails | – | – | – |

Boost timings, each with an empty `LOCKSTEP_CACHE`. Tarball: first fetch 17 s,
751 MB of cache (a 95 MB archive plus a 655 MB unpacked tree). Six submodules:
8 s, 17 MB. All submodules: failed after 140 s and 162 MB. A warm locked
configure, Merkle check included, took about 1 s for both that passed. The
six-submodule lock lists each submodule's path, URL and commit, and they match
`git ls-tree` in the superproject.

## Failures

Boost with all submodules: `lockstep: git config --file
.../staging/tree/tree/libs/parser/.gitmodules --get-regexp
^submodule\..*\.(path|url)$ failed`. `libs/parser` has an empty `.gitmodules`,
and `git config --get-regexp` exits 1 when nothing matches. The submodule walk
in `src/Git.cpp` treats that exit code as an error when it should mean "no
submodules".

tracy: `lockstep: .gitmodules lists suite/auto-sync/vendor/tree-sitter-cpp,
which is not a submodule at b2bf6327…` (capstone 6.0.0-Alpha11). git ignores
`.gitmodules` entries that have no gitlink, and so does CPM. `src/Git.cpp:249`
throws instead of skipping the entry.

GeometryProcessing-cmake-recipes (run with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`,
which plain CPM needs for CLI11): `OPTIONS entry 'LIBIGL_WITH_CGAL' is not
'NAME VALUE'`. CPM treats an option with no value as `ON`. lockstep's OPTIONS
parser in `src/PackageArguments.cpp` refuses it.

AddBoost.cmake: `lockstep does not know the option 'filesystem'`. AddBoost
passes `FIND_PACKAGE_ARGUMENTS "COMPONENTS system\;filesystem"`. The forwarding
macros in `cmake/lockstep-module.cmake` (`CPMAddPackage` calls
`_lockstep_run(add ${ARGV})`) expand `${ARGV}` again, so the escaped `\;`
splits the value in two. CPM is a function and reads `"${ARGN}"`, so it keeps
the value whole.

audio-plugin-template: it calls `CPMAddPackage(NAME JUCE GITHUB_REPOSITORY
juce-framework/JUCE GIT_TAG 8.0.12 SOURCE_DIR ${LIB_DIR}/juce)`. In CPM,
`SOURCE_DIR` next to a repository means "clone into this directory". In
`src/PackageArguments.cpp:340`, the `cpm` dialect makes any `SOURCE_DIR` a
local package. The repository is then ignored, nothing is recorded, and the
lock has no packages. The first clone only worked because the plain-CPM run had
already filled `libs/`. In the second clone `libs/juce` does not exist, nothing
is added and gives no error, and configure fails with `Unknown CMake command
"juce_add_plugin"`.

aaltitoad (run with `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`): `find_package(argvparse)`
fails in `src/cli/simulator/CMakeLists.txt:18` after `CPMAddPackage("gh:sillydan1/argvparse@1.2.3")`.
CPM writes a find-package redirect for every package it adds, so a later
`find_package` finds the added package. lockstep's add snippet
(`src/Commands.cpp`) writes nothing like it. Plain CPM got past this point but
then failed at generate on `FLEX_INCLUDE_DIR`, a problem with this machine,
not lockstep.

jngl: `URL_HASH SHA1=d2c07e1e… is not SHA256=<hex>, the only hash lockstep
checks` (`src/PackageArguments.cpp:311`). lockstep could accept SHA1 here,
because it checks every tree with its own Merkle hash anyway. Plain CPM fails on
this machine too, at `pkg_check_modules(ogg)`.

Building lockstep: at 83ad77b, `src/Commands.cpp:117` calls `std::find` over
`LockedSubmodule`, which has no `operator==`, so AppleClang refuses it.

Minor: the lock's `"bootstrap"` field is a path relative to the source
directory. When the bootstrap file lives outside the project, as in these runs,
the lock bytes change with the clone's location. The second clones only matched
because they sat at the same depth.
