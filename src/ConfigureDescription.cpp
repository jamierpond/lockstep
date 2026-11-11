#include "ConfigureDescription.h"

#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <cctype>

namespace lockstep
{
namespace
{
constexpr auto recordPartCount = 4;

std::string unescapeRecordValue(std::string_view value)
{
    auto text = std::string {};

    for (auto index = std::size_t {0}; index < value.size(); ++index)
    {
        if (value[index] != '\\' || index + 1 == value.size())
        {
            text += value[index];
            continue;
        }

        auto escaped = value[++index];

        if (escaped == 'n')
            text += '\n';
        else if (escaped == 'r')
            text += '\r';
        else if (escaped == 't')
            text += '\t';
        else
            text += escaped;
    }

    return text;
}
} // namespace

std::string replaceAll(std::string text, std::string_view from, std::string_view to)
{
    if (from.empty())
        return text;

    for (auto position = text.find(from); position != std::string::npos;
         position = text.find(from, position + to.size()))
        text.replace(position, from.size(), to);

    return text;
}

std::vector<std::string> splitCMakeList(const std::string& list)
{
    auto items = std::vector<std::string> {};

    for (const auto& item: eacp::Strings::split(list, ';'))
    {
        if (!item.empty())
            items.push_back(item);
    }

    return items;
}

bool isInsideDirectory(const std::string& path, const std::string& directory)
{
    auto normalizedPath = toForwardSlashes(path);
    auto normalizedDirectory = toForwardSlashes(directory);
    return normalizedPath == normalizedDirectory
           || normalizedPath.starts_with(normalizedDirectory + "/");
}

std::string findTargetField(const ConfigureDescription& description,
                            const std::string& target,
                            std::string_view field)
{
    auto found = description.targets.find(target);

    if (found == description.targets.end())
        return {};

    return findField(found->second, field);
}

const RecordFields& findSubject(const std::map<std::string, RecordFields>& subjects,
                                const std::string& subject)
{
    static const auto none = RecordFields {};
    auto found = subjects.find(subject);

    if (found == subjects.end())
        return none;

    return found->second;
}

std::string toUpperCase(std::string text)
{
    for (auto& character: text)
        character =
            static_cast<char>(std::toupper(static_cast<unsigned char>(character)));

    return text;
}

std::string toForwardSlashes(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

ConfigureDescription parseConfigureDescription(std::string_view text)
{
    auto description = ConfigureDescription {};

    for (auto line: eacp::Strings::split(text, '\n'))
    {
        // CMake's file(WRITE) ends each line with \r\n on Windows; the module
        // escapes every carriage return in a value, so this one is the line's.
        if (line.ends_with('\r'))
            line.pop_back();

        auto parts = eacp::Strings::split(line, '\t', recordPartCount);

        if (parts.size() != recordPartCount)
            continue;

        auto field = std::pair {parts[2], unescapeRecordValue(parts[3])};
        const auto& kind = parts[0];

        if (kind == "global")
            description.globals.push_back(field);
        else if (kind == "target")
            description.targets[parts[1]].push_back(field);
        else if (kind == "directory")
            description.directories[parts[1]].push_back(field);
        else if (kind == "variable")
            description.variables[parts[1]].push_back(field);
    }

    return description;
}

std::string findField(const RecordFields& fields, std::string_view name)
{
    for (const auto& [field, value]: fields)
    {
        if (field == name)
            return value;
    }

    return {};
}

std::optional<ConfiguredPackage>
    findOwningPackage(const std::vector<ConfiguredPackage>& packages,
                      const std::string& directory)
{
    auto owner = std::optional<ConfiguredPackage> {};

    for (const auto& package: packages)
    {
        auto isLongerMatch =
            !owner || package.sourceDirectory.size() > owner->sourceDirectory.size();

        if (isInsideDirectory(directory, package.sourceDirectory) && isLongerMatch)
            owner = package;
    }

    return owner;
}

std::set<std::string> collectLinkClosure(const ConfigureDescription& description,
                                         const std::string& target)
{
    auto closure = std::set<std::string> {};
    auto pending =
        splitCMakeList(findTargetField(description, target, "LINKED_TARGETS"));

    for (const auto& linked: splitCMakeList(
             findTargetField(description, target, "INTERFACE_LINKED_TARGETS")))
        pending.push_back(linked);

    while (!pending.empty())
    {
        auto next = pending.back();
        pending.pop_back();

        if (next == target || !closure.insert(next).second)
            continue;

        for (const auto& linked: splitCMakeList(
                 findTargetField(description, next, "INTERFACE_LINKED_TARGETS")))
            pending.push_back(linked);
    }

    return closure;
}
} // namespace lockstep
