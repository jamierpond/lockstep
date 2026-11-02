# What lockstep needs from eacp

lockstep had to write these because eacp Core lacks them. Each belongs in
eacp, and once it lands there lockstep's copy is deleted. None is
implemented yet. Line numbers are from lockstep `origin/main` at the
2026-11-02 review.

| Proposed eacp addition | Header | Replaces in lockstep |
|---|---|---|
| `Sha256` (`update`, `finish`), `computeSha256Hex(text)`, `computeFileSha256(path)` | `Core/Utils/Sha256.h` | `src/Sha256.{h,cpp}`, `PinnedRelease.cpp:71`, `PackageCache.cpp:298`, `MerkleHash.cpp:67` |
| `Strings::toHex(Span<const uint8_t>)` | `Core/Utils/Strings.h` | `Sha256.cpp:167` `convertDigestToHex` (eacp has only `parseHex`) |
| `FilePath::relativeTo(base)`, `FilePath::lexicallyNormal()` | `Core/Utils/FilePath.h` | `Commands.cpp:35-45`, `:65-66` (`std::filesystem` through `toStdPath`) |
| `FilePath::currentDirectory()` | `Core/Utils/FilePath.h` | `Main.cpp:59`, `:71` |
| `FilePath::withSuffix(suffix)` | `Core/Utils/FilePath.h` | `sibling(fileName() + ".x")` in `Download.cpp:17`, `Git.cpp:174`, `PackageCache.cpp:31`, `PinnedRelease.cpp:133` |
| `Files::isSameFile(a, b)` | `Core/Utils/Files.h` | `Commands.cpp:379` `std::filesystem::equivalent` |
| `Files::setExecutable(path)` | `Core/Utils/Files.h` | `PinnedRelease.cpp:62` `makeExecutable` |
| `Files::readSymlink(path)` | `Core/Utils/Files.h` | `MerkleHash.cpp:41` `read_symlink` |
| `Files::writeFileAtomically(path, std::string_view)` | `Core/Utils/Files.h` | `LockFile.cpp:90-92` `reinterpret_cast` into a `Span`; `writeFile` got this overload in eacp #177 |
| `FilePath::wide()` emitting `\` separators, as `nativeString()` already does | `Core/Utils/FilePath.h` | `src/LongPath.{h,cpp}`: `wide()` keeps `/`, so `toStdPath`, `MemoryMappedFile` and every `Files` call reach Windows with `/` and a `\\?\` long path never works |
| `Platform::architectureName()` | `Core/Utils/Platform.h` | `PinnedRelease.cpp:30-37` architecture `#if` |

SHA-256 matters most: the rest are thin wrappers, but it is about 170
lines of real code.
