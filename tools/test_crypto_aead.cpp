// Standalone test for app/src/main/cpp/rin_crypto_aead.h against NIST/RFC vectors.
// Build:  g++ -std=c++17 -O1 -Iapp/src/main/cpp tools/test_crypto_aead.cpp app/src/main/cpp/clc/sha256.cpp -o /tmp/test_aead && /tmp/test_aead
#include "rin_crypto_aead.h"
#include <iostream>
using namespace rincrypto;
static std::string unhex(const std::string& h) { std::string o; for (size_t i = 0; i + 1 < h.size(); i += 2) o += (char)std::stoi(h.substr(i, 2), nullptr, 16); return o; }
static std::string hex(const std::string& s) { static const char* d = "0123456789abcdef"; std::string o; for (unsigned char c : s) { o += d[c >> 4]; o += d[c & 15]; } return o; }
static int fails = 0;
static void check(const char* n, bool ok) { std::cout << (ok ? "PASS  " : "FAIL  ") << n << "\n"; if (!ok) ++fails; }
int main() {
    std::string out;
    // FIPS 197 C.1 / C.3
    { AesKey k; aesSetKey(k, (const uint8_t*)unhex("000102030405060708090a0b0c0d0e0f").data(), 16);
      uint8_t o[16]; std::string pt = unhex("00112233445566778899aabbccddeeff"); aesEncryptBlock(k, (const uint8_t*)pt.data(), o);
      check("AES-128 FIPS197", hex(std::string((char*)o, 16)) == "69c4e0d86a7b0430d8cdb78070b4c55a"); }
    { AesKey k; std::string key = unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"); aesSetKey(k, (const uint8_t*)key.data(), 32);
      uint8_t o[16]; std::string pt = unhex("00112233445566778899aabbccddeeff"); aesEncryptBlock(k, (const uint8_t*)pt.data(), o);
      check("AES-256 FIPS197", hex(std::string((char*)o, 16)) == "8ea2b7ca516745bfeafc49904b496089"); }
    // NIST GCM test case 1/2 (AES-128), 3/4 (with data/AAD), 13/14/15/16 (AES-256)
    check("GCM TC1 (empty)", gcmSeal(unhex("00000000000000000000000000000000"), unhex("000000000000000000000000"), "", "", out) && hex(out) == "58e2fccefa7e3061367f1d57a4e7455a");
    check("GCM TC2", gcmSeal(unhex("00000000000000000000000000000000"), unhex("000000000000000000000000"), unhex("00000000000000000000000000000000"), "", out)
          && hex(out) == "0388dace60b6a392f328c2b971b2fe78ab6e47d42cec13bdf53a67b21257bddf");
    std::string K3 = unhex("feffe9928665731c6d6a8f9467308308"), N3 = unhex("cafebabefacedbaddecaf888");
    std::string P4 = unhex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    std::string A4 = unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    check("GCM TC4 (AAD, partial block)", gcmSeal(K3, N3, P4, A4, out)
          && hex(out) == "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e0915bc94fbc3221a5db94fae95ae7121a47");
    std::string dec; check("GCM open round trip", gcmOpen(K3, N3, out, A4, dec) && dec == P4);
    std::string bad = out; bad[3] ^= 1; check("GCM tampered ciphertext rejected", !gcmOpen(K3, N3, bad, A4, dec));
    check("GCM wrong AAD rejected", !gcmOpen(K3, N3, out, "x", dec));
    std::string K15 = unhex("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    std::string P15 = unhex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255");
    check("GCM TC15 (AES-256)", gcmSeal(K15, unhex("cafebabefacedbaddecaf888"), P15, "", out)
          && hex(out) == "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad" "b094dac5d93471bdec1a502270e3cc6c");
    // PBKDF2-HMAC-SHA256 (RFC 7914 section 11)
    check("PBKDF2 c=1", hex(pbkdf2Sha256("passwd", "salt", 1, 64)) == "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783");
    check("PBKDF2 c=80000", hex(pbkdf2Sha256("Password", "NaCl", 80000, 64)) == "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d");
    uint8_t r1[16], r2[16]; osRandomBytes(r1, 16); osRandomBytes(r2, 16);
    check("OS random differs", std::memcmp(r1, r2, 16) != 0);
    std::cout << (fails ? "FAILURES\n" : "ALL PASSED\n");
    return fails ? 1 : 0;
}
