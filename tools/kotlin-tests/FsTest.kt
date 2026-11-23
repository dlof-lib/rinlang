import com.dlof.rinlang.*
import java.io.File
fun main() {
    val b = File("/tmp/fsb"); b.deleteRecursively(); File(b, "src/deep").mkdirs()
    File(b, "main.rin").writeText("print(1)\nprint(2)\n"); File(b, "src/u.rin").writeText("x"); File(b, ".git").mkdirs(); File(b, "bin.dat").writeBytes(byteArrayOf(1,0,2))
    File("/tmp/secret.txt").writeText("s")
    fun p(o: RinFs.Out) { println("err=${o.error} extra=${o.extra}"); o.lines.forEach { println(RinAnsi.strip(it)) } }
    p(RinFs.ls(b, "")); p(RinFs.tree(b, "")); p(RinFs.cat(b, "main.rin"))
    println(RinFs.ls(b, "..").error); check(RinFs.ls(b, "..").error == RinFs.OUTSIDE); check(RinFs.cat(b, "../secret.txt").error == RinFs.OUTSIDE)
    check(RinFs.cat(b, "/tmp/secret.txt").error == RinFs.OUTSIDE)
    check(RinFs.cat(b, "bin.dat").error == RinFs.BINARY); check(RinFs.cat(b, "src").error == RinFs.NOT_FILE)
    check(RinFs.ls(b, "nope").error == RinFs.NOT_FOUND); check(RinFs.tree(b, "main.rin").error == RinFs.NOT_DIR)
    check(RinFs.ls(b, "", true).lines.size == 4)
    // ---- head / tail / wc / find / parseCountArg / log ----
    File(b, "long.txt").writeText((1..25).joinToString("\n") { "line $it word" } + "\n")
    check(RinFs.parseCountArg("a.txt 5") == ("a.txt" to 5) && RinFs.parseCountArg("-n 3 a.txt") == ("a.txt" to 3))
    check(RinFs.parseCountArg("a.txt") == ("a.txt" to 10) && RinFs.parseCountArg("a.txt 0").second == 1)
    val h = RinFs.head(b, "long.txt", 3); check(h.lines.size == 3 && h.extra == 22L && RinAnsi.strip(h.lines[0]).endsWith("line 1 word"))
    val t = RinFs.tail(b, "long.txt", 2); check(t.lines.size == 2 && RinAnsi.strip(t.lines[1]).startsWith("25 │"))
    check(RinAnsi.strip(RinFs.wc(b, "long.txt").lines[0]).startsWith("25 lines · 75 words"))
    check(RinFs.head(b, "nope").error == RinFs.NOT_FOUND && RinFs.tail(b, "../secret.txt").error == RinFs.OUTSIDE && RinFs.wc(b, "bin.dat").error == RinFs.BINARY)
    val f = RinFs.find(b, "U.RIN").lines.map { RinAnsi.strip(it) }; check(f == listOf("src/u.rin"))
    check(RinFs.find(b, "git").lines.isEmpty())              // المخفي لا يُبحث فيه
    check(RinFs.find(b, "src").lines.map { RinAnsi.strip(it) } == listOf("src/"))
    check(RinFs.find(b, "").error != null)
    check(RinFs.logFileName(0L).matches(Regex("terminal-\\d{8}-\\d{6}\\.txt")))
    val saved = RinFs.saveLog(b, "hello", 1_700_000_000_000L); check(saved != null && File(b, saved!!).readText() == "hello")
    println("OK")
}
