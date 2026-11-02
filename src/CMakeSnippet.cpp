#include "CMakeSnippet.h"

namespace lockstep
{
std::string quoteForCMake(std::string_view text)
{
    auto quoted = std::string {"\""};

    for (auto character: text)
    {
        if (character == '\\' || character == '"' || character == '$')
            quoted += '\\';

        quoted += character;
    }

    return quoted + '"';
}
} // namespace lockstep
