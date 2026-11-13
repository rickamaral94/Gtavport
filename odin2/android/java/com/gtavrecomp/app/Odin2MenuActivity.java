package com.gtavrecomp.app;

import android.content.Intent;

/**
 * Gtavport: tela inicial do app quando o jogo já está compilado neste aparelho, no estilo do menu do
 * GTA V: Jogar, Configurações (vídeo/exibição), Driver gráfico, Logs de desempenho e, por último,
 * reinstalar/recompilar (a tela da Fase 1). Sem jogo compilado o app abre direto na tela de instalação.
 */
public class Odin2MenuActivity extends Odin2GtaActivity {
    /** Odin2Exit ("Fechar o app" no menu do jogo): fecha o app em vez de mostrar o menu. */
    static final String EXTRA_EXIT = "com.gtavport.EXIT";

    @Override
    protected void onCreate(android.os.Bundle b) {
        super.onCreate(b);
        if (getIntent().getBooleanExtra(EXTRA_EXIT, false)) finishAndRemoveTask();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (intent.getBooleanExtra(EXTRA_EXIT, false)) finishAndRemoveTask();
    }

    @Override
    protected String title() {
        return "GTAVPORT";
    }

    @Override
    protected String subtitleText() {
        return "Modo história · jogo compilado neste aparelho";
    }

    @Override
    protected String[][] hints() {
        return new String[][] {{"A", "Selecionar"}, {"B", "Sair"}};
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (getIntent().getBooleanExtra(EXTRA_EXIT, false)) return;
        Odin2DeviceBuild.invalidateInstalled();  // pode ter mudado em outro processo
        if (!Odin2DeviceBuild.installed(this)) {  // apagaram os dados / outro APK: volta à instalação
            startActivity(new Intent(this, Odin2DeviceBuildActivity.class).putExtra(Odin2DeviceBuildActivity.EXTRA_BUILD, true));
            finish();
        }
    }

    private Item logsItem;

    private java.io.File requestFile() {
        return new java.io.File(getExternalFilesDir(null), "diag_request.txt");
    }

    private String readRequest() {
        try {
            return new String(java.nio.file.Files.readAllBytes(requestFile().toPath()), java.nio.charset.StandardCharsets.UTF_8).trim();
        } catch (Exception e) {
            return "";
        }
    }

    @Override
    protected void onChanged(Item item) {
        if (item != logsItem) return;
        try {
            if ("off".equals(item.value())) requestFile().delete();
            else java.nio.file.Files.write(requestFile().toPath(),
                    ("always".equals(item.value()) ? "60 always" : "ab".equals(item.value()) ? "600 ab"
                            : "hdr".equals(item.value()) ? "600 hdr"
                            : "next600".equals(item.value()) ? "600" : "60")
                            .getBytes(java.nio.charset.StandardCharsets.UTF_8));
        } catch (Exception e) {
            android.widget.Toast.makeText(this, "Não foi possível salvar: " + e.getMessage(), android.widget.Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onRestart() {
        super.onRestart();
        rebuild();  // o pedido "no próximo jogo" some depois do relatório
    }

    @Override
    protected void buildTabs() {
        Tab m = new Tab("Jogo");
        tabs.add(m);
        // O jogo roda em outro processo (":game"): este menu fica atrás dele e volta quando se sai do jogo
        // (Voltar > Sair do jogo), sem forçar o encerramento do app.
        m.add(new Item("Jogar", "Abre o jogo com as configurações escolhidas. Para sair: Voltar (ou Start) > Sair do jogo.",
                () -> startActivity(new Intent(this, GameActivity.class))));
        m.add(new Item("Configurações", "Vídeo (resolução interna, upscaler, Snapdragon GSR, anti-aliasing, anisotrópico) e exibição.",
                () -> startActivity(new Intent(this, Odin2SettingsActivity.class))));
        m.add(new Item("Driver gráfico", "Driver do sistema (Qualcomm) ou um Turnip importado.",
                () -> startActivity(new Intent(this, Odin2DriverActivity.class))));
        // Grava os primeiros 60 s de JOGO (sem menus, loading nem vídeos) e salva em logs/perf-diag-<data>.txt, a
        // mesma pasta do gtav.log (odin2_diag.cpp). O pedido é o arquivo diag_request.txt, lido no início do jogo.
        String cur = readRequest();
        int idx = cur.endsWith("always") ? 3 : cur.endsWith("ab") ? 4 : cur.endsWith("hdr") ? 5 : cur.startsWith("600") ? 2
                : cur.isEmpty() || cur.startsWith("0") ? 0 : 1;
        logsItem = m.add(new Item("Logs de desempenho",
                "Grava os primeiros 60 s (ou 10 min) de jogo (só jogo: sem menus, loading nem vídeos; começa após 5 s jogando) e salva "
                        + "o relatório na pasta logs, junto do gtav.log: perf-diag-<data>.txt. Traz fps, travadas, GPU, clocks, "
                        + "temperatura, CPU de cada thread e as funções do jogo onde o tempo vai. Um aviso no canto mostra a gravação. "
                        + "Comparar opções: a cada 10 s troca uma opção gráfica (luzes a 2x2, teste de profundidade antigo, tudo a 2x2) "
                        + "e compara: a imagem pode mudar um pouco durante a gravação. Isolar o passe HDR: lista os shaders que "
                        + "desenham a cena e desliga um por vez por 10 s, para achar o que trava a GPU (partes da imagem somem "
                        + "durante a gravação; fique parado numa cena que trava).",
                new String[] {"off", "next", "next600", "always", "ab", "hdr"},
                new String[] {"Desligado", "Próximo jogo (60 s)", "Próximo jogo (10 min)", "Em todo início (60 s)",
                        "Comparar opções (A/B, 10 min)", "Isolar o passe HDR (10 min)"}, idx));
        m.add(new Item("Ver logs gravados", "Os relatórios e as gravações, com exportação para Downloads.",
                () -> startActivity(new Intent(this, Odin2PerfLogsActivity.class))));
        m.add(new Item("Reinstalar / recompilar", "Volta à tela de instalação: trocar os discos ou o Title Update, ou compilar o jogo de novo.",
                () -> startActivity(new Intent(this, Odin2DeviceBuildActivity.class).putExtra(Odin2DeviceBuildActivity.EXTRA_BUILD, true))));
    }
}
