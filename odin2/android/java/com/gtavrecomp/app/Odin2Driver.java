package com.gtavrecomp.app;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Gtavport: drivers Vulkan customizados (Turnip/Mesa ou outro pacote no formato adrenotools),
 * carregados pelo libodin2driver.so (odin2/driver).
 *
 * Pacote: zip com meta.json ("libraryName" = o .so do driver) + o .so. É o formato dos pacotes
 * Turnip distribuídos para emuladores Android. Instalado em getFilesDir()/drivers/NOME/
 * (armazenamento interno: o driver precisa ser carregável com dlopen, o externo não permite).
 *
 * Escolha do driver, em ordem de prioridade:
 *   1. env:ODIN2_VK_DRIVER=NOME no args.txt (benchmark: tools/bench/matrix.sh)
 *   2. a escolha feita na tela "Driver gráfico" (Odin2DriverActivity)
 *   3. driver do sistema
 * Driver pedido e não encontrado/não carregado = system, com o motivo em logs/driver_status.txt.
 */
final class Odin2Driver {
    static final String TAG = "Odin2Driver";
    static final String SYSTEM = "system";
    private static final String PREFS = "odin2_driver";
    private static final int EM_AARCH64 = 183;

    private Odin2Driver() {}

    /** Informações de um driver instalado (meta.json). */
    static final class Info {
        String id, name = "", description = "", author = "", vendor = "", driverVersion = "", libraryName;
        int minApi;
        File dir;

        String title() {
            String t = name.isEmpty() ? id : name;
            return driverVersion.isEmpty() ? t : t + " — " + driverVersion;
        }
    }

    // ---------- chamado pela GameActivity antes de carregar as bibliotecas nativas ----------

    static void configure(Context c) {
        try {
            importPendingAdb(c);
        } catch (Exception e) {
            Log.e(TAG, "importação via adb falhou: " + e);
        }
        Odin2DriverActivity.addShortcut(c);
        Odin2PerfLogsActivity.addShortcut(c);
        Odin2DeviceBuild.configureRuntime(c);  // Fase 1: ferramentas de shader do aparelho
        File status = statusFile(c);
        status.getParentFile().mkdirs();
        setenv("ODIN2_VK_STATUS_FILE", status.getAbsolutePath());

        String name = System.getenv("ODIN2_VK_DRIVER");  // args.txt (benchmark) tem prioridade
        String source = "args.txt";
        if (name == null || name.isEmpty()) {
            name = selected(c);
            source = "tela Driver gráfico";
        }
        if (name.equals(SYSTEM)) {
            setenv("ODIN2_VK_DRIVER", SYSTEM);
            Log.i(TAG, "driver pedido: system");
            return;
        }
        Info info = load(c, name);
        if (info == null || !new File(info.dir, info.libraryName).isFile()) {
            Log.e(TAG, "driver '" + name + "' (" + source + ") não instalado: usando system");
            writeStatus(status, name, SYSTEM, "driver não instalado");
            setenv("ODIN2_VK_DRIVER", SYSTEM);
            return;
        }
        File tmp = new File(c.getCacheDir(), "adrenotools");
        tmp.mkdirs();
        setenv("ODIN2_VK_DRIVER", name);
        setenv("ODIN2_VK_DRIVER_DIR", info.dir.getAbsolutePath() + "/");
        setenv("ODIN2_VK_DRIVER_LIB", info.libraryName);
        setenv("ODIN2_VK_HOOK_DIR", c.getApplicationInfo().nativeLibraryDir + "/");
        setenv("ODIN2_VK_TMP_DIR", tmp.getAbsolutePath() + "/");
        // Variáveis do próprio driver (ex.: TU_DEBUG=sysmem no Turnip), só com ele escolhido.
        for (String[] kv : parseEnv(driverEnv(c, name))) {
            setenv(kv[0], kv[1]);
            Log.i(TAG, "  " + kv[0] + "=" + kv[1]);
        }
        Log.i(TAG, "driver pedido: " + name + " (" + info.libraryName + ", via " + source + ")");
    }

    // ---------- estado ----------

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static String selected(Context c) {
        return prefs(c).getString("selected", SYSTEM);
    }

    static void select(Context c, String id) {
        prefs(c).edit().putString("selected", id).apply();
    }

