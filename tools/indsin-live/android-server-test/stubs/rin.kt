package com.dlof.rinlang
object R { object string { const val indsin_browser_failed=1; const val indsin_browser_opening=2; const val indsin_browser_no_app=3; const val indsin_browser_nothing=4; const val indsin_live_notif_channel=5; const val indsin_live_notif_title=6; const val indsin_live_notif_text=7; const val indsin_live_notif_stop=8; const val indsin_live_notif_open=9 }
  object drawable { const val ic_indsin_live_notif=1 } }
object IndsinPreviewManager {
  private val worker = java.util.concurrent.Executors.newSingleThreadExecutor{ r->Thread(r,"worker").apply{isDaemon=true} }
  fun postToWorker(block: () -> Unit) { worker.execute { try { block() } catch (_: Throwable) {} } }
  fun <T> runOnWorker(timeoutMs: Long = 30_000L, block: () -> T): T = worker.submit(java.util.concurrent.Callable { block() }).get(timeoutMs, java.util.concurrent.TimeUnit.MILLISECONDS)
}
object RinEngine {
  class IndsinSession(var source: String, var w:Int=390, containerName:String?=null) {
    var closed=false; val isClosed get()=closed
    private fun env(extra:String="") = """{"ok":true,"handled":true,"fabric":{"w":$w,"src":${org.json.JSONObject.quote(source)}}$extra}"""
    fun currentJson()=env(); fun tap(x:Double,y:Double)=env(); fun setInput(n:String,t:String)=env()
    fun longPress(x:Double,y:Double)=env(); fun doubleTap(x:Double,y:Double)=env(); fun hover(x:Double,y:Double,e:Boolean)=env()
    fun mediaPicked(a:String,b:String,c:String,d:Boolean,e:Double,f:Boolean)=env(); fun mediaProgress(a:String,b:String,c:Double,d:String)=env()
    fun tick()=env(); fun updateSource(s:String):String{source=s;return env()}
    fun setViewport(h:Int):String=env(); fun resize(w:Int,h:Int){this.w=w}
    fun exportPng(p:String):Boolean{ java.io.File(p).writeBytes(byteArrayOf(-119,80,78,71)); return true}
    fun statsJson()="{}"; fun auditJson()="{}"; fun outline()="{}"
    fun setDefaultBackground(b:String):Boolean=true; fun close(){closed=true}
  }
}

object IndsinViewTracer { fun containsView(s:String)=false }
