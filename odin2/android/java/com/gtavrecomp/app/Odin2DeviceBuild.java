package com.gtavrecomp.app;

import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Gtavport (Fase 1): instalar e compilar o jogo no próprio aparelho, sem PC.
 *
 * O nativo (libgtav_setup.so, install.cpp RunDeviceBuild) copia e confere os dados dos discos, roda o
 * codegen do ReXGlue, confere o código gerado contra a referência, compila com o clang e linka com o
 * lld, todos executados de nativeLibraryDir. Os dados de build vêm do asset gtavport-toolroot.zip,
 * extraído em getFilesDir()/toolroot. O resultado (files/libgtav_game.so + files/pack/) fica onde a
 * tela de setup põe um pack importado do PC, então o resto do app não muda.
 *
 * Os shaders: o jogo traduz cada shader no aparelho na primeira vez que aparece (shader_compiler.cpp
 * com o patch 0011), com as ferramentas configuradas por configureRuntime().
 *
 * Estado compartilhado (mesmo processo) entre o serviço que roda a build e a tela que a mostra.
 */
public final class Odin2DeviceBuild {
    static final String TAG = "Odin2DeviceBuild";
    static final String ASSET = "gtavport-toolroot.zip", ASSET_VERSION = "gtavport-toolroot.version";

    // ---------- estado (lido pela tela, escrito pela thread da build) ----------
    static volatile boolean running, finished;
    static volatile String stage = "", error;
    static volatile double progress;
    static final AtomicBoolean cancelled = new AtomicBoolean(false);
    private static final StringBuilder logTail = new StringBuilder();

    static synchronized String log() {
        return logTail.toString();
    }

    private static synchronized void appendLog(String s) {
        logTail.append(s).append('\n');
        if (logTail.length() > 16000) logTail.delete(0, logTail.length() - 12000);
    }

    // ---------- callbacks do nativo (mesmos nomes que JavaUi em android_setup_jni.cpp procura) ----------
    void onStage(String s) {
        stage = s;
        appendLog("» " + s);
    }

    void onLog(String s) {
        appendLog(s);
    }

    void onProgress(double f) {
        progress = f;
    }

    boolean isCancelled() {
        return cancelled.get();
    }

    /** A instalação pergunta (arquivo diferente do esperado etc.): aqui sempre "não", com o motivo no log. */
    boolean onAsk(String q) {
        appendLog("? " + q + " -> não (responda pela tela de setup do upstream se precisar continuar)");
        return false;
    }

    private native String nativeBuild(String toolroot, String libdir, String home, String privateDir,
                                      String disc1, String disc2, String tu);

    /** Disponível neste APK? (o asset e as ferramentas vêm só na build do Gtavport com a Fase 1). */
    static boolean available(Context c) {
        try (InputStream in = c.getAssets().open(ASSET_VERSION)) {
            return new File(c.getApplicationInfo().nativeLibraryDir, "libclang.so").isFile();
        } catch (Exception e) {
            return false;
        }
    }

    /** Roda a build inteira (na thread do serviço). null = concluída. */
    static String run(Context c, String disc1, String disc2, String tu) {
        running = true;
        finished = false;
        error = null;
        progress = 0;
        cancelled.set(false);
        synchronized (Odin2DeviceBuild.class) {
            logTail.setLength(0);
        }
        try {
            System.loadLibrary("gtav_setup");
            File root = ensureToolroot(c);
            Odin2DeviceBuild b = new Odin2DeviceBuild();
            String err = b.nativeBuild(root.getAbsolutePath(), c.getApplicationInfo().nativeLibraryDir,
                    c.getExternalFilesDir(null).getAbsolutePath(), c.getFilesDir().getAbsolutePath(), disc1, disc2, tu);
            error = err;
            if (err == null) appendLog("Concluído.");
            else appendLog("Parou: " + err);
            return err;
        } catch (Throwable t) {
            error = String.valueOf(t);
            Log.e(TAG, "build", t);
            return error;
        } finally {
            running = false;
            finished = true;
        }
    }

    /** Antes de o jogo carregar as bibliotecas (Odin2Driver.configure): ferramentas de shader do aparelho. */
    static void configureRuntime(Context c) {
        if (!available(c)) return;
        try {
            File root = ensureToolroot(c);
            android.system.Os.setenv("GTAVPORT_NATIVE_LIB_DIR", c.getApplicationInfo().nativeLibraryDir, true);
            android.system.Os.setenv("GTAVPORT_SHADER_COMMON", new File(root, "shader_common.android.h").getAbsolutePath(), true);
        } catch (Exception e) {
            Log.w(TAG, "configureRuntime: " + e);
        }
    }

    /** Extrai o asset para files/toolroot quando falta ou é de outra versão do APK. */
    static synchronized File ensureToolroot(Context c) throws Exception {
        File root = new File(c.getFilesDir(), "toolroot");
        String want;
        try (InputStream in = c.getAssets().open(ASSET_VERSION)) {
            want = new String(in.readAllBytes(), StandardCharsets.UTF_8).trim();
        }
        File ver = new File(root, "toolroot.version");
        if (ver.isFile() && new String(Files.readAllBytes(ver.toPath()), StandardCharsets.UTF_8).trim().equals(want)) return root;
        File tmp = new File(c.getFilesDir(), "toolroot.new");
        Odin2Driver.deleteTree(tmp);
        tmp.mkdirs();
        String base = tmp.getCanonicalPath() + File.separator;
        try (InputStream raw = c.getAssets().open(ASSET); ZipInputStream z = new ZipInputStream(raw)) {
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            while ((e = z.getNextEntry()) != null) {
                File out = new File(tmp, e.getName());
                if (!out.getCanonicalPath().startsWith(base)) continue;  // nada fora da pasta
                if (e.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                out.getParentFile().mkdirs();
                try (OutputStream o = new FileOutputStream(out)) {
                    int n;
                    while ((n = z.read(buf)) > 0) o.write(buf, 0, n);
                }
            }
        }
        Odin2Driver.deleteTree(root);
        if (!tmp.renameTo(root)) throw new IllegalStateException("não foi possível preparar " + root);
        Log.i(TAG, "toolroot " + want + " extraído");
        return root;
    }

    private Odin2DeviceBuild() {}
}
