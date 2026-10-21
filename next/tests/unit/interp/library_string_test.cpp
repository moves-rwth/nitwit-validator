#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "library_test_support.hpp"

namespace nitwit::interp::test {
namespace {

using namespace std::string_literals;
using value::InputId;

class StringTest : public LibraryTest {
protected:
    Int128 compare(const char* function, std::string_view first, std::string_view second,
                   std::optional<Int128> count = std::nullopt) {
        std::vector arguments{pointer(string(first)), pointer(string(second))};
        if (count) {
            arguments.push_back(size(*count));
        }
        return callInteger(function, arguments);
    }
};

INSTANTIATE_TEST_SUITE_P(DataModels, StringTest, testing::Values(DataModel::ILP32, DataModel::LP64), modelName);

// ---- Copying ----

TEST_P(StringTest, MemcpyCopiesBytesInitializationAndOrigins) {
    const Address source = buffer(8);
    writeInteger(source, 0x11223344, ScalarType::Int);
    memory().setOrigin(source, 4, InputId{3});
    const Address destination = buffer(8);
    EXPECT_EQ(callPointer("memcpy", {pointer(destination), pointer(source), size(8)}), destination);
    EXPECT_EQ(readInteger(destination, ScalarType::Int), 0x11223344);
    EXPECT_EQ(memory().origin(destination, 4), InputId{3});
    EXPECT_TRUE(memory().isInitialized(destination, 4));
    EXPECT_FALSE(memory().isInitialized(destination + 4, 4));
}

TEST_P(StringTest, MemcpyBetweenOverlappingObjectsIsUndefined) {
    const Address block = buffer(16);
    EXPECT_TRUE(isUndefined(call("memcpy", {pointer(block + 2), pointer(block), size(8)}), "overlapping"));
    EXPECT_TRUE(isUndefined(call("memcpy", {pointer(block), pointer(block), size(1)}), "overlapping"));
    EXPECT_EQ(callPointer("memcpy", {pointer(block + 8), pointer(block), size(8)}), block + 8);
}

TEST_P(StringTest, MemcpyOfZeroBytesAccessesNothing) {
    EXPECT_EQ(callPointer("memcpy", {pointer(0), pointer(0), size(0)}), 0u);
}

TEST_P(StringTest, MemcpyReportsWhichSideFails) {
    const Address small = buffer(4);
    const Address large = buffer(8);
    EXPECT_TRUE(isUndefined(call("memcpy", {pointer(large), pointer(small), size(8)}), "memcpy reads past the end"));
    EXPECT_TRUE(isUndefined(call("memcpy", {pointer(small), pointer(large), size(8)}), "memcpy writes past the end"));
    EXPECT_TRUE(isUndefined(call("memcpy", {pointer(string("ro", Region::ReadOnly)), pointer(large), size(2)}),
                            "writes to read-only memory"));
    EXPECT_TRUE(isUndefined(call("__builtin_memcpy", {pointer(0), pointer(large), size(1)}),
                            "__builtin_memcpy writes through a null pointer"));
}

TEST_P(StringTest, MemmoveHandlesOverlap) {
    const Address block = bytesObject("abcdefgh");
    EXPECT_EQ(callPointer("memmove", {pointer(block + 2), pointer(block), size(5)}), block + 2);
    EXPECT_EQ(readText(block, 8), "ababcdeh");
}

TEST_P(StringTest, MemsetFillsWithTheByteValue) {
    const Address block = buffer(4);
    EXPECT_EQ(callPointer("memset", {pointer(block), intValue(0x1AB), size(3)}), block);
    EXPECT_EQ(readText(block, 3), "\xab\xab\xab");
    EXPECT_TRUE(memory().isInitialized(block, 3));
    EXPECT_FALSE(memory().isInitialized(block + 3, 1));
    EXPECT_TRUE(isUndefined(call("memset", {pointer(block), intValue(0), size(5)}), "past the end"));
}

TEST_P(StringTest, StrcpyCopiesTheTerminator) {
    const Address destination = buffer(8);
    EXPECT_EQ(callPointer("strcpy", {pointer(destination), pointer(string("abc"))}), destination);
    EXPECT_EQ(readText(destination, 4), "abc\0"s);
    EXPECT_FALSE(memory().isInitialized(destination + 4, 1));
}

TEST_P(StringTest, StrcpyErrors) {
    const Address destination = buffer(3);
    EXPECT_TRUE(isUndefined(call("strcpy", {pointer(destination), pointer(string("abc"))}), "writes past the end"));
    EXPECT_TRUE(isUndefined(call("strcpy", {pointer(buffer(8)), pointer(bytesObject("abc"))}), "reads past the end"));
    const Address overlapping = string("abcdef");
    EXPECT_TRUE(isUndefined(call("strcpy", {pointer(overlapping + 1), pointer(overlapping)}), "overlapping"));
    EXPECT_TRUE(isUndefined(call("strcpy", {pointer(overlapping), pointer(overlapping + 2)}), "overlapping"));
}

TEST_P(StringTest, StrncpyPadsWithNullCharacters) {
    const Address destination = buffer(6);
    EXPECT_EQ(callPointer("strncpy", {pointer(destination), pointer(string("ab")), size(6)}), destination);
    EXPECT_EQ(readText(destination, 6), "ab\0\0\0\0"s);
}

TEST_P(StringTest, StrncpyDoesNotTerminateALongSource) {
    const Address destination = buffer(4);
    callPointer("strncpy", {pointer(destination), pointer(bytesObject("abcdef")), size(3)});
    EXPECT_EQ(readText(destination, 3), "abc");
    EXPECT_FALSE(memory().isInitialized(destination + 3, 1));
}

TEST_P(StringTest, StrcatAppends) {
    const Address destination = buffer(8);
    callPointer("strcpy", {pointer(destination), pointer(string("ab"))});
    EXPECT_EQ(callPointer("strcat", {pointer(destination), pointer(string("cde"))}), destination);
    EXPECT_EQ(readString(destination), "abcde");
    EXPECT_TRUE(isUndefined(call("strcat", {pointer(destination), pointer(string("fgh"))}), "writes past the end"));
    EXPECT_TRUE(isUndefined(call("strcat", {pointer(destination), pointer(destination + 1)}), "overlapping"));
}

TEST_P(StringTest, StrncatAppendsAtMostNCharactersAndATerminator) {
    const Address destination = buffer(8);
    callPointer("strcpy", {pointer(destination), pointer(string("ab"))});
    callPointer("strncat", {pointer(destination), pointer(bytesObject("cdef")), size(2)});
    EXPECT_EQ(readText(destination, 5), "abcd\0"s);
    callPointer("strncat", {pointer(destination), pointer(string("x")), size(5)});
    EXPECT_EQ(readText(destination, 6), "abcdx\0"s);
}

TEST_P(StringTest, StrdupAllocatesACopy) {
    const Address copy = callPointer("strdup", {pointer(string("text"))});
    EXPECT_EQ(memory().find(copy)->region, Region::Heap);
    EXPECT_EQ(memory().find(copy)->size, 5u);
    EXPECT_EQ(readString(copy), "text");
    const Address prefix = callPointer("strndup", {pointer(bytesObject("abcdef")), size(3)});
    EXPECT_EQ(memory().find(prefix)->size, 4u);
    EXPECT_EQ(readString(prefix), "abc");
    EXPECT_EQ(readString(callPointer("strndup", {pointer(string("ab")), size(10)})), "ab");
}

// ---- Comparison: the values glibc 2.39 returned natively (-m64: the difference, -m32: the sign) ----

TEST_P(StringTest, StrcmpReturnsWhatGlibcReturns) {
    EXPECT_EQ(compare("strcmp", "ab", "ac"), -1);
    EXPECT_EQ(compare("strcmp", "ac", "ab"), 1);
    EXPECT_EQ(compare("strcmp", "a", "abc"), lp64() ? -98 : -1);
    EXPECT_EQ(compare("strcmp", "abc", "a"), lp64() ? 98 : 1);
    EXPECT_EQ(compare("strcmp", "a\xff", "a"), lp64() ? 255 : 1); // unsigned char comparison (C17 7.24.4p1)
    EXPECT_EQ(compare("strcmp", "x", "x"), 0);
    EXPECT_EQ(compare("strcmp", "", ""), 0);
}

TEST_P(StringTest, StrncmpReturnsWhatGlibcReturns) {
    EXPECT_EQ(compare("strncmp", "abcd", "abzz", 3), lp64() ? -23 : -1);
    EXPECT_EQ(compare("strncmp", "abcd", "abzz", 2), 0);
    EXPECT_EQ(compare("strncmp", "a", "\xf0", 1), lp64() ? -143 : -1);
    EXPECT_EQ(compare("strncmp", "abc", "abd", 0), 0);
}

TEST_P(StringTest, MemcmpReturnsWhatGlibcReturns) {
    EXPECT_EQ(compare("memcmp", "abc", "abd", 3), -1);
    EXPECT_EQ(compare("memcmp", "abd", "abc", 3), 1);
    EXPECT_EQ(compare("memcmp", "a\x01", "a\xf1", 2), lp64() ? -240 : -1);
    EXPECT_EQ(compare("memcmp", "abc", "abc", 3), 0);
    EXPECT_EQ(compare("bcmp", "a\x01", "a\xf1", 2), lp64() ? -240 : -1); // glibc's bcmp is memcmp
}

TEST_P(StringTest, StrcasecmpReturnsWhatGlibcReturns) {
    EXPECT_EQ(compare("strcasecmp", "abc", "ABC"), 0);
    EXPECT_EQ(compare("strcasecmp", "abc", "abd"), -1);
    EXPECT_EQ(compare("strcasecmp", "a", "abc"), lp64() ? -98 : -1);
    EXPECT_EQ(compare("strcasecmp", "Z", "a"), lp64() ? 25 : 1);
    EXPECT_EQ(compare("strcasecmp", "[", "a"), lp64() ? -6 : -1); // '[' lies between 'Z' and 'a'
    EXPECT_EQ(compare("strcasecmp", "a\xff", "A"), lp64() ? 255 : 1);
    EXPECT_EQ(compare("strcasecmp", "hello", "HELLO world"), lp64() ? -32 : -1);
    EXPECT_EQ(compare("strncasecmp", "hello", "HELLO world", 5), 0);
    EXPECT_EQ(compare("strncasecmp", "a", "abc", 2), lp64() ? -98 : -1);
}

TEST_P(StringTest, BsdAndPosixRelatives) {
    const Address destination = buffer(16);
    EXPECT_EQ(callPointer("mempcpy", {pointer(destination), pointer(string("abc")), size(3)}), destination + 3);
    EXPECT_EQ(callPointer("stpcpy", {pointer(destination), pointer(string("hello"))}), destination + 5);
    EXPECT_EQ(readText(destination, 6), "hello\0"s);
    EXPECT_TRUE(isUndefined(call("stpcpy", {pointer(destination + 1), pointer(destination)}), "overlapping"));
    EXPECT_EQ(call("bcopy", {pointer(destination), pointer(destination + 1), size(5)}).kind,
              LibraryResult::Kind::Return); // bcopy(source, destination, n) may overlap
    EXPECT_EQ(readText(destination, 6), "hhello"s);
    EXPECT_EQ(call("bzero", {pointer(destination), size(2)}).kind, LibraryResult::Kind::Return);
    EXPECT_EQ(readText(destination, 3), "\0\0e"s);
    const Address text = string("abcabc");
    EXPECT_EQ(callPointer("index", {pointer(text), intValue('b')}), text + 1);
    EXPECT_EQ(callPointer("rindex", {pointer(text), intValue('b')}), text + 4);
}

TEST_P(StringTest, ComparisonsStopAtTheFirstDifference) {
    // Neither array is terminated, but the strings differ before their ends.
    EXPECT_EQ(callInteger("strcmp", {pointer(bytesObject("ab")), pointer(bytesObject("ax"))}), lp64() ? -22 : -1);
    EXPECT_EQ(callInteger("memcmp", {pointer(bytesObject("ab")), pointer(bytesObject("xb")), size(100)}),
              lp64() ? -23 : -1);
    EXPECT_TRUE(isUndefined(call("strcmp", {pointer(bytesObject("ab")), pointer(bytesObject("ab"))}), "past the end"));
    EXPECT_TRUE(
        isUndefined(call("memcmp", {pointer(bytesObject("ab")), pointer(bytesObject("ab")), size(3)}), "past the end"));
}

// ---- Search ----

TEST_P(StringTest, StrlenAndStrnlen) {
    EXPECT_EQ(callInteger("strlen", {pointer(string("hello"))}), 5);
    EXPECT_EQ(callInteger("strlen", {pointer(string(""))}), 0);
    EXPECT_EQ(callInteger("strnlen", {pointer(string("hello")), size(3)}), 3);
    EXPECT_EQ(callInteger("strnlen", {pointer(bytesObject("abc")), size(3)}), 3);
    EXPECT_EQ(callInteger("strnlen", {pointer(string("ab")), size(10)}), 2);
}

TEST_P(StringTest, StrlenReturnsASizeT) {
    const LibraryResult result = call("strlen", {pointer(string("abc"))});
    EXPECT_EQ(result.value.bits(), value::Value::integer(3, format(context.target().sizeType())).bits());
}

TEST_P(StringTest, StrlenOfInvalidStrings) {
    EXPECT_TRUE(isUndefined(call("strlen", {pointer(bytesObject("abc"))}), "strlen reads past the end of the object"));
    EXPECT_TRUE(isUndefined(call("strlen", {pointer(0)}), "strlen reads through a null pointer"));
    EXPECT_TRUE(isUndefined(call("strlen", {pointer(0x7FFF'0000)}), "not in any object"));
    const Address block = callPointer("strdup", {pointer(string("x"))});
    call("free", {pointer(block)});
    EXPECT_TRUE(isUndefined(call("strlen", {pointer(block)}), "lifetime has ended"));
    const Address function = memory().allocate(Region::Code, 1, 1, "f").value();
    EXPECT_TRUE(isUndefined(call("strlen", {pointer(function)}), "the code of a function"));
}

TEST_P(StringTest, StrlenOfALongString) {
    const std::string text(1000, 'x');
    EXPECT_EQ(callInteger("strlen", {pointer(string(text))}), 1000);
}

TEST_P(StringTest, UninitializedBytesReadAsStored) {
    // The library reads uninitialized bytes as they are stored (zero), without asking the oracle.
    EXPECT_EQ(callInteger("strlen", {pointer(buffer(4))}), 0);
}

TEST_P(StringTest, MemchrStopsAtTheFirstMatch) {
    const Address block = bytesObject("abcb");
    EXPECT_EQ(callPointer("memchr", {pointer(block), intValue('b'), size(100)}), block + 1);
    EXPECT_EQ(callPointer("memchr", {pointer(block), intValue('b' + 256), size(4)}), block + 1);
    EXPECT_EQ(callPointer("memchr", {pointer(block), intValue('x'), size(4)}), 0u);
    EXPECT_TRUE(isUndefined(call("memchr", {pointer(block), intValue('x'), size(5)}), "past the end"));
}

TEST_P(StringTest, StrchrAndStrrchrFindTheTerminatorToo) {
    const Address text = string("abcabc");
    EXPECT_EQ(callPointer("strchr", {pointer(text), intValue('b')}), text + 1);
    EXPECT_EQ(callPointer("strrchr", {pointer(text), intValue('b')}), text + 4);
    EXPECT_EQ(callPointer("strchr", {pointer(text), intValue('x')}), 0u);
    EXPECT_EQ(callPointer("strrchr", {pointer(text), intValue('x')}), 0u);
    EXPECT_EQ(callPointer("strchr", {pointer(text), intValue(0)}), text + 6);
    EXPECT_EQ(callPointer("strrchr", {pointer(text), intValue(0)}), text + 6);
}

TEST_P(StringTest, StrchrReadsOnlyUpToTheMatch) {
    const Address unterminated = bytesObject("ab");
    EXPECT_EQ(callPointer("strchr", {pointer(unterminated), intValue('b')}), unterminated + 1);
    EXPECT_TRUE(isUndefined(call("strrchr", {pointer(unterminated), intValue('b')}), "past the end"));
}

TEST_P(StringTest, Strstr) {
    const Address text = string("hello world");
    EXPECT_EQ(callPointer("strstr", {pointer(text), pointer(string("o w"))}), text + 4);
    EXPECT_EQ(callPointer("strstr", {pointer(text), pointer(string(""))}), text);
    EXPECT_EQ(callPointer("strstr", {pointer(text), pointer(string("xyz"))}), 0u);
}

TEST_P(StringTest, SpanFunctions) {
    const Address text = string("abc123");
    EXPECT_EQ(callInteger("strspn", {pointer(text), pointer(string("cba"))}), 3);
    EXPECT_EQ(callInteger("strspn", {pointer(text), pointer(string(""))}), 0);
    EXPECT_EQ(callInteger("strcspn", {pointer(text), pointer(string("321"))}), 3);
    EXPECT_EQ(callInteger("strcspn", {pointer(text), pointer(string("xyz"))}), 6);
    EXPECT_EQ(callPointer("strpbrk", {pointer(text), pointer(string("31"))}), text + 3);
    EXPECT_EQ(callPointer("strpbrk", {pointer(text), pointer(string("x"))}), 0u);
}

} // namespace
} // namespace nitwit::interp::test
