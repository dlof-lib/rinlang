package android.content
class Assets { fun open(n:String): java.io.InputStream = java.io.File(System.getenv("INDSIN_CLIENT") ?: "tools/indsin-live/client/index.html").inputStream() }
class SharedPreferences { private val m = HashMap<String,Boolean>()
  fun getBoolean(k:String,d:Boolean)=m[k]?:d
  fun edit() = Editor(m)
  class Editor(val m:HashMap<String,Boolean>) { fun putBoolean(k:String,v:Boolean):Editor{m[k]=v;return this}; fun apply(){} } }
open class Context {
  val cacheDir = java.io.File("/tmp/indsin_test_cache").apply{mkdirs()}
  val assets = Assets()
  open val applicationContext: Context get() = this
  fun getString(i:Int, vararg a:Any):String = "str$i"
  fun startActivity(i: Intent) {}
  fun startForegroundService(i: Intent): ComponentName? { println("FGS_START " + i.act); return null }
  fun <T:Any> getSystemService(c: Class<T>): T? = null
  fun getSharedPreferences(n:String, m:Int) = SharedPreferences()
  fun checkSelfPermission(p:String) = 0
  fun requestPermissions(p:Array<String>, c:Int) {}
  companion object { const val MODE_PRIVATE = 0 } }
class ComponentName
class Intent(val a:String?, val u:android.net.Uri?) {
  var act:String? = a
  constructor(ctx: Context, c: Class<*>) : this(null, null)
  fun addFlags(f:Int):Intent=this
  fun setAction(a:String):Intent { act=a; return this }
  val action get() = act
  companion object { const val ACTION_VIEW="v"; const val FLAG_ACTIVITY_NEW_TASK=1 } }
