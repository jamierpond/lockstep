#include <array>

int generatedNumber();
int firstObjectNumber();
int secondObjectNumber();

int main()
{
    constexpr auto numberTheToolWritesWithoutOffset = 5;
    constexpr auto expectedFirstObjectNumber = 1;
    constexpr auto expectedSecondObjectNumber = 2;
    constexpr auto expectedNumbers =
        std::array {numberTheToolWritesWithoutOffset + SAMPLE_TOOL_OFFSET,
                    expectedFirstObjectNumber,
                    expectedSecondObjectNumber};
    auto numbers =
        std::array {generatedNumber(), firstObjectNumber(), secondObjectNumber()};
    return numbers == expectedNumbers ? 0 : 1;
}