    static String driverEnv(Context c, String id) {
        return prefs(c).getString("env:" + id, "");
    }

    static void setDriverEnv(Context c, String id, String env) {
        prefs(c).edit().putString("env:" + id, env.trim()).apply();
    }

    /** Linhas NOME=VALOR; nomes restritos a [A-Z0-9_] para não mexer em variáveis do sistema por engano. */
    static List<String[]> parseEnv(String text) {
        List<String[]> out = new ArrayList<>();
        for (String l : text.split("\n")) {
            l = l.trim();
            int eq = l.indexOf('=');
            if (l.isEmpty() || l.startsWith("#") || eq < 1) continue;
            String k = l.substring(0, eq).trim();
            if (!k.matches("[A-Z][A-Z0-9_]*") || k.startsWith("ODIN2_") || k.equals("LD_PRELOAD") || k.equals("LD_LIBRARY_PATH")) continue;
            out.add(new String[] {k, l.substring(eq + 1).trim()});
        }
        return out;
    }

    static File statusFile(Context c) {
        return new File(c.getExternalFilesDir(null), "logs/driver_status.txt");
    }

    /** Resultado da última execução do jogo: requested/effective/detail. */
    static String lastStatus(Context c) {
        File f = statusFile(c);
        if (!f.isFile()) return "ainda não executado com o seletor de driver";
        try {
            String req = "", eff = "", det = "";
            for (String l : new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\n")) {
                if (l.startsWith("requested=")) req = l.substring(10);
                else if (l.startsWith("effective=")) eff = l.substring(10);
                else if (l.startsWith("detail=")) det = l.substring(7);
            }
            if (req.equals(eff)) return "última execução: " + eff + " carregado";
            return "última execução: pediu " + req + ", usou " + eff + (det.isEmpty() ? "" : " (" + det + ")");
        } catch (Exception e) {
            return "status ilegível: " + e;
        }
    }

