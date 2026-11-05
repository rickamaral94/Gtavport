package com.gtavrecomp.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Gtavport (Fase 1): a tela de setup do app. Escolha Disc 1, Disc 2 e o Title Update 27; o app copia e
 * confere os arquivos e recompila o jogo aqui mesmo (Odin2BuildService), sem PC, como os ports de
 * BOTW/TOTK. Pode sair da tela: a build continua (notificação). Interrompida, continua de onde parou.
 * Usar um pack feito no PC fica como opção avançada (a tela de setup do upstream).
 */
public class Odin2DeviceBuildActivity extends Activity {
    private static final String[] LABELS = {"Disc 1 (Install)", "Disc 2 (Play)", "Title Update 27 (tu00000009_00000000)"};
    private final Uri[] picked = new Uri[3];
    private final TextView[] pickedText = new TextView[3];
    private final Button[] pickButtons = new Button[3];
    private Button start, play;
    private ProgressBar bar;
    private TextView status, log;
    private SharedPreferences prefs;
    private final Handler ui = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        prefs = getSharedPreferences("odin2_device_build", MODE_PRIVATE);
        for (int i = 0; i < 3; i++) {
            String s = prefs.getString("uri" + i, null);
            if (s != null) picked[i] = Uri.parse(s);
        }
        int pad = dp(16);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad, pad, pad);
        box.setBackgroundColor(Color.rgb(16, 20, 24));
        box.addView(text("GTA V — instalar e compilar neste aparelho", 22, Color.WHITE));
        box.addView(text("Tudo no aparelho, sem PC: escolha os seus Disc 1, Disc 2 e Title Update 27. O app copia e confere ~16 GB, "
                + "recompila o código do jogo aqui (codegen + clang, vários minutos nos núcleos grandes) e o deixa "
                + "pronto. Os shaders são traduzidos enquanto você joga: nos primeiros minutos alguns objetos aparecem "
                + "com atraso. Precisa de ~25 GB livres durante a instalação. Deixe na tomada e com ventoinha ligada.", 14, Color.LTGRAY));
        if (!Odin2DeviceBuild.available(this)) {
            box.addView(text("Este APK não traz as ferramentas de compilação. Use a release do Gtavport com a Fase 1.",
                    15, Color.rgb(255, 130, 130)));
        }
        for (int i = 0; i < 3; i++) {
            final int slot = i;
            box.addView(text(LABELS[i], 15, Color.WHITE));
            LinearLayout row = new LinearLayout(this);
            row.setOrientation(LinearLayout.HORIZONTAL);
            pickButtons[i] = button("Escolher…", v -> pick(slot));
            row.addView(pickButtons[i]);
            pickedText[i] = text("", 13, Color.LTGRAY);
            pickedText[i].setPadding(dp(8), 0, 0, 0);
            row.addView(pickedText[i]);
            box.addView(row);
        }
        start = button("Instalar e compilar", v -> onStartClicked());
        box.addView(start);
        bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(1000);
        box.addView(bar);
        status = text("", 15, Color.rgb(255, 210, 120));
        box.addView(status);
        play = button("Jogar", v -> {
            startActivity(new Intent(this, GameActivity.class));
            finish();
        });
        play.setVisibility(View.GONE);
        box.addView(play);
        log = text("", 11, Color.rgb(150, 160, 170));
        log.setTypeface(Typeface.MONOSPACE);
        box.addView(log);
        box.addView(text("Avançado", 14, Color.GRAY));
        box.addView(button("Usar um pack feito no PC (Build for Android)…", v -> {
            startActivity(new Intent().setClassName(this, "com.gtavrecomp.app.SetupActivity")
                    .putExtra("com.gtavrecomp.app.SETUP", true).putExtra("odin2_pc", true));
        }));
        box.addView(button("Driver gráfico…", v -> startActivity(new Intent(this, Odin2DriverActivity.class))));
        box.addView(button("Logs de desempenho…", v -> startActivity(new Intent(this, Odin2PerfLogsActivity.class))));
        ScrollView sv = new ScrollView(this);
        sv.addView(box);
        setContentView(sv);
        if (Build.VERSION.SDK_INT >= 33) requestPermissions(new String[] {"android.permission.POST_NOTIFICATIONS"}, 1);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        poll();
    }

    @Override
    protected void onPause() {
        super.onPause();
        ui.removeCallbacksAndMessages(null);
    }

    private void poll() {
        refresh();
        ui.postDelayed(this::poll, 500);
    }

    private void refresh() {
        boolean busy = Odin2DeviceBuild.running;
        for (int i = 0; i < 3; i++) {
            pickedText[i].setText(picked[i] != null ? displayName(picked[i]) : "não escolhido");
            pickButtons[i].setEnabled(!busy);
        }
        start.setText(busy ? "Cancelar" : "Instalar e compilar");
        start.setEnabled(Odin2DeviceBuild.available(this));
        bar.setProgress((int) (Odin2DeviceBuild.progress * 1000));
        if (busy) {
            status.setText(Odin2DeviceBuild.stage);
        } else if (!Odin2DeviceBuild.finished && Odin2DeviceBuild.installed(this)) {
            status.setText("O jogo já está compilado neste aparelho para esta versão do app.");
            play.setVisibility(View.VISIBLE);
        } else if (Odin2DeviceBuild.finished) {
            String e = Odin2DeviceBuild.error;
            status.setText(e == null ? "Pronto: o jogo foi compilado neste aparelho."
                    : Odin2DeviceBuild.cancelled.get() ? "Cancelado. Toque em Instalar e compilar para continuar de onde parou."
                    : "Parou: " + e);
            play.setVisibility(e == null ? View.VISIBLE : View.GONE);
        }
        String l = Odin2DeviceBuild.log();
        if (!l.equals(log.getText().toString())) log.setText(l);
    }

    private void onStartClicked() {
        if (Odin2DeviceBuild.running) {
            Odin2DeviceBuild.cancelled.set(true);
            status.setText("Cancelando…");
            return;
        }
        for (int i = 0; i < 3; i++) {
            if (picked[i] == null) {
                new AlertDialog.Builder(this).setMessage("Escolha " + LABELS[i] + ".").setPositiveButton("OK", null).show();
                return;
            }
        }
        String[] uris = {picked[0].toString(), picked[1].toString(), picked[2].toString()};
        Odin2DeviceBuild.finished = false;
        Intent i = new Intent(this, Odin2BuildService.class).putExtra(Odin2BuildService.EXTRA_URIS, uris);
        startForegroundService(i);
    }

    private void pick(int slot) {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
        startActivityForResult(i, 100 + slot);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        int slot = req - 100;
        if (res != RESULT_OK || data == null || data.getData() == null || slot < 0 || slot > 2) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {
        }
        picked[slot] = uri;
        prefs.edit().putString("uri" + slot, uri.toString()).apply();
        refresh();
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        }
        return uri.getLastPathSegment();
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    private TextView text(String s, int sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
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
