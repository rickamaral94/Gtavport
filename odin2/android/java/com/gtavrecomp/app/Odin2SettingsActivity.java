package com.gtavrecomp.app;

import android.app.AlertDialog;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Gtavport: configurações de vídeo e de exibição antes de abrir o jogo, no estilo do menu do GTA V.
 * Grava o mesmo gtav_settings.ini (getExternalFilesDir()/gtav_settings.ini, name=value) que o menu do
 * jogo (Esc / Start) lê no início e grava (menu.cpp kSettings): as duas telas mostram os mesmos valores.
 * Linhas que esta tela não conhece ficam como estão.
 */
public class Odin2SettingsActivity extends Odin2GtaActivity {
    private final Map<String, String> ini = new LinkedHashMap<>();
    private final List<String> other = new ArrayList<>();  // comentários e chaves que não são desta tela
    private final Map<Item, String> keys = new LinkedHashMap<>();
    private boolean dirty;
    private Item diagItem;

    // Padrões do jogo no Android (os REXCVAR_DEFINE de cada opção).
    private static final String[][] DEFAULTS = {
            {"gtav_native_render_scale", "0.75"}, {"odin2_upscaler", "bilinear"}, {"odin2_fsr_sharpness", "0.2"},
            {"odin2_antialias", "off"}, {"gtav_native_max_anisotropy", "16"}, {"gtav_native_vsync", "true"},
            {"gtav_fps_limit", "30"}, {"gtav_native_fps", "false"}, {"gtav_touch_controls", "true"},
            {"gtav_native_async_pipelines", "true"}, {"user_language", "1"},
    };

    @Override
    protected String title() {
        return "CONFIGURAÇÕES";
    }

    @Override
    protected String subtitleText() {
        return "Valem na próxima vez que o jogo abrir. Também ficam no menu do jogo (Start).";
    }

    @Override
    protected String[][] hints() {
        return new String[][] {{"◀▶", "Alterar"}, {"LB RB", "Abas"}, {"B", "Salvar e voltar"}};
    }

    private File file() {
        return new File(getExternalFilesDir(null), "gtav_settings.ini");
    }

    @Override
    protected void onCreate(android.os.Bundle b) {
        load();
        super.onCreate(b);
    }

    private void load() {
        ini.clear();
        other.clear();
        try {
            for (String l : new String(Files.readAllBytes(file().toPath()), StandardCharsets.UTF_8).split("\n")) {
                int eq = l.indexOf('=');
                if (!l.startsWith("#") && eq > 0) ini.put(l.substring(0, eq).trim(), l.substring(eq + 1).trim());
                else if (!l.isEmpty()) other.add(l);
            }
        } catch (Exception ignored) {
            // sem arquivo ainda: padrões
        }
    }

    private String get(String key) {
        String v = ini.get(key);
        if (v != null) return v;
        for (String[] d : DEFAULTS)
            if (d[0].equals(key)) return d[1];
        return "";
    }

    /** Opção ligada a uma chave do ini; o valor atual escolhe a entrada mais próxima. */
    private Item opt(Tab t, String key, String label, String help, String[] values, String[] names) {
        String cur = get(key);
        int idx = -1;
        for (int i = 0; i < values.length; i++)
            if (values[i].equals(cur)) idx = i;
        if (idx < 0) {
            try {  // números: "0.750000" do jogo vs "0.75" daqui
                double c = Double.parseDouble(cur);
                for (int i = 0; i < values.length; i++)
                    if (Math.abs(Double.parseDouble(values[i]) - c) < 0.005) idx = i;
            } catch (NumberFormatException ignored) {
            }
        }
        Item it = t.add(new Item(label, help, values, names, Math.max(idx, 0)));
        keys.put(it, key);
        return it;
    }

    private static final String[] ON_OFF = {"true", "false"}, ON_OFF_NAMES = {"Ligado", "Desligado"};