    private static void writeStatus(File f, String req, String eff, String detail) {
        try (OutputStream o = new FileOutputStream(f)) {
            o.write(("requested=" + req + "\neffective=" + eff + "\ndetail=" + detail + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (Exception ignored) {
        }
    }

    // ---------- drivers instalados ----------

    static File driversDir(Context c) {
        return new File(c.getFilesDir(), "drivers");
    }

    static List<Info> list(Context c) {
        List<Info> out = new ArrayList<>();
        File[] dirs = driversDir(c).listFiles(File::isDirectory);
        if (dirs == null) return out;
        Arrays.sort(dirs);
        for (File d : dirs) {
            if (d.getName().endsWith(".tmp")) continue;
            Info i = load(c, d.getName());
            if (i != null) out.add(i);
        }
        return out;
    }

    static Info load(Context c, String id) {
        File dir = new File(driversDir(c), id);
        return dir.isDirectory() ? readInfo(dir, id) : null;
    }

    static Info readInfo(File dir, String id) {
        Info i = new Info();
        i.id = id;
        i.dir = dir;
        File meta = new File(dir, "meta.json");
        if (meta.isFile()) {
            try {
                JSONObject j = new JSONObject(new String(Files.readAllBytes(meta.toPath()), StandardCharsets.UTF_8));
                i.name = j.optString("name", "");
                i.description = j.optString("description", "");
                i.author = j.optString("author", "");
                i.vendor = j.optString("vendor", "");
                i.driverVersion = j.optString("driverVersion", "");
                i.minApi = j.optInt("minApi", 0);
                String lib = j.optString("libraryName", "");
                if (!lib.isEmpty() && !lib.contains("/")) i.libraryName = lib;
            } catch (Exception e) {
                Log.w(TAG, "meta.json inválido em " + dir + ": " + e);
            }
        }
        if (i.libraryName == null) {
            File[] sos = dir.listFiles((d, n) -> n.endsWith(".so"));
            if (sos != null && sos.length == 1) i.libraryName = sos[0].getName();
        }
        return i.libraryName == null ? null : i;
    }

    static boolean remove(Context c, String id) {
        if (id.equals(selected(c))) select(c, SYSTEM);
        prefs(c).edit().remove("env:" + id).apply();
        File d = new File(driversDir(c), id);
        deleteTree(d);
        return !d.exists();
    }

    /**
     * Instala um pacote (zip) e devolve o id. Valida: meta.json/libraryName, .so ELF aarch64,
     * minApi. Lança IllegalArgumentException com a mensagem para o usuário.
     */
    static String install(Context c, InputStream zip, String fileName) throws Exception {
        File root = driversDir(c);
        root.mkdirs();
        File staging = new File(root, "import.tmp");
        deleteTree(staging);
        staging.mkdirs();
        try {
            if (!unzipFlat(zip, staging)) throw new IllegalArgumentException("o zip não tem nenhum .so ou meta.json");
            Info info = readInfo(staging, "import");
            if (info == null) throw new IllegalArgumentException("pacote sem meta.json com libraryName (formato adrenotools)");
            File so = new File(staging, info.libraryName);
            if (!so.isFile()) throw new IllegalArgumentException("meta.json aponta para " + info.libraryName + ", que não está no zip");
            if (!isElfAarch64(so)) throw new IllegalArgumentException(info.libraryName + " não é uma biblioteca ARM64");
            if (info.minApi > Build.VERSION.SDK_INT)
                throw new IllegalArgumentException("o driver exige Android API " + info.minApi + " (este aparelho: " + Build.VERSION.SDK_INT + ")");
            String base = !info.name.isEmpty() ? info.name + (info.driverVersion.isEmpty() ? "" : "-" + info.driverVersion)
                                               : fileName.replaceAll("(?i)\\.zip$", "");
            String id = base.replaceAll("[^A-Za-z0-9._-]+", "_").replaceAll("^[._]+", "");
            if (id.isEmpty() || id.equals(SYSTEM)) id = "driver";
            if (id.length() > 60) id = id.substring(0, 60);
            File dest = new File(root, id);
            deleteTree(dest);
            if (!staging.renameTo(dest)) throw new IllegalStateException("não foi possível mover para " + dest);
            Log.i(TAG, "driver instalado: " + id + " (" + info.libraryName + ")");
            return id;
        } finally {
            deleteTree(staging);
        }
    }

    /** Pacotes copiados por adb para files/drivers_import/ no armazenamento externo (benchmark). */
    private static void importPendingAdb(Context c) throws Exception {
        File ext = c.getExternalFilesDir(null);
        File[] zips = ext == null ? null : new File(ext, "drivers_import").listFiles((d, n) -> n.endsWith(".zip"));
        if (zips == null) return;
        for (File z : zips) {
            // Por adb o nome do driver é o nome do zip (é o que tools/driver/push_driver.sh anuncia).
            String id = z.getName().substring(0, z.getName().length() - 4).replaceAll("[^A-Za-z0-9._-]", "_");
            try (InputStream in = new FileInputStream(z)) {
                String got = install(c, in, z.getName());
                if (!got.equals(id)) {
                    File from = new File(driversDir(c), got), to = new File(driversDir(c), id);
                    deleteTree(to);
                    from.renameTo(to);
                }
                z.delete();
            } catch (Exception e) {
                Log.e(TAG, "pacote inválido " + z + ": " + e.getMessage());
            }
        }
    }

    private static boolean isElfAarch64(File f) {
        try (RandomAccessFile r = new RandomAccessFile(f, "r")) {
            byte[] h = new byte[20];
            r.readFully(h);
            return h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F' && h[4] == 2
                    && ((h[18] & 0xff) | (h[19] & 0xff) << 8) == EM_AARCH64;
        } catch (Exception e) {
            return false;
        }
    }

    /** Extrai só .so e .json, sem subpastas (nenhum caminho do zip é usado). */
    private static boolean unzipFlat(InputStream zip, File dest) throws Exception {
        boolean any = false;
        try (ZipInputStream in = new ZipInputStream(zip)) {
            ZipEntry e;
            byte[] buf = new byte[1 << 16];
            while ((e = in.getNextEntry()) != null) {
                if (e.isDirectory()) continue;
                String n = new File(e.getName()).getName();
                if (!(n.endsWith(".so") || n.endsWith(".json")) || n.startsWith(".")) continue;
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

    static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteTree(k);
        f.setWritable(true, true);
        f.delete();
    }

    static void setenv(String k, String v) {
        try {
            android.system.Os.setenv(k, v, true);
        } catch (Exception e) {
            Log.w(TAG, "setenv " + k + ": " + e);
        }
    }
}
