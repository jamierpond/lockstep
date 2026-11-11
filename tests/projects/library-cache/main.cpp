#include <greeter/greeter.h>
#include <greeter_generated.h>

int main()
{
    constexpr auto expectedGreeting = 42;
    constexpr auto expectedGenerated = 7;
    return greet() == expectedGreeting && greeterGenerated == expectedGenerated ? 0
                                                                                : 1;
}
