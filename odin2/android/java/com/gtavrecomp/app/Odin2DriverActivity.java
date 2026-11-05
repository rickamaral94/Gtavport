package com.gtavrecomp.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RadioButton;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Gtavport: tela "Driver gráfico". Importa pacotes de driver Turnip (zip no formato adrenotools),
 * lista, escolhe, testa (em processo separado), define variáveis do driver e remove. A escolha vale
 * no próximo início do jogo. Abre pelo botão na tela de setup e pelo atalho do ícone do app.
 */
public class Odin2DriverActivity extends Activity {
    private static final int REQ_IMPORT = 1;
    private static final int REQ_TEST = 2;
    private LinearLayout list;
    private TextView status;
    private final Map<String, String> testResults = new HashMap<>();
    private volatile boolean busy;

    /** Atalho do ícone do app (o APK pula o setup quando o jogo já está instalado). */
    static void addShortcut(Context c) {
        try {
            android.content.pm.ShortcutManager sm = c.getSystemService(android.content.pm.ShortcutManager.class);
            if (sm == null) return;
            Intent i = new Intent(c, Odin2DriverActivity.class).setAction(Intent.ACTION_MAIN)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            android.content.pm.ShortcutInfo info = new android.content.pm.ShortcutInfo.Builder(c, "odin2_driver")
                    .setShortLabel("Driver gráfico").setLongLabel("Driver gráfico (Turnip / sistema)").setIntent(i).build();
            sm.addDynamicShortcuts(java.util.Collections.singletonList(info));
        } catch (Exception e) {
            Log.w(Odin2Driver.TAG, "atalho: " + e);
        }
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        int pad = dp(16);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(pad, pad, pad, pad);
        box.setBackgroundColor(Color.rgb(16, 20, 24));
        box.addView(text("Driver gráfico (Vulkan)", 24, Color.WHITE));
        box.addView(text("Escolha o driver usado pelo jogo. Pacotes Turnip (Mesa) no formato zip com meta.json "
                + "podem ser importados abaixo. A escolha vale a partir do próximo início do jogo. Se o driver "
                + "escolhido não carregar, o jogo usa o driver do sistema e o motivo aparece aqui.", 14, Color.LTGRAY));
        status = text("", 14, Color.rgb(255, 210, 120));
        box.addView(status);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        box.addView(list);
        Button imp = new Button(this);
        imp.setText("Importar pacote de driver (.zip)…");
        imp.setOnClickListener(v -> pickZip());
        box.addView(imp);
        Button play = new Button(this);
        play.setText("Iniciar o jogo");
        play.setOnClickListener(v -> {
            startActivity(new Intent(this, GameActivity.class));
            finish();
        });
        box.addView(play);
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
        String st = Odin2Driver.lastStatus(this);
        String override = argsOverride();
        if (override != null) st += "\nATENÇÃO: args.txt define env:ODIN2_VK_DRIVER=" + override + " e tem prioridade sobre esta tela.";
        status.setText(st);
        list.removeAllViews();
        String sel = Odin2Driver.selected(this);
        list.addView(row(null, sel.equals(Odin2Driver.SYSTEM)));
        List<Odin2Driver.Info> drivers = Odin2Driver.list(this);
        boolean found = sel.equals(Odin2Driver.SYSTEM);
        for (Odin2Driver.Info i : drivers) {
            list.addView(row(i, i.id.equals(sel)));
            found |= i.id.equals(sel);
        }
        if (!found) Odin2Driver.select(this, Odin2Driver.SYSTEM);  // driver escolhido foi removido
        if (drivers.isEmpty()) list.addView(text("Nenhum driver customizado instalado.", 13, Color.GRAY));
    }

