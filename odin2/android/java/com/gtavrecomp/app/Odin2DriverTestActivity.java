package com.gtavrecomp.app;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.widget.TextView;

/**
 * Gtavport: "Testar" um driver. Roda em processo próprio (android:process=":drivertest" no
 * manifesto): o adrenotools troca o driver uma vez por processo, e um driver quebrado que derrube
 * o processo não leva junto a tela de drivers nem o jogo. Devolve o JSON de driver_test.cpp.
 */
public class Odin2DriverTestActivity extends Activity {
    static final String EXTRA_ID = "id";
    static final String EXTRA_RESULT = "result";

    static native String nativeTest(String dir, String lib, String hookDir, String tmpDir);

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        TextView t = new TextView(this);
        t.setText("Testando driver…");
        t.setPadding(48, 48, 48, 48);
        setContentView(t);
        String id = getIntent().getStringExtra(EXTRA_ID);
        new Thread(() -> {
            String result;
            try {
                String dir = null, lib = null;
                if (id != null && !id.equals(Odin2Driver.SYSTEM)) {
                    Odin2Driver.Info info = Odin2Driver.load(this, id);
                    if (info == null) throw new IllegalStateException("driver não instalado");
                    dir = info.dir.getAbsolutePath() + "/";
                    lib = info.libraryName;
                    for (String[] kv : Odin2Driver.parseEnv(Odin2Driver.driverEnv(this, id))) Odin2Driver.setenv(kv[0], kv[1]);
                }
                System.loadLibrary("odin2driver");
                java.io.File tmp = new java.io.File(getCacheDir(), "adrenotools-test");
                tmp.mkdirs();
                result = nativeTest(dir, lib, getApplicationInfo().nativeLibraryDir + "/", tmp.getAbsolutePath() + "/");
            } catch (Throwable e) {
                result = "{\"ok\":false,\"error\":\"" + String.valueOf(e.getMessage()).replace("\"", "'") + "\"}";
            }
            final String r = result;
            runOnUiThread(() -> {
                setResult(RESULT_OK, new Intent().putExtra(EXTRA_ID, id).putExtra(EXTRA_RESULT, r));
                finish();
            });
        }, "driver-test").start();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // O driver carregado fica neste processo: encerrá-lo deixa o próximo teste limpo.
        if (isFinishing()) Process.killProcess(Process.myPid());
    }
}
