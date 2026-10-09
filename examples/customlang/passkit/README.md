# Passkit — a `<>` tag language for email, passwords, links, API keys, encryption and databases

![passkit](../../../assets/branding/passkit_icon.png)

`.passkit` files are written with simple tags. Every tag does one job and stores its result in a variable (`name="x"`).

```xml
<passkit>
  <email    name="e"  address="Rima@Example.com" />
  <password name="pw" generate length="18" />
  <password name="h"  hash="$pw.password" />
  <apikey   name="k"  generate prefix="pk_live" />
  <sql table="users" insert id="u1" email="$e.address" password_hash="$h.hash" key_hash="$k.hash" />
  <api name="r" url="https://api.example.com/v1/me" key="$k.key" dry />
  <print value="{e.masked} — {r.request.method} {r.request.url}" />
</passkit>
```

- **Reading a variable:** `$x` or `$x.field.sub` (keeps its original type), and `{x.field}` inside text.
- **Comments:** `<!-- ... -->`. **Flag without a value:** `generate`, `dry`. **Unquoted value:** `length=16`.

## Tags

| Tag | Operations (an attribute selects the operation) |
|---|---|
| `<set name value>` `<print value>` | variable / print |
| `<if var [eq\|neq\|gt\|gte\|lt\|lte\|has\|empty]>` … `<else/>` … `</if>` | conditions |
| `<for each="u" in="$rows">` | loop over an array |
| `<assert var … message>` | assertion (stops with a clear error) |
| `<email>` | `address` (validate + normalize + mask) · `mailto` + `subject` + `body` (encoded mailto link) |
| `<password>` | `generate length` · `passphrase words` · `pin length` · `check policy=basic\|standard\|strict\|pin` · `strength` · `hash` · `verify against` · `mask` |
| `<apikey>` | `generate prefix bytes` · `verify against` · `check prefix` · `mask` |
| `<link>` | `url` (parse: scheme/host/port/path/query/fragment) · `build base path query.k=v` · `https` flag |
| `<api>` | `url method key auth=bearer\|header\|query header.X=… body dry insecure` |
| `<sql>` | `table` + `create` · `insert id fields…` · `update id fields…` · `select where order limit` · `find field equals` · `count` · `exists` · `delete where` |
| `<input name default required>` | input contract: takes the passed value, or `default`, or fails when `required` |
| `<return value>` | ends the file and returns a value to the caller (`result.value`) |
| `<import file>` | include another `.passkit` file **in the same scope** |
| `<run file name in.k=… [quiet] [strict]>` | call another file **in an isolated scope** with inputs `in.k`; result `{ok,value,vars,output,message}` |
| `<call fn arg0… \| args="$list" name [optional]>` | call a Rin function that Rin registered explicitly |
| `<crypt op=…>` | `keygen` `seal` `open` `sealpw` `openpw` `envelope` `unenvelope` `hmac` `hkdf` `pbkdf2` `random` `uuid` `b64` `b64d` `b32` `b32d` `fingerprint` `merkle` `pepper` `derive` `shamir` `combine` |
| `<token op=…>` | `sign` (claim.* ttl now iss aud) · `verify` · `signurl` · `verifyurl` |
| `<otp op=…>` | `secret` · `code` · `verify` · `uri` · `recovery` · `recoverycheck` |
| `<db table op=…>` | CRUD, transactions, encrypted fields, blind index, users, sessions, API keys, audit log, rate limit, reset tokens, nonces, 2FA (see below) |
| `<container of op=…>` | `ensure` `get` `set` `has` `delete` `fields` `load` `save` `tojson` `fromjson` `clone` `checksum` `seal` `open` `sealall` `openall` `exec` `bind` `sync` `names` `kind` `exists` |

## Encryption (`<crypt>`, `<token>`, `<otp>`)

```xml
<crypt name="k" op="keygen" />
<crypt name="s" op="seal" key="$k.key" value="secret text" aad="invoice-17" />
<crypt name="o" op="open" key="$k.key" value="$s.sealed" aad="invoice-17" />   <!-- fails if key, text or aad changed -->

<token name="t" op="sign"   key="$k.key" claim.sub="u1" claim.role="admin" now="1000" ttl="3600" iss="me" />
<token name="v" op="verify" key="$k.key" value="$t.token" now="1500" iss="me" />

<otp name="s" op="secret" />
<otp name="c" op="code"   secret="$s.secret" now="6000" />
<otp name="v" op="verify" secret="$s.secret" code="$c.code" now="6010" />
```

Rin has no wall clock, so time-based tags take an explicit `now` (seconds, for example an epoch from your app).

## Databases (`<db>`)