    /** Uma linha: seleção, nome, detalhes, Testar / Variáveis / Remover, resultado do teste. */
    private View row(Odin2Driver.Info info, boolean selected) {
        String id = info == null ? Odin2Driver.SYSTEM : info.id;
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.VERTICAL);
        r.setPadding(0, dp(8), 0, dp(8));
        RadioButton rb = new RadioButton(this);
        rb.setChecked(selected);
        rb.setTextColor(Color.WHITE);
        rb.setTextSize(16);
        rb.setText(info == null ? "Driver do sistema (Qualcomm)" : info.title());
        rb.setOnClickListener(v -> {
            Odin2Driver.select(this, id);
            refresh();
        });
        r.addView(rb);
        if (info != null) {
            StringBuilder d = new StringBuilder();
            if (!info.vendor.isEmpty()) d.append(info.vendor).append(" · ");
            if (!info.author.isEmpty()) d.append(info.author).append(" · ");
            d.append(info.libraryName);
            if (!info.description.isEmpty()) d.append("\n").append(info.description);
            String env = Odin2Driver.driverEnv(this, id);
            if (!env.isEmpty()) d.append("\nvariáveis: ").append(env.replace('\n', ' '));
            r.addView(text(d.toString(), 12, Color.GRAY));
        }
        LinearLayout btns = new LinearLayout(this);
        btns.setOrientation(LinearLayout.HORIZONTAL);
        btns.setGravity(Gravity.START);
        btns.addView(button("Testar", v -> test(id)));
        if (info != null) {
            btns.addView(button("Variáveis", v -> editEnv(info)));
            btns.addView(button("Remover", v -> confirmRemove(info)));
        }
        r.addView(btns);
        String res = testResults.get(id);
        if (res != null) r.addView(text(res, 12, res.startsWith("OK") ? Color.rgb(140, 220, 140) : Color.rgb(255, 130, 130)));
        return r;
    }

    // ---------- ações ----------

    private void pickZip() {
        if (busy) return;
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
                .putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
        startActivityForResult(i, REQ_IMPORT);
    }

    private void test(String id) {
        testResults.put(id, "testando…");
        refresh();
        startActivityForResult(new Intent(this, Odin2DriverTestActivity.class).putExtra(Odin2DriverTestActivity.EXTRA_ID, id), REQ_TEST);
        pendingTest = id;
    }

    private String pendingTest;

    private void editEnv(Odin2Driver.Info info) {
        EditText e = new EditText(this);
        e.setText(Odin2Driver.driverEnv(this, info.id));
        e.setHint("NOME=VALOR, uma por linha (ex.: TU_DEBUG=sysmem)");
        e.setMinLines(3);
        new AlertDialog.Builder(this).setTitle("Variáveis de " + info.title())
                .setMessage("Aplicadas só com este driver escolhido. Ex. Turnip: TU_DEBUG=sysmem ou TU_DEBUG=gmem "
                        + "para comparar modos de renderização. Meça antes e depois (docs/baseline.md).")
                .setView(e)
                .setPositiveButton("Salvar", (d, w) -> {
                    Odin2Driver.setDriverEnv(this, info.id, e.getText().toString());
                    refresh();
                })
                .setNegativeButton("Cancelar", null).show();
    }

    private void confirmRemove(Odin2Driver.Info info) {
        new AlertDialog.Builder(this).setTitle("Remover driver").setMessage("Remover " + info.title() + "?")
                .setPositiveButton("Remover", (d, w) -> {
                    Odin2Driver.remove(this, info.id);
                    testResults.remove(info.id);
                    refresh();
                })
                .setNegativeButton("Cancelar", null).show();
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_IMPORT && res == RESULT_OK && data != null && data.getData() != null) {
            importZip(data.getData());
        } else if (req == REQ_TEST) {
            String id = data != null ? data.getStringExtra(Odin2DriverTestActivity.EXTRA_ID) : pendingTest;
            if (id == null) id = Odin2Driver.SYSTEM;
            String json = data != null ? data.getStringExtra(Odin2DriverTestActivity.EXTRA_RESULT) : null;
            testResults.put(id, json == null ? "FALHOU: o processo de teste caiu ao carregar o driver (incompatível com este aparelho?)"
                                             : describe(json));
            refresh();
        }
    }

    private void importZip(Uri uri) {
        busy = true;
        status.setText("Importando…");
        new Thread(() -> {
            String msg;
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                String id = Odin2Driver.install(this, in, displayName(uri));
                msg = "Driver instalado: " + id + ". Selecione-o e use Testar antes de jogar.";
            } catch (IllegalArgumentException e) {
                msg = "Pacote recusado: " + e.getMessage();
            } catch (Exception e) {
                msg = "Erro ao importar: " + e;
            }
            final String m = msg;
            runOnUiThread(() -> {
                busy = false;
                refresh();
                new AlertDialog.Builder(this).setTitle("Importar driver").setMessage(m).setPositiveButton("OK", null).show();
            });
        }, "driver-import").start();
    }

    private static String describe(String json) {
        try {
            JSONObject j = new JSONObject(json);
            if (!j.optBoolean("ok")) return "FALHOU: " + j.optString("error");
            StringBuilder s = new StringBuilder("OK: ").append(j.optString("device")).append(" · Vulkan ").append(j.optString("api"));
            String dn = j.optString("driverName"), di = j.optString("driverInfo");
            if (!dn.isEmpty()) s.append("\n").append(dn).append(di.isEmpty() ? "" : " — " + di);
            s.append("\nshaderInt64=").append(j.optBoolean("shaderInt64"))
             .append(" geometryShader=").append(j.optBoolean("geometryShader"))
             .append(" pipelineLibrary=").append(j.optBoolean("graphicsPipelineLibrary"))
             .append(" · ").append(j.optInt("extensions")).append(" extensões");
            return s.toString();
        } catch (Exception e) {
            return "FALHOU: resposta ilegível";
        }
    }

    /** Valor de env:ODIN2_VK_DRIVER no args.txt (benchmark), que tem prioridade sobre esta tela. */
    private String argsOverride() {
        File f = new File(getExternalFilesDir(null), "args.txt");
        if (!f.isFile()) return null;
        try {
            for (String l : new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\n")) {
                l = l.trim();
                if (l.startsWith("env:ODIN2_VK_DRIVER=")) return l.substring(20);
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        }
        return "driver.zip";
    }

    // ---------- UI ----------

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
