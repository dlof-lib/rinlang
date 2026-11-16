import java.lang.reflect.*;
import java.nio.*;
import java.security.MessageDigest;
import java.util.*;
import java.util.zip.Adler32;

/**
 * مُتحقِّق ومُشغِّل DEX مستقل (للاختبار فقط): يقرأ الملف من البايتات الخام وفق مواصفة dex-format
 * بلا أي مشاركة كود مع DexBuilder، ثم (1) يفحص البنية: magic/checksum/SHA-1/ترتيب الجداول/map_list/حدود
 * الإزاحات، (2) يتحقق من أنواع السجلات بتحليل تدفّق بيانات شبيه بمُحقِّق ART (ref/int/wide/zero/conflict)،
 * (3) ينفّذ البايتكود فعلياً مع تمرير نداءات static إلى الصنف الحقيقي RinRt.
 */
public class DexVm {
    final ByteBuffer b; final int fileSize;
    String[] strings; int[] typeIds; int[][] protos; int[][] fields; int[][] methods;
    static class Code { int regs, ins, outs; short[] insns; }
    Map<Integer, Code> codeByMethod = new HashMap<>();
    Map<Integer, Object> statics = new HashMap<>();   // field idx -> value
    String ownerDesc;

    DexVm(byte[] data) {
        b = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN); fileSize = data.length; parse(data);
    }
    static void fail(String m) { throw new IllegalStateException("DEXFAIL: " + m); }
    int u4(int off) { return b.getInt(off); }
    int u2(int off) { return b.getShort(off) & 0xffff; }

    int uleb(int[] pos) { int r = 0, sh = 0; while (true) { int x = b.get(pos[0]++) & 0xff; r |= (x & 0x7f) << sh; if ((x & 0x80) == 0) return r; sh += 7; } }

    static String mutf8(ByteBuffer bb, int off, int utf16len) {
        StringBuilder sb = new StringBuilder(); int i = off;
        while (true) {
            int c = bb.get(i++) & 0xff;
            if (c == 0) break;
            if (c < 0x80) sb.append((char) c);
            else if ((c & 0xe0) == 0xc0) sb.append((char) (((c & 0x1f) << 6) | (bb.get(i++) & 0x3f)));
            else sb.append((char) (((c & 0x0f) << 12) | ((bb.get(i++) & 0x3f) << 6) | (bb.get(i++) & 0x3f)));
        }
        if (sb.length() != utf16len) fail("utf16_size mismatch for string at " + off);
        return sb.toString();
    }

    void parse(byte[] d) {
        if (!(d[0]=='d'&&d[1]=='e'&&d[2]=='x'&&d[3]==10&&d[7]==0)) fail("magic");
        if (d[4]!='0'||d[5]!='3'||d[6]!='5') fail("version");
        Adler32 ad = new Adler32(); ad.update(d, 12, d.length - 12);
        if ((int) ad.getValue() != u4(8)) fail("checksum");
        try { MessageDigest sha = MessageDigest.getInstance("SHA-1"); sha.update(d, 32, d.length - 32);
            byte[] dg = sha.digest(); for (int i = 0; i < 20; i++) if (dg[i] != d[12 + i]) fail("sha1"); } catch (Exception e) { throw new RuntimeException(e); }
        if (u4(32) != d.length) fail("file_size");
        if (u4(36) != 0x70) fail("header_size");
        if (u4(40) != 0x12345678) fail("endian");
        int mapOff = u4(52);
        int sN = u4(56), sO = u4(60), tN = u4(64), tO = u4(68), pN = u4(72), pO = u4(76), fN = u4(80), fO = u4(84),
            mN = u4(88), mO = u4(92), cN = u4(96), cO = u4(100), dSz = u4(104), dO = u4(108);
        if (dO + dSz != d.length) fail("data_size/data_off");
        if (dSz % 4 != 0) fail("data_size not multiple of 4");
        strings = new String[sN];
        for (int i = 0; i < sN; i++) {
            int off = u4(sO + 4 * i); int[] p = {off}; int len = uleb(p);
            strings[i] = mutf8(b, p[0], len);
            if (off < dO) fail("string data before data section");
            if (i > 0 && strings[i - 1].compareTo(strings[i]) >= 0) fail("strings not sorted/unique at " + i);
        }
        typeIds = new int[tN];
        for (int i = 0; i < tN; i++) { typeIds[i] = u4(tO + 4 * i); if (i > 0 && typeIds[i - 1] >= typeIds[i]) fail("type_ids order"); String t = strings[typeIds[i]];
            if (!(t.matches("V|Z|B|S|C|I|J|F|D|L[^;]+;|\\[+(?:[ZBSCIJFD]|L[^;]+;)"))) fail("bad type descriptor " + t); }
        protos = new int[pN][];
        for (int i = 0; i < pN; i++) {
            int shorty = u4(pO + 12 * i), ret = u4(pO + 12 * i + 4), po = u4(pO + 12 * i + 8);
            int[] params = new int[0];
            if (po != 0) { if (po % 4 != 0) fail("type_list align"); int n = u4(po); params = new int[n]; for (int k = 0; k < n; k++) params[k] = u2(po + 4 + 2 * k); }
            protos[i] = new int[params.length + 2]; protos[i][0] = ret; protos[i][1] = shorty;
            System.arraycopy(params, 0, protos[i], 2, params.length);
            StringBuilder sh = new StringBuilder(); sh.append(shortyOf(strings[typeIds[ret]]));
            for (int pt : params) sh.append(shortyOf(strings[typeIds[pt]]));
            if (!sh.toString().equals(strings[shorty])) fail("shorty mismatch " + strings[shorty] + " vs " + sh);
            if (i > 0) { int pr = protos[i-1][0]; if (pr > ret) fail("proto order"); }
        }
        fields = new int[fN][];
        for (int i = 0; i < fN; i++) { fields[i] = new int[]{u2(fO + 8 * i), u2(fO + 8 * i + 2), u4(fO + 8 * i + 4)};
            if (i > 0) { int[] a = fields[i-1], c = fields[i]; int cmp = a[0]!=c[0]?Integer.compare(a[0],c[0]):a[2]!=c[2]?Integer.compare(a[2],c[2]):Integer.compare(a[1],c[1]); if (cmp >= 0) fail("field_ids order"); } }
        methods = new int[mN][];
        for (int i = 0; i < mN; i++) { methods[i] = new int[]{u2(mO + 8 * i), u2(mO + 8 * i + 2), u4(mO + 8 * i + 4)};
            if (i > 0) { int[] a = methods[i-1], c = methods[i]; int cmp = a[0]!=c[0]?Integer.compare(a[0],c[0]):a[2]!=c[2]?Integer.compare(a[2],c[2]):Integer.compare(a[1],c[1]); if (cmp >= 0) fail("method_ids order"); } }
        for (int i = 0; i < cN; i++) {
            int o = cO + 32 * i; int cls = u4(o), acc = u4(o + 4), sup = u4(o + 8), cd = u4(o + 24);
            ownerDesc = strings[typeIds[cls]];
            if (!strings[typeIds[sup]].equals("Ljava/lang/Object;")) fail("super");
            int[] p = {cd}; int sf = uleb(p), inf = uleb(p), dm = uleb(p), vm = uleb(p);
            int prev = 0; for (int k = 0; k < sf; k++) { prev += uleb(p); uleb(p); if (fields[prev][0] != cls) fail("static field owner"); }
            if (inf != 0) fail("instance fields unexpected");
            for (int pass = 0; pass < 2; pass++) { prev = 0; int n = pass == 0 ? dm : vm;
                for (int k = 0; k < n; k++) { prev += uleb(p); int ac = uleb(p); int co = uleb(p);
                    if (methods[prev][0] != cls) fail("method owner");
                    if (co != 0) {
                        if (co % 4 != 0) fail("code_item align");
                        Code c = new Code(); c.regs = u2(co); c.ins = u2(co + 2); c.outs = u2(co + 4);
                        if (u2(co + 6) != 0) fail("tries unsupported"); int sz = u4(co + 12); c.insns = new short[sz];
                        for (int q = 0; q < sz; q++) c.insns[q] = b.getShort(co + 16 + 2 * q);
                        codeByMethod.put(prev, c);
                    } } }
        }
        // map_list
        int n = u4(mapOff); int last = -1;
        for (int i = 0; i < n; i++) { int off = u4(mapOff + 4 + 12 * i + 8); if (off <= last) fail("map_list not sorted"); last = off; }
    }
    static char shortyOf(String d) { char c = d.charAt(0); return (c == 'L' || c == '[') ? 'L' : c; }

    // ------------------------------------------------------------ مُحقِّق الأنواع
    static final int UNDEF = 0, ZERO = 1, INT = 2, REF = 3, WLO = 4, WHI = 5, CONF = 6;
    static int merge(int a, int b) {
        if (a == b) return a;
        if (a == ZERO && b == REF || a == REF && b == ZERO) return REF;
        if (a == ZERO && b == INT || a == INT && b == ZERO) return INT;
        return CONF;
    }
    String mdesc(int mi) { return ownerOf(mi) + "->" + strings[methods[mi][2]] + protoDesc(methods[mi][1]); }
    String ownerOf(int mi) { return strings[typeIds[methods[mi][0]]]; }
    String protoDesc(int pi) { StringBuilder sb = new StringBuilder("("); for (int k = 2; k < protos[pi].length; k++) sb.append(strings[typeIds[protos[pi][k]]]);
        return sb.append(")").append(strings[typeIds[protos[pi][0]]]).toString(); }

    static int len(short[] c, int pc) {
        int op = c[pc] & 0xff;
        switch (op) {
            case 0x0e: case 0x07: case 0x0a: case 0x0c: case 0x11: case 0x12: return 1;
            case 0x08: case 0x13: case 0x1a: case 0x28 - 0x28 + 0x29: case 0x38: case 0x39: case 0x60: case 0x62: case 0x67: case 0x69: return 2;
            case 0x09: case 0x14: case 0x1b: case 0x25: case 0x2a: case 0x71: case 0x77: return 3;
            case 0x18: return 5;
        }
        fail("unknown opcode 0x" + Integer.toHexString(op) + " at " + pc); return 0;
    }

    void verify(int mi) {
        Code c = codeByMethod.get(mi); if (c == null) return;
        String where = mdesc(mi);
        int R = c.regs;
        int[][] state = new int[c.insns.length][];
        int[] init = new int[R];
        int nparams = protos[methods[mi][1]].length - 2;
        if (nparams != c.ins) fail(where + ": ins_size " + c.ins + " != params " + nparams);
        for (int i = 0; i < c.ins; i++) init[R - c.ins + i] = REF;
        // أطوال التعليمات + حدود
        boolean[] start = new boolean[c.insns.length + 1];
        for (int pc = 0; pc < c.insns.length; ) { start[pc] = true; pc += len(c.insns, pc); }
        ArrayDeque<Integer> work = new ArrayDeque<>();
        state[0] = init; work.add(0);
        int[] pendingKind = new int[c.insns.length]; // 0 none, REF, INT after invoke/filled
        while (!work.isEmpty()) {
            int pc = work.poll(); int[] s = state[pc].clone(); int op = c.insns[pc] & 0xff; int u0 = c.insns[pc] & 0xffff;
            int a = (u0 >> 8) & 0xff; int L = len(c.insns, pc);
            if (c.outs < 0) fail("outs");
            List<Integer> next = new ArrayList<>(); boolean falls = true;
            int pend = 0; // نوع نتيجة معلّقة
            switch (op) {
                case 0x07: { int dst = (u0 >> 8) & 0xf, src = (u0 >> 12) & 0xf; chkR(where, pc, R, dst, src); ref(where, pc, s, src); s[dst] = s[src]; break; }
                case 0x08: { int src = c.insns[pc + 1] & 0xffff; chkR(where, pc, R, a, src); ref(where, pc, s, src); s[a] = s[src]; break; }
                case 0x09: { int dst = c.insns[pc + 1] & 0xffff, src = c.insns[pc + 2] & 0xffff; chkR(where, pc, R, dst, src); ref(where, pc, s, src); s[dst] = s[src]; break; }
                case 0x0a: case 0x0c: {
                    chkR(where, pc, R, a, a);
                    int prevPend = pendingIn(c, pc, start, state);
                    if (op == 0x0a && prevPend != INT) fail(where + "@" + pc + ": move-result without int-returning invoke");
                    if (op == 0x0c && prevPend != REF) fail(where + "@" + pc + ": move-result-object without ref-returning invoke");
                    s[a] = prevPend; break; }
                case 0x0e: if (!strings[typeIds[protos[methods[mi][1]][0]]].equals("V")) fail(where + "@" + pc + ": return-void in non-void"); falls = false; break;
                case 0x11: chkR(where, pc, R, a, a); ref(where, pc, s, a); if (!strings[typeIds[protos[methods[mi][1]][0]]].startsWith("L")) fail(where + ": return-object type"); falls = false; break;
                case 0x12: { int dst = (u0 >> 8) & 0xf, v = (u0 >> 12) & 0xf; chkR(where, pc, R, dst, dst); s[dst] = (v == 0) ? ZERO : INT; break; }
                case 0x13: { int v = (short) c.insns[pc + 1]; chkR(where, pc, R, a, a); s[a] = v == 0 ? ZERO : INT; break; }
                case 0x14: { int v = (c.insns[pc + 1] & 0xffff) | (c.insns[pc + 2] << 16); chkR(where, pc, R, a, a); s[a] = v == 0 ? ZERO : INT; break; }
                case 0x18: chkR(where, pc, R, a, a + 1); s[a] = WLO; s[a + 1] = WHI; break;
                case 0x1a: { int si = c.insns[pc + 1] & 0xffff; if (si >= strings.length) fail("string idx"); chkR(where, pc, R, a, a); s[a] = REF; break; }
                case 0x1b: { int si = (c.insns[pc + 1] & 0xffff) | ((c.insns[pc + 2] & 0xffff) << 16); if (si >= strings.length) fail("string idx"); chkR(where, pc, R, a, a); s[a] = REF; break; }
                case 0x60: case 0x62: { int fi = c.insns[pc + 1] & 0xffff; chkR(where, pc, R, a, a); String t = strings[typeIds[fields[fi][1]]];
                    if (op == 0x62 && !t.startsWith("L")) fail(where + "@" + pc + ": sget-object on " + t); if (op == 0x60 && !t.equals("I")) fail(where + "@" + pc + ": sget on " + t);
                    s[a] = op == 0x62 ? REF : INT; break; }
                case 0x67: case 0x69: { int fi = c.insns[pc + 1] & 0xffff; chkR(where, pc, R, a, a); String t = strings[typeIds[fields[fi][1]]];
                    if (op == 0x69) { ref(where, pc, s, a); if (!t.startsWith("L")) fail(where + "@" + pc + ": sput-object on " + t); }
                    else { if (s[a] != INT && s[a] != ZERO) fail(where + "@" + pc + ": sput int reg type " + s[a]); if (!t.equals("I")) fail("sput on " + t); }
                    break; }
                case 0x25: { int cnt = a; int ti = c.insns[pc + 1] & 0xffff; int first = c.insns[pc + 2] & 0xffff;
                    if (!strings[typeIds[ti]].equals("[Ljava/lang/Object;")) fail("filled-new-array type");
                    for (int k = 0; k < cnt; k++) { chkR(where, pc, R, first + k, first + k); ref(where, pc, s, first + k); } if (cnt > c.outs) fail(where + "@" + pc + ": outs_size too small"); break; }
                case 0x71: case 0x77: {
                    int mIdx = c.insns[pc + 1] & 0xffff; int[] proto = protos[methods[mIdx][1]];
                    int cnt, first = 0; int[] regs;
                    if (op == 0x77) { cnt = a; first = c.insns[pc + 2] & 0xffff; regs = new int[cnt]; for (int k = 0; k < cnt; k++) regs[k] = first + k; }
                    else { cnt = (u0 >> 12) & 0xf; int w = c.insns[pc + 2] & 0xffff; int g = (u0 >> 8) & 0xf; regs = new int[cnt]; for (int k = 0; k < cnt; k++) regs[k] = k < 4 ? (w >> (4 * k)) & 0xf : g; }
                    int need = 0; for (int k = 2; k < proto.length; k++) need += strings[typeIds[proto[k]]].equals("D") || strings[typeIds[proto[k]]].equals("J") ? 2 : 1;
                    if (need != cnt) fail(where + "@" + pc + ": invoke reg count " + cnt + " != " + need + " for " + mdesc(mIdx));
                    if (cnt > c.outs) fail(where + "@" + pc + ": outs_size " + c.outs + " < " + cnt);
                    int ri = 0;
                    for (int k = 2; k < proto.length; k++) { String t = strings[typeIds[proto[k]]]; int r = regs[ri];
                        if (r >= R) fail("reg oob");
                        if (t.equals("D")) { if (s[r] != WLO || s[regs[ri + 1]] != WHI || regs[ri + 1] != r + 1) fail(where + "@" + pc + ": wide arg"); ri += 2; }
                        else if (t.equals("I") || t.equals("Z")) { if (s[r] != INT && s[r] != ZERO) fail(where + "@" + pc + ": int arg reg v" + r + " type " + s[r] + " calling " + mdesc(mIdx)); ri++; }
                        else { if (s[r] != REF && s[r] != ZERO) fail(where + "@" + pc + ": ref arg reg v" + r + " type " + s[r] + " calling " + mdesc(mIdx)); ri++; } }
                    String rt = strings[typeIds[proto[0]]];
                    pend = rt.equals("V") ? 0 : (rt.equals("Z") || rt.equals("I")) ? INT : rt.startsWith("L") || rt.startsWith("[") ? REF : -1;
                    if (pend == -1) fail("unsupported return " + rt);
                    break; }
                case 0x29: { int off = (short) c.insns[pc + 1]; next.add(pc + off); falls = false; break; }
                case 0x2a: { int off = (c.insns[pc + 1] & 0xffff) | (c.insns[pc + 2] << 16); next.add(pc + off); falls = false; break; }
                case 0x38: case 0x39: { chkR(where, pc, R, a, a); if (s[a] == UNDEF || s[a] == CONF || s[a] == WLO || s[a] == WHI) fail(where + "@" + pc + ": if on bad reg type"); next.add(pc + (short) c.insns[pc + 1]); break; }
                default: fail("verify: op " + op);
            }
            for (int t : next) if (t < 0 || t >= c.insns.length || !start[t]) fail(where + "@" + pc + ": branch to invalid target " + t);
            if (falls) { next.add(pc + L); if (pc + L >= c.insns.length) fail(where + "@" + pc + ": falls off end of code"); }
            if (pend != 0) pendingKind[pc + L < c.insns.length ? pc + L : 0] = pend; // معلّق للتعليمة التالية
            for (int t : next) {
                if (state[t] == null) { state[t] = s.clone(); work.add(t); }
                else { boolean ch = false; for (int r = 0; r < R; r++) { int m = merge(state[t][r], s[r]); if (state[t][r] == UNDEF) m = (s[r] == UNDEF ? UNDEF : CONF); if (m != state[t][r]) { state[t][r] = m; ch = true; } } if (ch) work.add(t); }
            }
            pendingMap.put(mi + ":" + (falls ? pc + L : -1), pend);
        }
    }
    Map<String, Integer> pendingMap = new HashMap<>();
    int pendingIn(Code c, int pc, boolean[] start, int[][] st) {
        int p = pc - 1; while (p >= 0 && !start[p]) p--;
        if (p < 0) return -1;
        int op = c.insns[p] & 0xff;
        if (op == 0x25) return REF;
        if (op == 0x71 || op == 0x77) {
            int[] proto = protos[methods[c.insns[p + 1] & 0xffff][1]];
            String rt = strings[typeIds[proto[0]]];
            if (rt.equals("Z") || rt.equals("I")) return INT;
            if (rt.startsWith("L") || rt.startsWith("[")) return REF;
        }
        return -1;
    }
    void chkR(String w, int pc, int R, int a, int b) { if (a >= R || b >= R) fail(w + "@" + pc + ": register out of range"); }
    void ref(String w, int pc, int[] s, int r) { if (s[r] != REF && s[r] != ZERO) fail(w + "@" + pc + ": v" + r + " expected ref, has type " + s[r]); }

    // ------------------------------------------------------------ التنفيذ
    Class<?> typeToClass(String d) throws Exception {
        switch (d) { case "V": return void.class; case "Z": return boolean.class; case "I": return int.class; case "D": return double.class; }
        if (d.startsWith("[")) return Class.forName(d.replace('/', '.'));
        return Class.forName(d.substring(1, d.length() - 1).replace('/', '.'));
    }
    Object invoke(int mi, Object[] args) throws Throwable {
        Code c = codeByMethod.get(mi);
        if (c != null) return exec(mi, c, args);
        String owner = ownerOf(mi); String name = strings[methods[mi][2]]; int[] proto = protos[methods[mi][1]];
        Class<?>[] pt = new Class<?>[proto.length - 2];
        for (int k = 2; k < proto.length; k++) pt[k - 2] = typeToClass(strings[typeIds[proto[k]]]);
        Class<?> cls = typeToClass(owner);
        Method m = cls.getMethod(name, pt);
        if (!Modifier.isStatic(m.getModifiers())) fail("non-static target " + mdesc(mi));
        try { return m.invoke(null, args); } catch (InvocationTargetException e) { throw e.getCause(); }
    }
    Object getStatic(int fi) throws Exception {
        String owner = strings[typeIds[fields[fi][0]]]; String name = strings[fields[fi][2]];
        if (owner.equals(ownerDesc)) { if (!statics.containsKey(fi)) statics.put(fi, null); return statics.get(fi); }
        Field f = typeToClass(owner).getField(name); return f.get(null);
    }
    void putStatic(int fi, Object v) throws Exception {
        String owner = strings[typeIds[fields[fi][0]]]; String name = strings[fields[fi][2]];
        if (owner.equals(ownerDesc)) { statics.put(fi, v); return; }
        Field f = typeToClass(owner).getField(name);
        f.set(null, v);
    }
    Set<String> initDone = new HashSet<>();
    Object exec(int mi, Code c, Object[] args) throws Throwable {
        Object[] r = new Object[c.regs];
        for (int i = 0; i < args.length; i++) r[c.regs - c.ins + i] = args[i];
        short[] in = c.insns; int pc = 0; Object result = null; long steps = 0;
        while (true) {
            if (++steps > 2_000_000_000L) fail("step limit");
            int u0 = in[pc] & 0xffff; int op = u0 & 0xff; int a = (u0 >> 8) & 0xff;
            switch (op) {
                case 0x07: r[(u0 >> 8) & 0xf] = r[(u0 >> 12) & 0xf]; pc += 1; break;
                case 0x08: r[a] = r[in[pc + 1] & 0xffff]; pc += 2; break;
                case 0x09: r[in[pc + 1] & 0xffff] = r[in[pc + 2] & 0xffff]; pc += 3; break;
                case 0x0a: case 0x0c: r[a] = result; pc += 1; break;
                case 0x0e: return null;
                case 0x11: return r[a];
                case 0x12: { int v = (u0 >> 12) & 0xf; if ((v & 8) != 0) v -= 16; r[(u0 >> 8) & 0xf] = v == 0 ? null : (Object) Integer.valueOf(v); pc += 1; break; }
                case 0x13: { int v = (short) in[pc + 1]; r[a] = v == 0 ? null : (Object) Integer.valueOf(v); pc += 2; break; }
                case 0x14: { int v = (in[pc + 1] & 0xffff) | (in[pc + 2] << 16); r[a] = v == 0 ? null : (Object) Integer.valueOf(v); pc += 3; break; }
                case 0x18: { long v = (in[pc + 1] & 0xffffL) | ((in[pc + 2] & 0xffffL) << 16) | ((in[pc + 3] & 0xffffL) << 32) | ((in[pc + 4] & 0xffffL) << 48);
                    r[a] = Double.longBitsToDouble(v); r[a + 1] = null; pc += 5; break; }
                case 0x1a: r[a] = strings[in[pc + 1] & 0xffff]; pc += 2; break;
                case 0x1b: r[a] = strings[(in[pc + 1] & 0xffff) | ((in[pc + 2] & 0xffff) << 16)]; pc += 3; break;
                case 0x60: { Object v = getStatic(in[pc + 1] & 0xffff); r[a] = v; pc += 2; break; }
                case 0x62: { r[a] = getStatic(in[pc + 1] & 0xffff); pc += 2; break; }
                case 0x67: case 0x69: { Object v = r[a]; int fi = in[pc + 1] & 0xffff; if (op == 0x67 && v == null) v = 0; putStatic(fi, v); pc += 2; break; }
                case 0x25: { int cnt = a; int first = in[pc + 2] & 0xffff; Object[] arr = new Object[cnt]; for (int k = 0; k < cnt; k++) arr[k] = r[first + k]; result = arr; pc += 3; break; }
                case 0x71: case 0x77: {
                    int mIdx = in[pc + 1] & 0xffff; int[] proto = protos[methods[mIdx][1]];
                    int[] regs; int cnt;
                    if (op == 0x77) { cnt = a; int first = in[pc + 2] & 0xffff; regs = new int[cnt]; for (int k = 0; k < cnt; k++) regs[k] = first + k; }
                    else { cnt = (u0 >> 12) & 0xf; int w = in[pc + 2] & 0xffff; int g = (u0 >> 8) & 0xf; regs = new int[cnt]; for (int k = 0; k < cnt; k++) regs[k] = k < 4 ? (w >> (4 * k)) & 0xf : g; }
                    Object[] av = new Object[proto.length - 2]; int ri = 0;
                    for (int k = 2; k < proto.length; k++) { String t = strings[typeIds[proto[k]]]; Object v = r[regs[ri]];
                        if (t.equals("D")) { av[k - 2] = (Double) v; ri += 2; }
                        else if (t.equals("I")) { av[k - 2] = v == null ? Integer.valueOf(0) : (Integer) v; ri++; }
                        else if (t.equals("Z")) { av[k - 2] = v == null ? Boolean.FALSE : Boolean.valueOf(((Integer) v) != 0); ri++; }
                        else { av[k - 2] = v; ri++; } }
                    Object res = invoke(mIdx, av);
                    String rt = strings[typeIds[proto[0]]];
                    if (rt.equals("Z")) result = ((Boolean) res) ? 1 : 0; else result = res;
                    pc += 3; break; }
                case 0x29: pc += (short) in[pc + 1]; break;
                case 0x2a: pc += (in[pc + 1] & 0xffff) | (in[pc + 2] << 16); break;
                case 0x38: { Object v = r[a]; boolean z = v == null || (v instanceof Integer && (Integer) v == 0); pc += z ? (short) in[pc + 1] : 2; break; }
                case 0x39: { Object v = r[a]; boolean z = v == null || (v instanceof Integer && (Integer) v == 0); pc += !z ? (short) in[pc + 1] : 2; break; }
                default: fail("exec: op " + op);
            }
        }
    }

    /** يتحقّق من كل الدوال ثم يشغّل run(). */
    public static void runDex(byte[] dex, String method) throws Throwable {
        DexVm vm = new DexVm(dex);
        for (int mi : vm.codeByMethod.keySet()) vm.verify(mi);
        int target = -1, clinit = -1;
        for (int i = 0; i < vm.methods.length; i++) {
            if (!vm.codeByMethod.containsKey(i)) continue;
            String n = vm.strings[vm.methods[i][2]];
            if (n.equals(method)) target = i; if (n.equals("<clinit>")) clinit = i;
        }
        if (target < 0) fail("method not found " + method);
        if (clinit >= 0) vm.exec(clinit, vm.codeByMethod.get(clinit), new Object[0]);
        vm.exec(target, vm.codeByMethod.get(target), new Object[0]);
    }
}
