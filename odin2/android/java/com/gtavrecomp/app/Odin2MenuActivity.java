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
        m.add(new Item("Logs de desempenho", "Sessões gravadas na seção Performance do jogo: gargalos, FPS, temperatura.",
                () -> startActivity(new Intent(this, Odin2PerfLogsActivity.class))));
        m.add(new Item("Reinstalar / recompilar", "Volta à tela de instalação: trocar os discos ou o Title Update, ou compilar o jogo de novo.",
                () -> startActivity(new Intent(this, Odin2DeviceBuildActivity.class).putExtra(Odin2DeviceBuildActivity.EXTRA_BUILD, true))));
    }
}
