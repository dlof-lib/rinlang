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

## What is standard and what is a construction

Verified against official test vectors (see `tests/verification/passkit_features.rin`):
HMAC-SHA256 (RFC 4231), HKDF (RFC 5869), PBKDF2-HMAC-SHA256, TOTP-SHA256 (RFC 6238), Base32 (RFC 4648).

Constructions (sound by design, but not a formal standard): `pcSeal`/`pcOpen` is a stream cipher built from
HMAC-CTR plus an HMAC tag (encrypt-then-MAC) with per-message subkeys from HKDF. It is **not** AES-GCM or ChaCha20;
do not rely on it where a specific standard is mandatory. `pkHash` is a salted, stretched HMAC chain: far better
than bare sha256 but not a replacement for bcrypt/scrypt/argon2 on a high-risk production server.

## The 167-feature catalog

[`docs/passkit-crypto-db.md`](passkit-crypto-db.md) lists every feature (F001 to F167) with the function or tag it
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

`tests/verification/passkit.rin` (policy/strength/hashing), `tests/verification/passkit_features.rin` (167 features),
`tests/verification/passkitlang.rin` (the library form of the language) and `examples/customlang/passkit/test.rin`
(language + file linking).
