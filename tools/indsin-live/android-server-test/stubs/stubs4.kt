package android.util
object Log { fun i(t:String,m:String,x:Throwable):Int=0; fun w(t:String,m:String):Int{System.err.println("W/$t: $m");return 0}; fun d(t:String,m:String):Int=0; fun e(t:String,m:String):Int{System.err.println("E/$t: $m");return 0}; fun i(t:String,m:String):Int=0
 fun w(t:String,m:String,e:Throwable):Int{System.err.println("W/$t: $m $e");return 0}; fun e(t:String,m:String,x:Throwable):Int=0 }
