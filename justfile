set windows-shell := ["cmd.exe", "/c"]

build_dir := "build"
prefix := home_directory() / ".local"

alias fmt := format

[doc('Show the recipe list')]
default:
    @just --list

[doc('Configure and build lockstep and its tests')]
build:
    cmake -G Ninja -B {{build_dir}} -DCMAKE_BUILD_TYPE=Release
    cmake --build {{build_dir}}

[doc('Build, then run every test')]
test: build
    ctest --test-dir {{build_dir}} --output-on-failure

[doc('clang-format every tracked source in place')]
[unix]
format:
    git ls-files '*.h' '*.cpp' '*.mm' | xargs clang-format -i

[doc('Build, then install lockstep into <prefix>/bin (default ~/.local/bin)')]
install prefix=prefix: build
    cmake --install {{build_dir}} --prefix "{{prefix}}"

[doc('Delete the build directories')]
clean:
    cmake -E rm -rf {{build_dir}} build-static