Operations are selected with `op`. Fields for `insert`/`update` use the `set.` prefix.

```xml
<db table="notes" op="insert" id="n1" set.title="Hello" set.body="World" />
<db table="notes" op="find" field="title" equals="Hello" name="hits" />

<!-- encrypted fields + a blind index so you can search without storing the plaintext -->
<db table="people" op="insert" key="$k.key" enc="email" blind="email" set.email="a@b.com" set.age="30" />
<db table="people" op="find"   key="$k.key" blind field="email" equals="A@B.com" name="found" />

<!-- security models -->
<db table="users"    op="user.create"    key="$k.key" email="a@b.com" password="Zt7!mQ4pLw" />
<db table="users"    op="user.login"     key="$k.key" email="a@b.com" password="Zt7!mQ4pLw" name="login" />
<db table="sessions" op="session.create" user="u1" ttl="3600" now="1000" name="s" />
<db table="keys"     op="apikey.create"  owner="svc" scopes="read,write" prefix="sk" now="10" name="k2" />
<db table="audit"    op="audit"          event="login" actor="u1" now="20" />
```

`<db op="insert">` refuses a field named password/secret/token/pin unless its value is a hash (`pk1$…`) or you encrypt it with `enc=` and `key=`.

## Linking containers to `.passkit` (`<container>`)

```xml
<container of="settings" op="ensure" />
<container of="settings" op="set" field="theme" value="dark" />
<container of="settings" op="load" prefix="cfg_" />                     <!-- fields become variables cfg_theme ... -->
<container of="settings" op="save" vars="result" />                     <!-- variables become fields -->
<container of="settings" op="seal" field="token" key="$k.key" />       <!-- encrypt one field in place -->
<container of="scripts"  op="exec" field="onStart" in.user="rima" />    <!-- run a .passkit stored inside a container field -->
<container of="settings" op="bind" field="theme" var="theme" />         <!-- bind a variable to a field -->
<container of="x" op="sync" dir="push" />                               <!-- push (or pull) every bound variable -->
```

From Rin (`@import "lib/passkitlang.og.rin";`): `passkitFromContainer(name)` (fields as inputs), `passkitToContainer(result, name, prefix)`, `passkitRunContainer(name, field, inputs)`, `passkitRunLinked(name, field, outPrefix)`.

## File linking (Rin ⇄ passkit ⇄ passkit)

```
Rin ──passkitRun / passkitRunSource──▶ .passkit        (inputs in, <return> out)
.passkit ──<call fn=…>──▶ a Rin function               (functions registered with passkitRegister)
.passkit ──<import> / <run>──▶ another .passkit        (same scope / isolated call)
```

```rin
@import "lib/passkitlang.og.rin";
fun double(x) { return x * 2; }
passkitRegister("double", double);

let r = passkitRun("signup.passkit", { email: "a@b.com" });
print r["ok"];                      // true/false
print r["value"];                   // what <return> returned
print passkitGet(r, "pw.strength"); // any variable inside the file by dotted path
```

- Paths are **relative to the current file's folder** and reject `..`, absolute paths and `:`.
- Link cycles are detected and the call depth is limited to 8.
- `<run>` does not stop the parent when the child fails (`ok:false` + `message`) unless `strict`.
- A `.passkit` file can only call Rin functions that Rin registered (the exposed surface is chosen by Rin, not by the file).

## Built-in safety

- `<sql insert>` / `<db insert>` refuse raw passwords and secrets.
- `<api>` refuses `http://` unless `insecure`, and shows the key masked in `request`.
- `<sql delete>` without `where` is refused. RCSQL errors come back as `{ok:false,error}` values instead of crashing.
- A raw API key is visible once in `$k.key` at generation time; store only `$k.hash`.

## Running

```
rin run.rin        # runs examples/hello.passkit (edit SOURCE_PATH)
rin test.rin       # language + file-linking tests
```

`CodeGen.rin`: `cgFormat(ast)` prints canonical source and `generate(ast)` produces a standalone Rin script.
The libraries underneath are `lib/passkit.og.rin`, `lib/passkitcrypt.og.rin` and `lib/passkitdb.og.rin` (see `docs/passkit.md`).

## Honest limits

- No SMTP sending; use `<api>` against a mail provider, or `mailto`.
- A real `<api>` call (without `dry`) needs network access; only `dry` mode is covered by the offline tests.
- Email validation is ASCII only (no internationalized addresses).
- RCSQL `where` text does not accept symbols such as `@` or `.` in values; use `find field equals` for those.
- `<call>`: an arity mistake inside a Rin function aborts the run unless that function is wrapped with `#ignorance`.
