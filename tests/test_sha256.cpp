#include "camera_iq/sha256.hpp"

#include <string>

#include "harness.hpp"

using camera_iq::sha256_hex;
using test::check;

void TESTS() {
  check(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb924"
                          "27ae41e4649b934ca495991b7852b855",
        "sha256: empty input matches the published test vector");
  check(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223"
                             "b00361a396177a9cb410ff61f20015ad",
        "sha256: abc matches the published test vector");

  std::string binary("a\0b", 3);
  check(sha256_hex(binary) == "59b271ae1bbcb1d31d41929817f4b16f"
                              "b439eb4f31520b5ad1d5ce98920a7138",
        "sha256: embedded NUL bytes are part of the digest");
}
