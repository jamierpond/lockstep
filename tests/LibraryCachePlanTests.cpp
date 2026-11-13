#include <LibraryCachePlan.h>

#include <NanoTest/NanoTest.h>

#include <string>
#include <vector>

using namespace nano;

namespace
{
auto tUsedTargets = test("LibraryCachePlan/usesOnlyLibrariesTheProjectReaches") = []
{
    auto description = lockstep::parseConfigureDescription(
        "target\tapp\tSOURCE_DIR\t/project\n"
        "target\tapp\tTYPE\tEXECUTABLE\n"
        "target\tapp\tLINKED_TARGETS\tgreeter\n"
        "target\tapp\tSOURCES\tmain.cpp;$<TARGET_OBJECTS:taken_objects>\n"
        "target\tgreeter\tSOURCE_DIR\t/cache/src/1111\n"
        "target\tgreeter\tTYPE\tSTATIC_LIBRARY\n"
        "target\ttaken_objects\tSOURCE_DIR\t/cache/src/1111\n"
        "target\ttaken_objects\tTYPE\tOBJECT_LIBRARY\n"
        "target\ttool\tSOURCE_DIR\t/cache/src/1111\n"
        "target\ttool\tTYPE\tEXECUTABLE\n"
        "target\ttool\tLINKED_TARGETS\ttool_support\n"
        "target\ttool_support\tSOURCE_DIR\t/cache/src/1111\n"
        "target\ttool_support\tTYPE\tSTATIC_LIBRARY\n"
        "target\tunused\tSOURCE_DIR\t/cache/src/1111\n"
        "target\tunused\tTYPE\tSTATIC_LIBRARY\n");
    auto packages = std::vector<lockstep::ConfiguredPackage> {
        {"greeter", "1111", "/cache/src/1111"}};
    auto names = lockstep::PackageVariableNames {};
    auto inputs = lockstep::LibraryCacheInputs {
        description, packages, names, {"/build", "/cache"}};
    auto used = lockstep::collectTargetsTheBuildUses(inputs);
    check(used.contains("greeter"));
    check(used.contains("taken_objects"));
    check(used.contains("tool_support"));
    check(!used.contains("unused"));
};
} // namespace
