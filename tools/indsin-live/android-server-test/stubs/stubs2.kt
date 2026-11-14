package android.net
class Uri(val s:String){ override fun toString()=s; companion object { fun parse(s:String)=Uri(s); fun decode(s:String):String=java.net.URLDecoder.decode(s,"UTF-8"); fun encode(s:String):String=java.net.URLEncoder.encode(s,"UTF-8") } }
