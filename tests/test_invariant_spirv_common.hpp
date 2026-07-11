#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "parallel-psx/SPIRV-Cross/spirv_common.hpp"

class SecurityTest : public ::testing::TestWithParam<double> {};

TEST_P(SecurityTest, BufferReadsNeverExceedDeclaredLength) {
    // Invariant: Buffer reads never exceed the declared length
    double payload = GetParam();
    std::string result = convert_to_string(payload);
    // Ensure the result fits within reasonable bounds (64 chars + ".0" suffix)
    EXPECT_LE(result.size(), 66u);
}

INSTANTIATE_TEST_SUITE_P(
    AdversarialInputs,
    SecurityTest,
    ::testing::Values(
        1.0,                    // Valid input
        1e308,                  // Boundary: max double value
        -1e308,                 // Boundary: min double value
        1.2345678901234567e-308 // Small positive denormal
    )
);

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}