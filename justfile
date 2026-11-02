set windows-shell := ["bash", "-c"]

build_dir := "build"
prefix := env_var_or_default("PREFIX", env_var("HOME") / ".local")

build:
    cmake -G Ninja -B {{build_dir}} -DCMAKE_BUILD_TYPE=Release
    cmake --build {{build_dir}}

test: build
    ctest --test-dir {{build_dir}} --output-on-failure

fmt:
    clang-format -i src/*.cpp src/*.h tests/*.cpp tests/*.h tests/projects/*/main.cpp

install prefix=prefix: build
    cmake --install {{build_dir}} --prefix "{{prefix}}"

clean:
    rm -rf {{build_dir}} build-static
