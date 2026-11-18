package com.gtavrecomp.app;

import android.content.Context;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

/**
 * Gtavport: geração de quadros (odin2/framegen). Importa o Lossless.dll do próprio usuário (Lossless Scaling, Steam):
 * os shaders dele viram SPIR-V em files/lsfg/spirv (liblsfg_extract.so). O DLL não é copiado nem guardado, e nada dele
 * vai no APK. O jogo lê a mesma pasta (GTAVPORT_LSFG_DIR, Odin2DeviceBuild.configureRuntime).
 */
final class Odin2Framegen {
    private Odin2Framegen() {}

    private static boolean loaded;

    private static synchronized boolean load() {
        if (!loaded) {
            try {
                System.loadLibrary("lsfg_extract");
                loaded = true;
            } catch (Throwable t) {
                return false;
            }
        }
        return true;
    }

    private static native String nativeImport(int fd, String root);
    private static native String nativeState(String root);
    private static native void nativeRemove(String root);

    static String root(Context c) {
        return c.getFilesDir().getAbsolutePath();
    }

    /** Uma linha para a tela: os shaders instalados ou por que não há. */
    static String state(Context c) {
        if (!load()) return "indisponível: liblsfg_extract.so fora do APK";
        String s = nativeState(root(c));
        return s.startsWith("1|") ? "Instalado: " + s.substring(2) : "Sem shaders: importe o seu Lossless.dll";
    }

    static boolean installed(Context c) {
        return load() && nativeState(root(c)).startsWith("1|");
    }

    /** Importa (bloqueia: chamar fora da thread de UI). Devolve a mensagem para o usuário. */
    static String importDll(Context c, Uri uri) {
        if (!load()) return "liblsfg_extract.so fora do APK";
        try (ParcelFileDescriptor pfd = c.getContentResolver().openFileDescriptor(uri, "r")) {
            if (pfd == null) return "Não foi possível abrir o arquivo";
            String r = nativeImport(pfd.detachFd(), root(c));  // (o nativo fecha o descritor)
            int bar = r.indexOf('|');
            int code = Integer.parseInt(r.substring(0, bar));
            String detail = r.substring(bar + 1);
            if (code == 0) return "Shaders instalados: " + detail;
            String[] why = {"", "não deu para ler o arquivo", "não é um DLL do Windows",
                    "faltam shaders: outra versão do Lossless Scaling", "um shader não traduziu", "SPIR-V inválido",
                    "os bindings de um shader não batem", "não deu para gravar os shaders"};
            return "Recusado (" + (code > 0 && code < why.length ? why[code] : "erro " + code) + "): " + detail;
        } catch (Exception e) {
            return "Erro ao importar: " + e;
        }
    }

    static void remove(Context c) {
        if (load()) nativeRemove(root(c));
    }
}
