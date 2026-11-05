package com.gtavport.probe;

import android.app.Activity;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/**
 * Gtavport — provas de viabilidade no aparelho (docs/plano-caminho-A.md, Etapa P).
 *
 *  P1  clang e lld empacotados em lib/arm64-v8a executam a partir de nativeLibraryDir.
 *  W^X controle negativo: a mesma ferramenta copiada para getFilesDir() NÃO deve executar.
 *  P2  compilar + linkar uma .so no aparelho, dlopen e chamar uma função (C e C++).
 *  S   carga sintética de C++ (não substitui P3): RSS e tempo do clang numa unidade grande gerada aqui.
 *  P3  se files/p3/ existir no armazenamento externo do app: compila as unidades com as flags dadas,
 *      N jobs nos núcleos grandes, CSV por unidade. Os .cpp vêm do próprio jogo do usuário e nunca
 *      saem do aparelho.
 *  P4a DXC no aparelho: um pixel shader HLSL nosso -> SPIR-V com as flags do upstream -> lower_int64.
 *  P4b se files/p4/*.bin existir (shaders capturados pelo próprio jogo): a cadeia do make_spirv.sh
 *      inteira (XenosRecomp -> ubo -> DXC -> int64); compara com files/p4/ref/NOME.spv do PC se houver.
 *
 * Relatório: tela, logcat (tag Odin2Probe) e files/probe_report.txt no armazenamento externo.
 */
