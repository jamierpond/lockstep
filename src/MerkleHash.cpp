#include "MerkleHash.h"
#include "Sha256.h"

#include <eacp/Core/Utils/File.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto excludedDirectoryName = std::string_view {".git"};

bool isExcludedEntry(const eacp::Files::DirectoryEntry& entry)
{
    return entry.path.fileName() == excludedDirectoryName;
}

eacp::Files::DirectoryOptions getEveryEntryOptions(bool recursive)
{
    auto options = eacp::Files::DirectoryOptions {};
    options.recursive = recursive;
    options.includeHidden = true;
    options.symlinks = eacp::Files::Symlinks::skip;
    options.onError = [](const eacp::Files::TraversalError& error)
    {
        throw std::runtime_error("cannot read " + error.path.str() + ": "
                                 + error.message);
        return eacp::Files::Visit::stop;
    };
    return options;
}

std::string computeSymlinkHash(const eacp::FilePath& link)
{
    auto target =
        eacp::FilePath(std::filesystem::read_symlink(eacp::toStdPath(link)));
    return computeSha256Hex(target.str());
}

std::string describeEntryKind(eacp::Files::EntryKind kind)
{
    switch (kind)
    {
        case eacp::Files::EntryKind::file:
            return "file";
        case eacp::Files::EntryKind::directory:
            return "directory";
        case eacp::Files::EntryKind::symlink:
            return "symlink";
        case eacp::Files::EntryKind::other:
            break;
    }

    throw std::runtime_error("cannot hash a special file");
}

std::string computeEntryHash(const eacp::Files::DirectoryEntry& entry)
{
    switch (entry.kind)
    {
        case eacp::Files::EntryKind::file:
            return convertDigestToHex(computeFileSha256(entry.path));
        case eacp::Files::EntryKind::directory:
            return computeMerkleHash(entry.path);
        case eacp::Files::EntryKind::symlink:
            return computeSymlinkHash(entry.path);
        case eacp::Files::EntryKind::other:
            break;
    }

    throw std::runtime_error(entry.path.str()
                             + " is neither a file, a directory "
                               "nor a symlink, so it cannot be hashed");
}
} // namespace

std::string computeMerkleHash(const eacp::FilePath& directory)
{
    auto entries =
        eacp::Files::listDirectory(directory, getEveryEntryOptions(false));

    std::sort(entries.begin(),
              entries.end(),
              [](const auto& left, const auto& right)
              { return left.path.fileName() < right.path.fileName(); });

    auto hasher = Sha256 {};

    for (const auto& entry: entries)
    {
        if (isExcludedEntry(entry))
            continue;

        auto line = describeEntryKind(entry.kind) + ' ' + computeEntryHash(entry)
                    + ' ' + entry.path.fileName() + '\n';
        hasher.update(line);
    }

    return convertDigestToHex(hasher.finish());
}

std::string computeTreeStamp(const eacp::FilePath& directory)
{
    auto hasher = Sha256 {};
    auto prefixLength = directory.str().size();

    eacp::Files::forEachEntry(directory,
                              getEveryEntryOptions(true),
                              [&](const eacp::Files::DirectoryEntry& entry)
                              {
                                  if (isExcludedEntry(entry))
                                      return eacp::Files::Visit::skipChildren;

                                  auto line = entry.path.str().substr(prefixLength);

                                  if (entry.kind == eacp::Files::EntryKind::file)
                                  {
                                      auto file = entry.file();
                                      line +=
                                          ' ' + std::to_string(file.size()) + ' '
                                          + std::to_string(file.modificationTime());
                                  }

                                  hasher.update(line + '\n');
                                  return eacp::Files::Visit::next;
                              });

    return convertDigestToHex(hasher.finish());
}
} // namespace lockstep
