# passkit — passwords, encryption, databases and a tag language

![passkit icon](../assets/branding/passkit_icon.png)

Official icon: `assets/branding/passkit_icon.svg` / `.png` (512×512). The 24dp file-list icon is `app/src/main/res/drawable/ic_passkit_file.xml`.

A family of pure-Rin libraries (no engine changes) built on natives that already exist
(`sec.hmacSha256`, `sec.sha256`, `sec.randomToken`, `sec.constantTimeEqual`, `sec.xorCipher`, doc containers + RCSQL).
All are embedded, so imports work even without a `lib/` folder.

| Library | Prefix | What it is |
|---|---|---|
| `lib/passkit.og.rin` | `pk` | password policy, strength, generation, salted hashing, lockout, lifecycle, redaction |
| `lib/passkitcrypt.og.rin` | `pc` | authenticated encryption, KDFs, keyrings, envelopes, tokens, signed URLs/requests, TOTP, Shamir |
| `lib/passkitdb.og.rin` | `pd` | database layer over RCSQL doc containers, encrypted fields, security models, container linking |
| `lib/passkitlang.og.rin` | `passkit*` | the `<passkit>` tag language as a library (generated from `examples/customlang/passkit/`) |

```rin
@import "lib/passkit.og.rin";        // pk*
@import "lib/passkitcrypt.og.rin";   // pc*
@import "lib/passkitdb.og.rin";      // pd*
@import "lib/passkitlang.og.rin";    // passkitRun, passkitRegister, ...
```

Every function returns `{ok:true,...}` or `{ok:false,error}` and never crashes on bad input.
Rin has no wall clock, so time-based functions take an explicit `nowSec`.

## What is standard

The engine now ships real primitives in C++ (`app/src/main/cpp/rin_crypto_aead.h`, no external dependency), checked against
official vectors by `tools/test_crypto_aead.cpp` and by `tests/verification/passkit_features.rin`:

| Primitive | Native | Vectors |
|---|---|---|
| AES-128/192/256 + **GCM** | `sec.aesGcmSeal` / `sec.aesGcmOpen` | FIPS 197, NIST SP 800-38D |
| **PBKDF2-HMAC-SHA256** | `sec.pbkdf2Sha256` | RFC 7914 |
| **OS CSPRNG** (`/dev/urandom`) | `sec.randomToken` | — (replaced the old seeded Mersenne Twister) |
| HMAC / HKDF / TOTP / Base32 | Rin on top of `sec.hmacSha256` | RFC 4231 / 5869 / 6238 / 4648 |

- `pcSeal` / `pcOpen` are **AES-256-GCM** (format `pc2`). Old `pc1` data (HMAC-CTR) still opens, so nothing already stored breaks.
- `pkHash` is **PBKDF2-HMAC-SHA256** with 600,000 iterations (format `pk2`). Old `pk1` hashes still verify and `pkNeedsRehash` flags them.

Honest limits: AES uses lookup tables (not hardened against cache-timing attacks by a co-located attacker); keep one key
under about 2^32 messages (rotate with the keyring); PBKDF2 is NIST-approved but not memory-hard, so for a high-risk server
prefer bcrypt/scrypt/argon2; this code has not had an independent security audit.

## The 185-feature catalog

[`docs/passkit-crypto-db.md`](passkit-crypto-db.md) lists every feature (F001 to F185) with the function or tag it
belongs to. It is generated from `tests/verification/passkit_features.rin`, where each line is a real check run on the
real interpreter, so the count and the descriptions cannot drift from reality. Regenerate it with
`python3 tools/gen_features_doc.py`.

## The tag language

`examples/customlang/passkit/` holds a complete language (Lexer, Parser, Interpreter, CodeGen, syntax file, example, tests).
Tags: `<email>` `<password>` `<apikey>` `<link>` `<api>` `<sql>` `<crypt>` `<token>` `<otp>` `<db>` `<container>` plus
`<set>` `<print>` `<if>` `<for>` `<assert>` `<input>` `<return>` `<import>` `<run>` `<call>`. See that folder's README.

From Rin: `passkitRun`, `passkitRunSource`, `passkitGet`, `passkitRegister`, `passkitHandlers`, `passkitUnregister`,
`passkitClearHandlers`, `passkitHandlerNames`, `passkitFromContainer`, `passkitToContainer`, `passkitRunContainer`,
`passkitRunLinked`.

## Notes learned along the way

- `len` and `charAt` in Rin work on **bytes**, so the library counts real characters with `pkChars`/`pkLen` (via `slice`).
- `toNumber` throws on non-numeric text; validate digits first.
- Words that cannot be used as identifiers or map keys: `file`, `text`, `fn`, `return`, `row`.
- Nested `@import "./x.rin"` resolves relative to the main script's folder.
- Raw string delimiters in `rin_stdlib_libs.h` must be 16 characters or fewer.
- After editing any `lib/passkit*.og.rin` or the language files, run
  `python3 tools/gen_passkitlang.py && python3 tools/embed_libs.py`.

## Tests

`tests/verification/passkit.rin` (policy/strength/hashing), `tests/verification/passkit_features.rin` (185 features),
`tests/verification/passkitlang.rin` (the library form of the language) and `examples/customlang/passkit/test.rin`
(language + file linking).
