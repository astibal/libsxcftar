#include <gtest/gtest.h>
#include <detail/crypto.hpp>

TEST(CftarCrypto, KnownSha256Vectors) {
    EXPECT_EQ(sx::cftar::crypto::sha256(""),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sx::cftar::crypto::sha256("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sx::cftar::crypto::sha256(std::string_view("a\0b", 3)),
        "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138");
}
