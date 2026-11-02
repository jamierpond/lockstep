#include "Status.h"

#include <eacp/Core/Utils/Console.h>

namespace lockstep
{
void printStatus(const std::string& line)
{
    eacp::Console::printError("lockstep: " + line);
}
} // namespace lockstep
