import com.dlof.rinlang.IndsinLiveServer
fun main() {
  val ctx = android.content.Context()
  IndsinLiveServer.openInBrowser(ctx, java.io.File(System.getenv("INDSIN_PROJ") ?: "/tmp/indsin_proj"), "main.indsin", "Text(\"hi\")")
  println("URL " + IndsinLiveServer.urlFor("main.indsin"))
  System.out.flush(); Thread.sleep(60000)
}
