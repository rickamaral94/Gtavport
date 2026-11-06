package com.gtavrecomp.app;

import android.app.Activity;
import android.content.Intent;
import android.os.Handler;
import android.os.Looper;

/**
 * Gtavport: sair do jogo pelo menu, como a aba Sair do BOTW/TOTK (Voltar do Android abre o menu do jogo;
 * "Sair do jogo" pergunta e chama daqui). O jogo roda no processo ":game" (patch 0020): o jogo recompilado
 * não tem como se desligar no meio (kernel emulado, threads do jogo), então esse processo termina e o
 * menu inicial, no processo principal, volta para a frente.
 * Chamado do nativo (menu.cpp) pela class loader da Activity.
 */
public final class Odin2Exit {
    private Odin2Exit() {}

    /** toMenu: volta ao menu inicial do app; senão fecha o app inteiro. */
    public static void quit(Activity a, boolean toMenu) {
        a.runOnUiThread(() -> {
            Intent i = new Intent(a, Odin2MenuActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
            if (!toMenu) i.putExtra(Odin2MenuActivity.EXTRA_EXIT, true);
            a.startActivity(i);
            a.finishAndRemoveTask();
            new Handler(Looper.getMainLooper()).postDelayed(() -> android.os.Process.killProcess(android.os.Process.myPid()), 300);
        });
    }
}