    @Override
    protected void buildTabs() {
        keys.clear();
        Tab v = new Tab("Vídeo");
        tabs.add(v);
        opt(v, "gtav_native_render_scale", "Resolução interna",
                "Parte de 1280x720 em que a cena 3D é desenhada. Menor = mais rápido e mais suave (use junto com um upscaler).",
                new String[] {"0.5", "0.6", "0.67", "0.75", "0.85", "1"},
                new String[] {"50%", "60%", "67%", "75%", "85%", "100% (720p nativo)"});
        opt(v, "odin2_upscaler", "Upscaler",
                "Como a imagem chega ao tamanho da tela. Snapdragon GSR 1: uma passada, feito para Adreno (detecta a direção das bordas). "
                        + "AMD FSR 1: duas passadas, um pouco mais nítido e mais caro.",
                new String[] {"bilinear", "sgsr1", "fsr1"},
                new String[] {"Bilinear (original)", "Snapdragon GSR 1", "AMD FSR 1"});
        opt(v, "odin2_fsr_sharpness", "Nitidez do FSR",
                "Só com o AMD FSR 1: quanto o RCAS realça os detalhes depois de ampliar.",
                new String[] {"0", "0.2", "0.5", "1", "2"},
                new String[] {"Máxima", "Alta (padrão AMD)", "Média", "Baixa", "Mínima"});
        opt(v, "odin2_antialias", "Anti-aliasing",
                "Suaviza o serrilhado das bordas. FXAA: uma passada, leve, um pouco mais suave. SMAA 1x: três passadas, bordas mais nítidas, usa mais GPU.",
                new String[] {"off", "fxaa", "smaa"}, new String[] {"Desligado", "FXAA", "SMAA 1x"});
        opt(v, "gtav_native_max_anisotropy", "Filtro anisotrópico",
                "Nitidez das texturas vistas de lado (chão, ruas ao longe). Diminua se a GPU for o gargalo.",
                new String[] {"1", "2", "4", "8", "16"}, new String[] {"Desligado", "2x", "4x", "8x", "16x"});

        Tab d = new Tab("Exibição");
        tabs.add(d);
        opt(d, "user_language", "Idioma do jogo",
                "Textos e legendas do jogo: o idioma que o console informa ao jogo (o GTA V do Xbox 360 traz português do "
                        + "Brasil). Vale na próxima vez que o jogo abrir.",
                new String[] {"1", "9", "5", "4", "6", "3"},
                new String[] {"English", "Português (Brasil)", "Español", "Français", "Italiano", "Deutsch"});
        opt(d, "gtav_fps_limit", "Limite de FPS",
                "30: o limitador do próprio jogo (mais estável, esquenta menos). Sem limite: o aparelho roda o quanto conseguir.",
                new String[] {"30", "45", "60", "0"}, new String[] {"30 (o do jogo)", "45", "60", "Sem limite"});
        opt(d, "gtav_native_vsync", "VSync",
                "Espera a tela antes de mostrar cada quadro: sem rasgos na imagem; pode somar um pouco de atraso.", ON_OFF, ON_OFF_NAMES);
        opt(d, "gtav_native_async_pipelines", "Efeitos novos",
                "Sem travar: um efeito que aparece pela primeira vez fica de fora por um ou dois quadros enquanto é preparado. "
                        + "Exatos: ele é preparado na hora (uma travadinha na primeira vez).",
                ON_OFF, new String[] {"Sem travar", "Exatos"});
        opt(d, "gtav_native_fps", "Contador de FPS", "Mostra os quadros por segundo no canto da tela.", ON_OFF, ON_OFF_NAMES);
        opt(d, "gtav_touch_controls", "Controles na tela",
                "Mostra o controle virtual na tela de toque. Com um controle conectado ele pode ficar desligado.", ON_OFF, ON_OFF_NAMES);
        Tab p = new Tab("Desempenho");
        tabs.add(p);
        File req = new File(getExternalFilesDir(null), "diag_request.txt");
        int pending = 0;
        try {
            pending = Integer.parseInt(new String(Files.readAllBytes(req.toPath()), StandardCharsets.UTF_8).trim().split("\\s+")[0]);
        } catch (Exception ignored) {
        }
        diagItem = p.add(new Item("Medir desempenho no próximo jogo",
                "Mede o jogo e gera um relatório (o do TOTK/BOTW): fps, travadas, GPU, clocks, temperatura, CPU de cada "
                        + "thread e em que funções do jogo o tempo vai. Começa após 5 s seguidos de jogo; durante a medição "
                        + "um aviso fica no canto. O relatório aparece em Logs de desempenho. Também dá para pedir pelo menu "
                        + "do jogo (Start > Performance).",
                new String[] {"0", "60", "600"}, new String[] {"Desligado", "60 s", "10 min"}, pending >= 600 ? 2 : pending > 0 ? 1 : 0));
        p.add(new Item("Logs de desempenho", "Os relatórios de Medir desempenho e as gravações da seção Performance.",
                () -> startActivity(new android.content.Intent(this, Odin2PerfLogsActivity.class))));

        d.add(new Item("Restaurar padrões", "Volta todas as opções desta tela aos valores padrão do Gtavport.", () ->
                new AlertDialog.Builder(this).setMessage("Restaurar as configurações padrão?")
                        .setPositiveButton("Restaurar", (x, y) -> {
                            for (String[] def : DEFAULTS) ini.remove(def[0]);
                            dirty = true;
                            save();
                            rebuild();
                        })
                        .setNegativeButton("Cancelar", null).show()));
    }

    @Override
    protected void onChanged(Item item) {
        if (item == diagItem) {  // pedido de medição: arquivo lido (e apagado) pelo jogo ao iniciar
            File req = new File(getExternalFilesDir(null), "diag_request.txt");
            try {
                if ("0".equals(item.value())) req.delete();
                else Files.write(req.toPath(), item.value().getBytes(StandardCharsets.UTF_8));
            } catch (Exception e) {
                android.widget.Toast.makeText(this, "Não foi possível salvar: " + e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
            }
            return;
        }
        String key = keys.get(item);
        if (key == null) return;
        ini.put(key, item.value());
        dirty = true;
    }

    @Override
    protected void onPause() {
        super.onPause();
        save();
    }

    private void save() {
        if (!dirty) return;
        StringBuilder out = new StringBuilder();
        boolean header = false;
        for (String l : other) {
            out.append(l).append('\n');
            header |= l.startsWith("#");
        }
        if (!header) out.insert(0, "# GTA V native renderer settings (Esc menu). name=value, applied over the command line.\n");
        for (Map.Entry<String, String> e : ini.entrySet()) out.append(e.getKey()).append('=').append(e.getValue()).append('\n');
        try {
            File f = file();
            f.getParentFile().mkdirs();
            Files.write(f.toPath(), out.toString().getBytes(StandardCharsets.UTF_8));
            dirty = false;
        } catch (Exception e) {
            android.widget.Toast.makeText(this, "Não foi possível salvar: " + e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
        }
    }
}
