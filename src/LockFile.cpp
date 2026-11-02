#include "LockFile.h"
#include "Sha256.h"

#include <eacp/Core/Utils/Files.h>

#include <Miro/Json.h>

#include <algorithm>
#include <stdexcept>

namespace lockstep
{
namespace
{
constexpr auto lockFileIndent = 2;

bool isNamed(const LockedPackage& package, const std::string& name)
{
    return package.name == name;
}
} // namespace

std::optional<LockedPackage> LockFile::findPackage(const std::string& name) const
{
    for (const auto& package: packages)
    {
        if (isNamed(package, name))
            return package;
    }

    return std::nullopt;
}

void LockFile::storePackage(const LockedPackage& package)
{
    for (auto& existing: packages)
    {
        if (isNamed(existing, package.name))
        {
            existing = package;
            return;
        }
    }

    packages.push_back(package);
}

std::string LockFile::computeRootHash() const
{
    auto sorted = packages;
    std::sort(sorted.begin(),
              sorted.end(),
              [](const auto& left, const auto& right)
              { return left.name < right.name; });

    auto hasher = Sha256 {};

    for (const auto& package: sorted)
        hasher.update(package.name + ' ' + package.hash + '\n');

    return convertDigestToHex(hasher.finish());
}

LockFile readLockFile(const eacp::FilePath& path)
{
    if (!eacp::Files::exists(path))
        return {};

    auto lock = Miro::createFromJSONString<LockFile>(eacp::Files::readFile(path));

    if (lock.version != lockFileFormatVersion)
    {
        throw std::runtime_error(
            path.str() + " has format version " + std::to_string(lock.version)
            + "; this lockstep reads " + std::to_string(lockFileFormatVersion));
    }

    return lock;
}

void writeLockFile(const eacp::FilePath& path, LockFile lock)
{
    std::sort(lock.packages.begin(),
              lock.packages.end(),
              [](const auto& left, const auto& right)
              { return left.name < right.name; });
    lock.root = lock.computeRootHash();

    auto text = Miro::toJSONString(lock, lockFileIndent) + '\n';
    auto bytes = eacp::Span<const std::uint8_t> {
        reinterpret_cast<const std::uint8_t*>(text.data()), int(text.size())};
    eacp::Files::writeFileAtomically(path, bytes);
}
} // namespace lockstep
