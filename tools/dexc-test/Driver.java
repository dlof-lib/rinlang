import java.io.*;
import java.nio.file.*;
import java.util.*;
import com.dlof.rinlang.apk.dex.RinDexCompiler;

/** Driver: java Driver file.rin  → يطبع مخرجات البرنامج المُصرَّف إلى DEX والمُنفَّذ عبر DexVm (أو UNSUPPORTED/ERROR). */
public class Driver {
    public static void main(String[] a) throws Throwable {
        Throwable[] err = new Throwable[1];
        Thread t = new Thread(null, () -> { try { run(a); } catch (Throwable e) { err[0] = e; } }, "rin", 1L << 29);
        t.start(); t.join();
        if (err[0] != null) throw err[0];
    }
    static void run(String[] a) throws Throwable {
        java.io.PrintStream ps = new java.io.PrintStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), true, "UTF-8");
        String src = new String(Files.readAllBytes(Paths.get(a[0])), "UTF-8");
        RinDexCompiler.Result res = RinDexCompiler.INSTANCE.compile(src, "x.rin");
        if (res instanceof RinDexCompiler.Result.Unsupported) { ps.println("UNSUPPORTED: " + ((RinDexCompiler.Result.Unsupported) res).getReason()); return; }
        byte[] dex = ((RinDexCompiler.Result.Ok) res).getDex();
        if (a.length > 1) Files.write(Paths.get(a[1]), dex);
        StringBuilder sb = new StringBuilder();
        com.dlof.rinlang.dexrt.RinRt.out = s -> { sb.append(s); return kotlin.Unit.INSTANCE; };
        try { DexVm.runDex(dex, "run"); }
        catch (com.dlof.rinlang.dexrt.RinRuntimeError e) { sb.append("error[" + e.getCode() + "]: " + e.getMessage() + "\n"); }
        ps.print(sb);
    }
}
