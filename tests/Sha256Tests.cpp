#include <Sha256.h>

#include <NanoTest/NanoTest.h>

#include <string>

using namespace nano;
using lockstep::computeSha256Hex;

auto tEmptyInput = test("Sha256/nistEmpty") = []
{
    check(computeSha256Hex("")
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
};

auto tAbc = test("Sha256/nistAbc") = []
{
    check(computeSha256Hex("abc")
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
};

auto tTwoBlocks = test("Sha256/nistTwoBlocks") = []
{
    check(
        computeSha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
        == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
};

auto tMillionAs = test("Sha256/nistMillionAs") = []
{
    constexpr auto millionCount = 1000000;
    check(computeSha256Hex(std::string(millionCount, 'a'))
          == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
};

auto tSplitUpdates = test("Sha256/splitUpdatesMatchOneUpdate") = []
{
    constexpr auto chunkSize = 7;
    auto text = std::string(1000, 'x');
    auto hasher = lockstep::Sha256 {};

    for (auto offset = std::size_t {0}; offset < text.size(); offset += chunkSize)
        hasher.update(std::string_view(text).substr(offset, chunkSize));

    check(lockstep::convertDigestToHex(hasher.finish()) == computeSha256Hex(text));
};
