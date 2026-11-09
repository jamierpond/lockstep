#include "CMakeSnippet.h"

namespace lockstep
{
namespace
{
bool isEscapedInCMakeQuotes(char character)
{
    return character == '\\' || character == '"' || character == '$';
}
} // namespace

std::string quoteForCMake(std::string_view text)
{
    auto quoted = std::string {"\""};

    for (auto character: text)
    {
        if (isEscapedInCMakeQuotes(character))
            quoted += '\\';

        quoted += character;
    }

    return quoted + '"';
}
} // namespace lockstep