public class ProbeActivity extends Activity {
    private static final String TAG = "Odin2Probe";
    private TextView view;
    private final StringBuilder report = new StringBuilder();
    private File ext, priv, libDir;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        view = new TextView(this);
        view.setTextSize(11);
        view.setTypeface(android.graphics.Typeface.MONOSPACE);
        ScrollView sv = new ScrollView(this);
        sv.addView(view);
        setContentView(sv);
        ext = getExternalFilesDir(null);
        priv = getFilesDir();
        libDir = new File(getApplicationInfo().nativeLibraryDir);
        new Thread(this::runAll, "probe").start();
    }

    private void runAll() {
        try {
            info();
            boolean p1 = p1();
            wxControl();
            boolean p2 = p1 && p2();
            if (p2) synthetic();
            p3();
            boolean p4 = p4a();
            if (p4) p4b();
            line("\nRESUMO: P1=" + (p1 ? "PASSOU" : "FALHOU") + " P2=" + (p2 ? "PASSOU" : (p1 ? "FALHOU" : "não executada"))
                    + " P4a=" + (hasShaderTools() ? (p4 ? "PASSOU" : "FALHOU") : "sem ferramentas no APK"));
        } catch (Throwable t) {
            line("ERRO: " + Log.getStackTraceString(t));
        }
        try (FileWriter w = new FileWriter(new File(ext, "probe_report.txt"))) {
            w.write(report.toString());
        } catch (Exception e) {
            Log.e(TAG, "relatório: " + e);
        }
    }

    // ---------- infra ----------
    private synchronized void line(String s) {
        report.append(s).append('\n');
        Log.i(TAG, s);
        final String all = report.toString();
        runOnUiThread(() -> view.setText(all));
    }

    private String clang() { return new File(libDir, "libclang.so").getPath(); }
    private String tool(String n) { return new File(libDir, "lib" + n + ".so").getPath(); }
    private boolean hasShaderTools() {
        return new File(tool("dxc")).isFile() && new File(tool("xenosrecomp")).isFile() && new File(tool("shader_fix")).isFile();
    }
    private String lld() { return new File(libDir, "libld.lld.so").getPath(); }

    private long[] run(String label, long mask, String... argv) {
        File log = new File(priv, "logs/" + label + ".log");
        log.getParentFile().mkdirs();
        long[] r = Native.runTool(argv, priv.getPath(), log.getPath(), mask);
        String status = r[0] < 0 && r[1] == 0 ? "não iniciou: " + Native.strerror((int) -r[0]) : "exit=" + r[0] + (r[5] != 0 ? " sinal=" + r[5] : "");
        line(String.format(Locale.ROOT, "  [%s] %s wall=%dms rss=%dMB user=%dms sys=%dms", label, status, r[1], r[2] / 1024, r[3], r[4]));
        if (r[0] != 0) line("    log: " + tail(log, 600));
        return r;
    }

    private static String tail(File f, int max) {
        try {
            String s = new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).trim();
            return s.length() > max ? "…" + s.substring(s.length() - max) : s;
        } catch (Exception e) {
            return "(sem log)";
        }
    }

    private float batteryC() {
        Intent i = registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        return i == null ? Float.NaN : i.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, 0) / 10f;
    }

    /** Núcleos com a maior frequência máxima "grande" (no 8 Gen 2: 1 prime + 4 performance). */
    private static long bigCoreMask() {
        int n = Runtime.getRuntime().availableProcessors();
        long[] f = new long[n];
        long min = Long.MAX_VALUE;
        for (int c = 0; c < n; c++) {
            f[c] = readLong("/sys/devices/system/cpu/cpu" + c + "/cpufreq/cpuinfo_max_freq");
            if (f[c] > 0) min = Math.min(min, f[c]);
        }
        long mask = 0;
        for (int c = 0; c < n; c++) if (f[c] > min) mask |= 1L << c;
        return mask;  // 0 = não deu para ler: sem restrição
    }

    private static long readLong(String p) {
        try {
            return Long.parseLong(new String(Files.readAllBytes(new File(p).toPath())).trim());
        } catch (Exception e) {
            return -1;
        }
    }

    private static void write(File f, String s) throws Exception {
        f.getParentFile().mkdirs();
        try (OutputStream o = new FileOutputStream(f)) {
            o.write(s.getBytes(StandardCharsets.UTF_8));
        }
    }

    // ---------- testes ----------
    private void info() {
        line("Gtavport probe — " + Build.MANUFACTURER + " " + Build.MODEL + " / SoC " + (Build.VERSION.SDK_INT >= 31 ? Build.SOC_MODEL : "?")
                + " / Android " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")");
        android.app.ActivityManager.MemoryInfo mi = new android.app.ActivityManager.MemoryInfo();
        ((android.app.ActivityManager) getSystemService(ACTIVITY_SERVICE)).getMemoryInfo(mi);
        line(String.format(Locale.ROOT, "RAM total=%dMB disponível=%dMB limiar_lmk=%dMB", mi.totalMem >> 20, mi.availMem >> 20, mi.threshold >> 20));
        StringBuilder cores = new StringBuilder("núcleos (MHz máx):");
        for (int c = 0; c < Runtime.getRuntime().availableProcessors(); c++)
            cores.append(' ').append(readLong("/sys/devices/system/cpu/cpu" + c + "/cpufreq/cpuinfo_max_freq") / 1000);
        line(cores + String.format(" máscara_grandes=0x%X", bigCoreMask()));
        line("bateria " + batteryC() + " °C; nativeLibraryDir=" + libDir);
        File[] libs = libDir.listFiles();
        if (libs != null) for (File l : libs) line(String.format(Locale.ROOT, "  %s %.1fMB", l.getName(), l.length() / 1048576.0));
    }

    private boolean p1() {
        line("\n== P1: executar clang e lld de nativeLibraryDir");
        long[] a = run("p1_clang_version", 0, clang(), "--version");
        long[] b = run("p1_lld_version", 0, lld(), "-flavor", "gnu", "--version");
        line("  clang: " + tail(new File(priv, "logs/p1_clang_version.log"), 200).replace('\n', ' '));
        return a[0] == 0 && b[0] == 0;
    }

    private void wxControl() {
        line("\n== W^X: cópia do clang em getFilesDir() não deve executar");
        try {
            File copy = new File(priv, "bin/clang");
            copy.getParentFile().mkdirs();
            Files.copy(new File(clang()).toPath(), copy.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            copy.setExecutable(true, true);
            long[] r = run("wx_copy", 0, copy.getPath(), "--version");
            line(r[0] < 0 && r[1] == 0 ? "  esperado: bloqueado (" + Native.strerror((int) -r[0]) + ")"
                                       : "  ATENÇÃO: executou de armazenamento gravável (targetSdk/SELinux diferentes do esperado)");
            copy.delete();
        } catch (Exception e) {
            line("  erro no controle: " + e);
        }
    }

    /** Headers internos do clang (stdint.h, arm_neon.h...), do asset para o armazenamento interno. */
    private File resourceDir() throws Exception {
        File dir = new File(priv, "clang-resource");
        File stamp = new File(dir, ".ok");
        if (stamp.exists()) return dir;
        line("  extraindo headers do clang (asset clang-resource.zip)");
        try (InputStream in = getAssets().open("clang-resource.zip");
             java.util.zip.ZipInputStream z = new java.util.zip.ZipInputStream(in)) {
            java.util.zip.ZipEntry e;
            byte[] buf = new byte[1 << 16];
            while ((e = z.getNextEntry()) != null) {
                File out = new File(dir, e.getName());
                if (!out.getCanonicalPath().startsWith(dir.getCanonicalPath() + File.separator)) continue;
                if (e.isDirectory()) { out.mkdirs(); continue; }
                out.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(out)) {
                    int r;
                    while ((r = z.read(buf)) > 0) o.write(buf, 0, r);
                }
            }
        }
        write(stamp, "1");
        return dir;
    }

    // Mesmas flags que o upstream usa para o código do jogo no Android (loader/src/install.cpp).
    private List<String> baseFlags(File res, boolean cxx) {
        List<String> f = new ArrayList<>(Arrays.asList("--target=aarch64-linux-android30", "-resource-dir", res.getPath(),
                "-nostdlibinc", "-fPIC", "-fno-semantic-interposition", "-fno-stack-protector", "-O2",
                "-march=armv8.2-a+lse+rcpc+dotprod", "-fno-math-errno", "-w"));
        if (cxx) f.addAll(Arrays.asList("-nostdinc++", "-std=gnu++23", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"));
        return f;
    }

    private boolean compileLinkLoad(String tag, String src, boolean cxx, File res, int arg, int expect) throws Exception {
        File dir = new File(priv, "p2");
        File in = new File(dir, tag + (cxx ? ".cpp" : ".c")), obj = new File(dir, tag + ".o"), so = new File(dir, "lib" + tag + ".so");
        write(in, src);
        obj.delete(); so.delete();
        List<String> cc = new ArrayList<>();
        cc.add(clang());
        cc.addAll(baseFlags(res, cxx));
        cc.addAll(Arrays.asList("-c", in.getPath(), "-o", obj.getPath()));
        if (run(tag + "_cc", 0, cc.toArray(new String[0]))[0] != 0) return false;
        // Mesmas opções de link do upstream para libgtav_game.so.
        if (run(tag + "_ld", 0, lld(), "-flavor", "gnu", "-shared", "-soname", so.getName(), "-Bsymbolic", "-z", "now",
                "-z", "relro", "-z", "noexecstack", "-z", "max-page-size=16384", "--hash-style=gnu", "-O1",
                "-o", so.getPath(), obj.getPath())[0] != 0) return false;
        String r = Native.dlopenCall(so.getPath(), "probe_entry", arg);
        line("  dlopen " + so.getName() + " -> " + r + " (esperado ok:" + expect + ")");
        return r.equals("ok:" + expect);
    }

    private boolean p2() throws Exception {
        line("\n== P2: compilar, linkar e carregar uma .so gerada no aparelho");
        File res = resourceDir();
        String c = "#include <stdint.h>\n#include <arm_neon.h>\n"
                + "__attribute__((visibility(\"default\"))) int probe_entry(int x) {\n"
                + "  uint32x4_t v = vdupq_n_u32((uint32_t)x); v = vaddq_u32(v, vdupq_n_u32(1));\n"
                + "  return (int)(vgetq_lane_u32(v, 0) * 2u); }\n";
        boolean okC = compileLinkLoad("probe_c", c, false, res, 20, 42);
        // C++23 freestanding como o código gerado: templates, constexpr, atomics do compilador (LSE).
        String cpp = "#include <stdint.h>\n"
                + "template <typename T> constexpr T bswap(T v) { if constexpr (sizeof(T) == 4) return __builtin_bswap32(v); else return __builtin_bswap64(v); }\n"
                + "static uint32_t mem[4];\n"
                + "extern \"C\" __attribute__((visibility(\"default\"))) int probe_entry(int x) {\n"
                + "  __atomic_fetch_add(&mem[0], (uint32_t)x, __ATOMIC_SEQ_CST);\n"
                + "  uint32_t e = 0; __atomic_compare_exchange_n(&mem[1], &e, bswap<uint32_t>(0x01000000u), false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);\n"
                + "  return (int)(mem[0] + mem[1]); }\n";
        boolean okCpp = compileLinkLoad("probe_cpp", cpp, true, res, 41, 42);
        return okC && okCpp;
    }

    /** Unidade C++ grande gerada aqui, parecida em forma com código recompilado (funções longas e lineares). */
    private void synthetic() throws Exception {
        line("\n== S: carga sintética (referência; NÃO substitui P3)");
        File res = resourceDir();
        StringBuilder s = new StringBuilder("#include <stdint.h>\nstruct Ctx { uint64_t r[32]; double f[32]; uint32_t cr; };\n");
        s.append("static inline uint32_t ld(uint8_t* b, uint32_t a) { return __builtin_bswap32(*(uint32_t*)(b + a)); }\n");
        s.append("static inline void st(uint8_t* b, uint32_t a, uint32_t v) { *(uint32_t*)(b + a) = __builtin_bswap32(v); }\n");
        java.util.Random rnd = new java.util.Random(1234);
        for (int fn = 0; fn < 400; fn++) {
            s.append("extern \"C\" void sub_").append(fn).append("(Ctx& c, uint8_t* b) {\n");
            for (int i = 0; i < 400; i++) {
                int d = rnd.nextInt(32), a = rnd.nextInt(32), k = rnd.nextInt(32);
                switch (rnd.nextInt(5)) {
                    case 0: s.append(" c.r[").append(d).append("] = c.r[").append(a).append("] + c.r[").append(k).append("];\n"); break;
                    case 1: s.append(" c.r[").append(d).append("] = ld(b, uint32_t(c.r[").append(a).append("] + ").append(i * 4).append("));\n"); break;
                    case 2: s.append(" st(b, uint32_t(c.r[").append(a).append("] + ").append(i * 4).append("), uint32_t(c.r[").append(d).append("]));\n"); break;
                    case 3: s.append(" c.f[").append(d % 32).append("] = c.f[").append(a).append("] * c.f[").append(k).append("] + 1.0;\n"); break;
                    default: s.append(" if (int64_t(c.r[").append(a).append("]) < 0) { c.cr = ").append(i).append("; goto L").append(i).append("; }\n L").append(i).append(":;\n"); break;
                }
            }
            s.append("}\n");
        }
        File in = new File(priv, "synth/synth.cpp");
        write(in, s.toString());
        line(String.format(Locale.ROOT, "  unidade: %.1f MB de C++, 400 funções x 400 linhas", in.length() / 1048576.0));
        List<String> cc = new ArrayList<>();
        cc.add(clang());
        cc.addAll(baseFlags(res, true));
        cc.addAll(Arrays.asList("-c", in.getPath(), "-o", new File(priv, "synth/synth.o").getPath()));
        float t0 = batteryC();
        run("synth_cc", bigCoreMask(), cc.toArray(new String[0]));
        line("  bateria " + t0 + " -> " + batteryC() + " °C");
    }

    // Flags do upstream (tools/android/make_spirv.sh).
    private static final String[] DXC_FLAGS = {"-HV", "2021", "-all-resources-bound", "-spirv", "-fvk-use-dx-layout",
            "-Wno-ignored-attributes", "-Qstrip_debug"};

    private String[] dxcArgs(String profile, File in, File out, boolean vs) {
        List<String> a = new ArrayList<>(Arrays.asList(tool("dxc"), "-T", profile));
        a.addAll(Arrays.asList(DXC_FLAGS));
        if (vs) a.add("-fvk-invert-y");
        a.addAll(Arrays.asList("-Fo", out.getPath(), in.getPath()));
        return a.toArray(new String[0]);
    }

    private static boolean isSpirv(File f) {
        try (java.io.FileInputStream in = new java.io.FileInputStream(f)) {
            byte[] h = new byte[4];
            return in.read(h) == 4 && h[0] == 0x03 && h[1] == 0x02 && h[2] == 0x23 && h[3] == 0x07;
        } catch (Exception e) {
            return false;
        }
    }

    private boolean p4a() throws Exception {
        line("\n== P4a: DXC no aparelho (shader HLSL próprio, não do jogo)");
        if (!hasShaderTools()) {
            line("  APK sem libdxc.so/libxenosrecomp.so/libshader_fix.so — pulada (build_dxc.sh, build_xenosrecomp.sh, build_shader_fix.sh)");
            return false;
        }
        // O dxc e o XenosRecomp carregam libdxcompiler.so de nativeLibraryDir.
        android.system.Os.setenv("LD_LIBRARY_PATH", libDir.getPath(), true);
        run("p4_dxc_version", 0, tool("dxc"), "--version");
        line("  " + tail(new File(priv, "logs/p4_dxc_version.log"), 200).replace('\n', ' '));
        File dir = new File(priv, "p4a");
        File hlsl = new File(dir, "probe_ps.hlsl"), spv = new File(dir, "probe_ps.spv");
        // Asset próprio (64-bit de propósito: exercita o lower_int64). O mesmo arquivo foi compilado no
        // host pelo DXC do dxc-bin (mesmo commit): assets/probe_ps.ref.spv, para comparar byte a byte.
        copyAsset("probe_ps.hlsl", hlsl);
        spv.delete();
        long[] r = run("p4a_dxc", bigCoreMask(), dxcArgs("ps_6_0", hlsl, spv, false));
        if (r[0] != 0 || !isSpirv(spv)) { line("  FALHOU: DXC não gerou SPIR-V"); return false; }
        File ref = new File(dir, "probe_ps.ref.spv");
        copyAsset("probe_ps.ref.spv", ref);
        boolean same = Arrays.equals(Files.readAllBytes(ref.toPath()), Files.readAllBytes(spv.toPath()));
        line("  SPIR-V do DXC do aparelho vs. DXC do PC (mesmo commit): " + (same ? "IDÊNTICO" : "DIFERENTE (" + spv.length() + " vs " + ref.length() + " bytes)"));
        File low = new File(dir, "probe_ps.lowered.spv");
        r = run("p4a_int64", 0, tool("shader_fix"), "int64", spv.getPath(), low.getPath());
        boolean ok = r[0] == 0 && isSpirv(low);
        line(String.format(Locale.ROOT, "  SPIR-V %d bytes -> lower_int64 %d bytes: %s", spv.length(), low.length(), ok ? "ok" : "FALHOU"));
        return ok && same;
    }

    private void copyAsset(String name, File to) throws Exception {
        to.getParentFile().mkdirs();
        try (InputStream in = getAssets().open(name); OutputStream o = new FileOutputStream(to)) {
            byte[] b = new byte[1 << 16];
            int n;
            while ((n = in.read(b)) > 0) o.write(b, 0, n);
        }
    }

    /** shader_common.h do XenosRecomp com as duas trocas de texto do make_spirv.sh (asset). */
    private File shaderCommon() throws Exception {
        File f = new File(priv, "p4/shader_common.h");
        if (f.isFile()) return f;
        f.getParentFile().mkdirs();
        try (InputStream in = getAssets().open("shader_common.h"); OutputStream o = new FileOutputStream(f)) {
            byte[] b = new byte[1 << 16];
            int n;
            while ((n = in.read(b)) > 0) o.write(b, 0, n);
        }
        return f;
    }

    private void p4b() throws Exception {
        File src = new File(ext, "p4");
        File[] bins = src.listFiles((d, n) -> n.endsWith(".bin"));
        if (bins == null || bins.length == 0) {
            line("\n== P4b: sem files/p4/*.bin (shaders capturados pelo jogo) — pulada");
            return;
        }
        line("\n== P4b: cadeia completa do make_spirv.sh em " + bins.length + " shader(s) capturado(s)");
        File common = shaderCommon();
        File work = new File(priv, "p4/work");
        deleteTree(work);
        work.mkdirs();
        File csv = new File(ext, "p4_result.csv");
        int ok = 0, same = 0, refs = 0;
        try (FileWriter w = new FileWriter(csv)) {
            w.write("shader,xenos_ms,ubo_ms,dxc_ms,int64_ms,dxc_rss_kb,result,ref\n");
            for (File bin : bins) {
                String name = bin.getName().replaceAll("\\.bin$", "");
                boolean vs = !name.startsWith("ps_");
                File hlsl = new File(work, name + ".hlsl"), spv = new File(work, name + ".spv");
                long[] a = run("p4b_xr_" + name, 0, tool("xenosrecomp"), bin.getPath(), hlsl.getPath(), common.getPath());
                long[] b = a[0] == 0 ? run("p4b_ubo_" + name, 0, tool("shader_fix"), "ubo", hlsl.getPath()) : new long[6];
                long[] c = a[0] == 0 && b[0] == 0 ? run("p4b_dxc_" + name, bigCoreMask(), dxcArgs(vs ? "vs_6_0" : "ps_6_0", hlsl, spv, vs)) : new long[6];
                long[] d = c[0] == 0 && spv.isFile() ? run("p4b_int64_" + name, 0, tool("shader_fix"), "int64", spv.getPath(), spv.getPath()) : new long[6];
                boolean good = a[0] == 0 && b[0] == 0 && c[0] == 0 && d[0] == 0 && isSpirv(spv);
                String ref = "";
                File refSpv = new File(src, "ref/" + name + ".spv");
                if (good && refSpv.isFile()) {
                    refs++;
                    boolean eq = Arrays.equals(Files.readAllBytes(refSpv.toPath()), Files.readAllBytes(spv.toPath()));
                    ref = eq ? "idêntico ao PC" : "DIFERENTE do PC";
                    if (eq) same++;
                }
                if (good) ok++;
                w.write(String.format(Locale.ROOT, "%s,%d,%d,%d,%d,%d,%s,%s\n", name, a[1], b[1], c[1], d[1], c[2], good ? "ok" : "falhou", ref));
                hlsl.delete();
                spv.delete();
            }
        }
        deleteTree(work);  // HLSL/SPIR-V são derivados do jogo: não ficam no aparelho além do teste
        line(String.format(Locale.ROOT, "  P4b: %d/%d ok; comparados com o PC: %d, idênticos: %d -> %s", ok, bins.length, refs, same, csv));
    }

    /**
     * P3: files/p3/flags.txt (uma flag por linha, além das baseFlags; caminhos relativos a p3/),
     * files/p3/units/*.cpp, opcional files/p3/jobs.txt. Copiados para o armazenamento interno.
     */
    private void p3() throws Exception {
        File src = new File(ext, "p3");
        if (!new File(src, "units").isDirectory()) {
            line("\n== P3: sem files/p3/units no armazenamento externo — pulada (ver android-ondevice/probe/README.md)");
            return;
        }
        line("\n== P3: unidades reais do jogo (ficam no aparelho)");
        File res = resourceDir();
        File work = new File(priv, "p3");
        copyTree(src, work);
        List<String> extra = new ArrayList<>();
        File ff = new File(work, "flags.txt");
        if (ff.exists())
            for (String l : new String(Files.readAllBytes(ff.toPath()), StandardCharsets.UTF_8).split("\n")) {
                l = l.trim();
                if (l.isEmpty() || l.startsWith("#")) continue;
                extra.add(l.replace("@P3@", work.getPath()));
            }
        File[] units = new File(work, "units").listFiles((d, n) -> n.endsWith(".cpp"));
        Arrays.sort(units, (a, b) -> Long.compare(b.length(), a.length()));  // maiores primeiro
        long mask = bigCoreMask();
        int jobs = (int) Math.max(1, readLong(new File(work, "jobs.txt").getPath()));
        if (!new File(work, "jobs.txt").exists()) jobs = Math.max(1, Long.bitCount(mask) - 1);
        line("  " + units.length + " unidades, jobs=" + jobs + String.format(" máscara=0x%X", mask) + ", bateria " + batteryC() + " °C");
        File csv = new File(ext, "p3_result.csv");
        try (FileWriter w = new FileWriter(csv)) {
            w.write("unit,bytes,exit,wall_ms,maxrss_kb,user_ms,sys_ms,battery_c\n");
            ExecutorService pool = Executors.newFixedThreadPool(jobs);
            List<Future<String>> fs = new ArrayList<>();
            long t0 = System.nanoTime();
            final long m = mask;
            for (File u : units) {
                fs.add(pool.submit(() -> {
                    List<String> cc = new ArrayList<>();
                    cc.add(clang());
                    cc.addAll(baseFlags(res, true));
                    cc.addAll(extra);
                    cc.addAll(Arrays.asList("-c", u.getPath(), "-o", u.getPath() + ".o"));
                    long[] r = run("p3_" + u.getName(), m, cc.toArray(new String[0]));
                    new File(u.getPath() + ".o").delete();
                    return String.format(Locale.ROOT, "%s,%d,%d,%d,%d,%d,%d,%.1f\n", u.getName(), u.length(), r[0], r[1], r[2], r[3], r[4], batteryC());
                }));
            }
            long maxRss = 0;
            int fails = 0;
            for (Future<String> f : fs) {
                String row = f.get();
                w.write(row);
                String[] p = row.split(",");
                maxRss = Math.max(maxRss, Long.parseLong(p[4]));
                if (!p[2].equals("0")) fails++;
            }
            pool.shutdown();
            line(String.format(Locale.ROOT, "  P3: %d unidades em %.1f s, RSS máx por job %d MB, falhas %d, bateria %.1f °C -> %s",
                    units.length, (System.nanoTime() - t0) / 1e9, maxRss / 1024, fails, batteryC(), csv));
        }
        deleteTree(work);  // não deixar código derivado do jogo além do necessário
    }

    private static void copyTree(File from, File to) throws Exception {
        if (from.isDirectory()) {
            to.mkdirs();
            File[] k = from.listFiles();
            if (k != null) for (File f : k) copyTree(f, new File(to, f.getName()));
        } else {
            Files.copy(from.toPath(), to.toPath(), java.nio.file.StandardCopyOption.REPLACE_EXISTING);
        }
    }

    private static void deleteTree(File f) {
        File[] k = f.listFiles();
        if (k != null) for (File c : k) deleteTree(c);
        f.delete();
    }
}
