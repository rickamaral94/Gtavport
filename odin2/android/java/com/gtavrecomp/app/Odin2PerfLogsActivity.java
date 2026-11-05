package com.gtavrecomp.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.MediaStore;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Gtavport: tela "Logs de desempenho". Lista as sessões gravadas na seção Performance do menu do
 * jogo (files/logs/perf/perf_*.txt + .csv), mostra o resumo e exporta um zip para Downloads/GtavPort
 * (MediaStore, sem permissão de armazenamento) com o resumo, o CSV por quadro, o gtav.log e o status
 * do driver, para análise no ADL (tools/bench/perf_report.py).
 */
public class Odin2PerfLogsActivity extends Activity {
    private static final String TAG = "Odin2Perf";
    private LinearLayout list;

    static void addShortcut(Context c) {
        try {
            android.content.pm.ShortcutManager sm = c.getSystemService(android.content.pm.ShortcutManager.class);
            if (sm == null) return;
            Intent i = new Intent(c, Odin2PerfLogsActivity.class).setAction(Intent.ACTION_MAIN)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            android.content.pm.ShortcutInfo info = new android.content.pm.ShortcutInfo.Builder(c, "odin2_perf")
                    .setShortLabel("Logs de desempenho").setLongLabel("Logs de desempenho (gargalos, Turnip)").setIntent(i).build();
            sm.addDynamicShortcuts(java.util.Collections.singletonList(info));
        } catch (Exception e) {
            Log.w(TAG, "atalho: " + e);
        }
    }

    private File perfDir() {
        return new File(getExternalFilesDir(null), "logs/perf");
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        int pad = dp(16);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad, pad, pad);
        box.setBackgroundColor(Color.rgb(16, 20, 24));
        box.addView(text("Logs de desempenho", 24, Color.WHITE, false));
        box.addView(text("Grave uma sessão no jogo: menu (RB + Back) > Performance > Gravar sessão. Cada gravação gera um "
                + "resumo (fps, 1% low, gargalo provável, clocks, margem térmica, driver) e um CSV por quadro. "
                + "Exporte para Downloads/GtavPort e compare no PC com tools/bench/perf_report.py.", 14, Color.LTGRAY, false));
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        box.addView(list);
        ScrollView sv = new ScrollView(this);
        sv.addView(box);
        setContentView(sv);
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
    }

    private void refresh() {
        list.removeAllViews();
        File[] txts = perfDir().listFiles((d, n) -> n.startsWith("perf_") && n.endsWith(".txt"));
        if (txts == null || txts.length == 0) {
            list.addView(text("Nenhuma sessão gravada ainda.", 14, Color.GRAY, false));
            return;
        }
        Arrays.sort(txts, (a, c) -> c.getName().compareTo(a.getName()));  // mais recente primeiro
        for (File t : txts) list.addView(session(t));
    }

    private View session(File txt) {
        String base = txt.getName().substring(0, txt.getName().length() - 4);
        File csv = new File(txt.getParentFile(), base + ".csv");
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.VERTICAL);
        r.setPadding(0, dp(10), 0, dp(10));
        r.addView(text(base.replace("perf_", "sessão ") + (csv.isFile() ? String.format(" · CSV %.1f MB", csv.length() / 1048576.0) : ""),
                16, Color.WHITE, false));
        String summary;
        try {
            summary = new String(Files.readAllBytes(txt.toPath()), StandardCharsets.UTF_8);
        } catch (Exception e) {
            summary = "resumo ilegível: " + e;
        }
        r.addView(text(summary.trim(), 11, Color.rgb(190, 200, 210), true));
        LinearLayout btns = new LinearLayout(this);
        btns.setOrientation(LinearLayout.HORIZONTAL);
        btns.addView(button("Exportar para Downloads", v -> export(base)));
        btns.addView(button("Apagar", v -> new AlertDialog.Builder(this).setTitle("Apagar sessão").setMessage("Apagar " + base + "?")
                .setPositiveButton("Apagar", (d, w) -> {
                    txt.delete();
                    csv.delete();
                    refresh();
                })
                .setNegativeButton("Cancelar", null).show()));
        r.addView(btns);
        return r;
    }

    /** zip em Downloads/GtavPort: resumo, CSV, gtav.log e status do driver. */
    private void export(String base) {
        new Thread(() -> {
            String msg;
            try {
                if (Build.VERSION.SDK_INT < 29) throw new IllegalStateException("exportação requer Android 10+");
                ContentValues cv = new ContentValues();
                cv.put(MediaStore.MediaColumns.DISPLAY_NAME, "gtavport_" + base + ".zip");
                cv.put(MediaStore.MediaColumns.MIME_TYPE, "application/zip");
                cv.put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/GtavPort");
                Uri uri = getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cv);
                if (uri == null) throw new IllegalStateException("MediaStore recusou o arquivo");
                File ext = getExternalFilesDir(null);
                File[] files = {new File(perfDir(), base + ".txt"), new File(perfDir(), base + ".csv"),
                        new File(ext, "logs/gtav.log"), new File(ext, "logs/driver_status.txt"), new File(ext, "gtav_settings.ini")};
                try (OutputStream os = getContentResolver().openOutputStream(uri); ZipOutputStream z = new ZipOutputStream(os)) {
                    byte[] buf = new byte[1 << 16];
                    for (File f : files) {
                        if (!f.isFile()) continue;
                        z.putNextEntry(new ZipEntry(f.getName()));
                        try (InputStream in = new FileInputStream(f)) {
                            int n;
                            while ((n = in.read(buf)) > 0) z.write(buf, 0, n);
                        }
                        z.closeEntry();
                    }
                }
                msg = "Exportado: Download/GtavPort/gtavport_" + base + ".zip";
            } catch (Exception e) {
                msg = "Falhou: " + e.getMessage();
            }
            final String m = msg;
            runOnUiThread(() -> new AlertDialog.Builder(this).setTitle("Exportar").setMessage(m).setPositiveButton("OK", null).show());
        }, "perf-export").start();
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    private TextView text(String s, int sp, int color, boolean mono) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (mono) t.setTypeface(Typeface.MONOSPACE);
        t.setPadding(0, dp(4), 0, dp(4));
        return t;
    }

    private Button button(String label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setOnClickListener(l);
        return b;
    }
}
