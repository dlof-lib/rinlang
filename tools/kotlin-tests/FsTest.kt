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
    println("OK")
}
