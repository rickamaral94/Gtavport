package com.gtavrecomp.app;

import android.content.Context;
import android.util.Log;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Gtavport (Etapa 0.5): escolha do driver Vulkan, carregado pelo libodin2driver.so via adrenotools.
 *
 * Importação: um pacote de driver no formato usual do Turnip (zip com meta.json + o .so) copiado
 * para files/drivers_import/ no armazenamento externo do app (adb push funciona, sem root) é
 * extraído no próximo início para getFilesDir()/drivers/NOME/ (armazenamento interno: o driver
 * precisa ser carregável com dlopen, o que o armazenamento externo não permite). NOME = nome do zip.
 *
 * Escolha: a linha "env:ODIN2_VK_DRIVER=NOME" no args.txt (ou "system"). Daqui saem as variáveis
 * que o lado nativo lê (odin2/driver/odin2_driver.h). Driver pedido e não encontrado = system,
 * com erro no log: o benchmark registra o driver efetivo, nunca só o pedido.
 */
final class Odin2Driver {
    private static final String TAG = "Odin2Driver";

    private Odin2Driver() {}

    static void configure(Context c) {
        try {
            importPending(c);
        } catch (Exception e) {
            Log.e(TAG, "importação de driver falhou: " + e);
        }
        String name = System.getenv("ODIN2_VK_DRIVER");
        if (name == null || name.isEmpty() || name.equals("system")) {
            setenv("ODIN2_VK_DRIVER", "system");
            Log.i(TAG, "driver pedido: system");
            return;
        }
        File dir = new File(new File(c.getFilesDir(), "drivers"), name);
        String lib = libraryName(dir);
        if (lib == null || !new File(dir, lib).isFile()) {
            Log.e(TAG, "driver '" + name + "' não instalado em " + dir + ": usando system");
            setenv("ODIN2_VK_DRIVER", "system");
            return;
        }
        File tmp = new File(c.getCacheDir(), "adrenotools");
        tmp.mkdirs();
        setenv("ODIN2_VK_DRIVER_DIR", dir.getAbsolutePath() + "/");
        setenv("ODIN2_VK_DRIVER_LIB", lib);
        setenv("ODIN2_VK_HOOK_DIR", c.getApplicationInfo().nativeLibraryDir + "/");
        setenv("ODIN2_VK_TMP_DIR", tmp.getAbsolutePath() + "/");
        Log.i(TAG, "driver pedido: " + name + " (" + lib + ")");
    }

    /** meta.json "libraryName" (formato dos pacotes Turnip/adrenotools), ou o único .so da pasta. */
    static String libraryName(File dir) {
        File meta = new File(dir, "meta.json");
        if (meta.isFile()) {
            try {
                String s = new String(Files.readAllBytes(meta.toPath()), StandardCharsets.UTF_8);
                String lib = new JSONObject(s).optString("libraryName", null);
                if (lib != null && !lib.isEmpty() && !lib.contains("/")) return lib;
            } catch (Exception e) {
                Log.w(TAG, "meta.json inválido em " + dir + ": " + e);
            }
        }
        File[] sos = dir.listFiles((d, n) -> n.endsWith(".so"));
        return (sos != null && sos.length == 1) ? sos[0].getName() : null;
    }

    private static void importPending(Context c) throws Exception {
        File ext = c.getExternalFilesDir(null);
        if (ext == null) return;
        File[] zips = new File(ext, "drivers_import").listFiles((d, n) -> n.endsWith(".zip"));
        if (zips == null) return;
        for (File zip : zips) {
            String name = zip.getName().substring(0, zip.getName().length() - 4).replaceAll("[^A-Za-z0-9._-]", "_");
            File dest = new File(new File(c.getFilesDir(), "drivers"), name);
            File staging = new File(dest.getPath() + ".tmp");
            deleteTree(staging);
            staging.mkdirs();
            if (unzipFlat(zip, staging) && libraryName(staging) != null) {
                deleteTree(dest);
                if (staging.renameTo(dest)) {
                    zip.delete();
                    Log.i(TAG, "driver importado: " + name);
                    continue;
                }
            }
            Log.e(TAG, "pacote de driver inválido (precisa de meta.json + .so): " + zip);
            deleteTree(staging);
        }
    }

    /** Extrai só .so e .json, sem subpastas (nada de caminhos com ".." do zip). */
    private static boolean unzipFlat(File zip, File dest) throws Exception {
        boolean any = false;
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            while ((e = in.getNextEntry()) != null) {
                if (e.isDirectory()) continue;
                String n = new File(e.getName()).getName();
                if (!(n.endsWith(".so") || n.endsWith(".json"))) continue;
                File out = new File(dest, n);
                try (OutputStream o = new FileOutputStream(out)) {
                    int r;
                    while ((r = in.read(buf)) > 0) o.write(buf, 0, r);
                }
                out.setReadable(true, true);
                out.setWritable(false, false);
                any = true;
            }
        }
        return any;
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.delete();
    }

    private static void setenv(String k, String v) {
        try {
            android.system.Os.setenv(k, v, true);
        } catch (Exception e) {
            Log.w(TAG, "setenv " + k + ": " + e);
        }
    }
}
