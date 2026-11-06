package com.gtavrecomp.app;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

/**
 * Gtavport: base das telas do app no estilo do menu de pausa do GTA V (visual próprio, sem arte nem
 * marca da Rockstar): fundo escuro, título grande em itálico, abas no topo (LB/RB), lista com o item
 * selecionado em branco e o valor entre setas, caixa de descrição embaixo e a barra de botões no canto.
 *
 * Controle: direcional/analógico sobe e desce e troca o valor, A confirma, B volta, LB/RB trocam de aba.
 * Toque: tocar num item o seleciona; tocar de novo confirma (ou passa para o próximo valor); as setas
 * trocam o valor.
 */
public abstract class Odin2GtaActivity extends Activity {
    static final int ACCENT = Color.rgb(93, 173, 226);  // azul do menu
    static final int ROW = Color.argb(150, 0, 0, 0), ROW_SELECTED = Color.WHITE;
    static final Typeface BOLD = Typeface.create("sans-serif-condensed", Typeface.BOLD);
    static final Typeface BOLD_ITALIC = Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC);
    static final Typeface REGULAR = Typeface.create("sans-serif-condensed", Typeface.NORMAL);

    /** Uma linha da lista: uma ação (values == null) ou uma opção com valores. */
    static class Item {
        final String label, help;
        final String[] values, names;
        int index;
        Runnable action;
        boolean enabled = true;

        Item(String label, String help, Runnable action) {
            this.label = label;
            this.help = help;
            this.values = null;
            this.names = null;
            this.action = action;
        }

        Item(String label, String help, String[] values, String[] names, int index) {
            this.label = label;
            this.help = help;
            this.values = values;
            this.names = names;
            this.index = Math.max(0, Math.min(index, values.length - 1));
        }

        String value() {
            return values == null ? null : values[index];
        }
    }

    static class Tab {
        final String title;
        final List<Item> items = new ArrayList<>();

        Tab(String title) {
            this.title = title;
        }

        Item add(Item i) {
            items.add(i);
            return i;
        }
    }

    protected final List<Tab> tabs = new ArrayList<>();
    protected int tab, sel;
    private LinearLayout tabBar, list;
    private TextView help, subtitle;
    private ScrollView scroll;

    /** Título grande do topo. */
    protected abstract String title();

    /** Linha menor abaixo do título (estado, versão...). */
    protected String subtitleText() {
        return "";
    }

    /** Monta as abas e itens (chamado no onCreate e em rebuild()). */
    protected abstract void buildTabs();

    /** Um valor mudou (opções). */
    protected void onChanged(Item item) {}

    /** B / voltar. */
    protected void onBackAction() {
        finish();
    }

    /** Barra de botões do canto (pares tecla, texto). */
    protected String[][] hints() {
        return new String[][] {{"A", "Selecionar"}, {"B", "Voltar"}};
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        FrameLayout root = new FrameLayout(this);
        GradientDrawable bg = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[] {Color.rgb(8, 12, 18), Color.rgb(18, 30, 44), Color.rgb(6, 8, 12)});
        root.setBackground(bg);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(48), dp(24), dp(48), dp(52));  // embaixo: a barra de botões
        root.addView(col, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        TextView t = text(title(), 44, Color.WHITE, BOLD_ITALIC);
        t.setShadowLayer(dp(3), dp(2), dp(2), Color.BLACK);
        t.setLetterSpacing(0.02f);
        col.addView(t);
        subtitle = text("", 14, Color.rgb(190, 200, 210), REGULAR);
        col.addView(subtitle);

        tabBar = new LinearLayout(this);
        tabBar.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams tbl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(36));
        tbl.topMargin = dp(14);
        col.addView(tabBar, tbl);
        View accent = new View(this);
        accent.setBackgroundColor(ACCENT);
        col.addView(accent, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(3)));

        LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams bl = new LinearLayout.LayoutParams(dp(620), 0, 1f);
        bl.topMargin = dp(8);
        col.addView(body, bl);
        scroll = new ScrollView(this);
        scroll.setVerticalScrollBarEnabled(false);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll.addView(list);
        body.addView(scroll, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        help = text("", 15, Color.WHITE, REGULAR);
        help.setBackgroundColor(Color.argb(190, 0, 0, 0));
        help.setPadding(dp(14), dp(10), dp(14), dp(10));
        help.setMinLines(2);
        LinearLayout.LayoutParams hl = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        hl.topMargin = dp(6);
        body.addView(help, hl);

        LinearLayout hintBar = new LinearLayout(this);
        hintBar.setOrientation(LinearLayout.HORIZONTAL);
        hintBar.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        for (String[] h : hints()) {
            TextView key = text(h[0], 13, Color.BLACK, BOLD);
            key.setGravity(Gravity.CENTER);
            GradientDrawable pill = new GradientDrawable();
            pill.setColor(Color.WHITE);
            pill.setCornerRadius(dp(11));
            key.setBackground(pill);
            key.setMinWidth(dp(22));
            key.setPadding(dp(6), 0, dp(6), 0);
            hintBar.addView(key, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, dp(22)));
            TextView label = text(h[1], 14, Color.WHITE, BOLD);
            label.setPadding(dp(6), 0, dp(18), 0);
            hintBar.addView(label);
        }
        FrameLayout.LayoutParams hp = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.BOTTOM | Gravity.END);
        hp.setMargins(0, 0, dp(32), dp(16));
        hintBar.setPadding(dp(10), dp(6), dp(0), dp(6));
        hintBar.setBackgroundColor(Color.argb(150, 0, 0, 0));
        root.addView(hintBar, hp);

        setContentView(root);
        rebuild();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    /** Refaz abas e itens (estado mudou). */
    protected void rebuild() {
        tabs.clear();
        buildTabs();
        if (tab >= tabs.size()) tab = 0;
        render();
    }

    protected void render() {
        subtitle.setText(subtitleText());
        tabBar.removeAllViews();
        for (int i = 0; i < tabs.size(); i++) {
            final int idx = i;
            boolean on = i == tab;
            TextView tv = text(tabs.get(i).title.toUpperCase(), 16, on ? Color.BLACK : Color.WHITE, BOLD);
            tv.setGravity(Gravity.CENTER);
            tv.setBackgroundColor(on ? Color.WHITE : Color.argb(150, 0, 0, 0));
            tv.setPadding(dp(22), 0, dp(22), 0);
            tv.setOnClickListener(v -> switchTab(idx));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.MATCH_PARENT);
            lp.rightMargin = dp(3);
            tabBar.addView(tv, lp);
        }
        list.removeAllViews();
        List<Item> items = tabs.isEmpty() ? new ArrayList<>() : tabs.get(tab).items;
        if (sel >= items.size()) sel = Math.max(0, items.size() - 1);
        for (int i = 0; i < items.size(); i++) list.addView(row(items.get(i), i, i == sel));
        help.setText(items.isEmpty() ? "" : items.get(sel).help);
        final int s = sel;
        list.post(() -> {
            if (s < list.getChildCount()) {
                View v = list.getChildAt(s);
                int top = v.getTop(), bottom = v.getBottom();
                if (top < scroll.getScrollY()) scroll.smoothScrollTo(0, top);
                else if (bottom > scroll.getScrollY() + scroll.getHeight()) scroll.smoothScrollTo(0, bottom - scroll.getHeight());
            }
        });
    }

    private View row(Item it, int i, boolean selected) {
        int fg = !it.enabled ? Color.GRAY : selected ? Color.BLACK : Color.WHITE;
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        r.setBackgroundColor(selected ? ROW_SELECTED : ROW);
        r.setPadding(dp(14), 0, dp(10), 0);
        TextView l = text(it.label, 17, fg, BOLD);
        r.addView(l, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        if (it.values != null) {
            if (selected) {
                TextView left = arrow("‹", fg);
                left.setOnClickListener(v -> change(-1));
                r.addView(left);
            }
            TextView val = text(it.names[it.index], 17, fg, BOLD);
            val.setPadding(dp(6), 0, dp(6), 0);
            r.addView(val);
            if (selected) {
                TextView right = arrow("›", fg);
                right.setOnClickListener(v -> change(+1));
                r.addView(right);
            }
        }
        r.setOnClickListener(v -> {
            if (sel != i) {
                sel = i;
                render();
            } else {
                activate();
            }
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(42));
        lp.bottomMargin = dp(2);
        r.setLayoutParams(lp);
        return r;
    }

    private TextView arrow(String s, int color) {
        TextView a = text(s, 26, color, BOLD);
        a.setGravity(Gravity.CENTER);
        a.setPadding(dp(10), 0, dp(10), dp(3));
        return a;
    }

    private Item current() {
        if (tabs.isEmpty()) return null;
        List<Item> items = tabs.get(tab).items;
        return sel < items.size() ? items.get(sel) : null;
    }

    private void switchTab(int t) {
        if (t == tab || t < 0 || t >= tabs.size()) return;
        tab = t;
        sel = 0;
        render();
    }

    private void move(int d) {
        if (tabs.isEmpty()) return;
        int n = tabs.get(tab).items.size();
        if (n == 0) return;
        sel = (sel + d + n) % n;
        render();
    }

    private void change(int d) {
        Item it = current();
        if (it == null || it.values == null || !it.enabled) return;
        it.index = (it.index + d + it.values.length) % it.values.length;
        onChanged(it);
        render();
    }

    private void activate() {
        Item it = current();
        if (it == null || !it.enabled) return;
        if (it.values != null) change(+1);
        else if (it.action != null) it.action.run();
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: move(-1); return true;
            case KeyEvent.KEYCODE_DPAD_DOWN: move(+1); return true;
            case KeyEvent.KEYCODE_DPAD_LEFT: change(-1); return true;
            case KeyEvent.KEYCODE_DPAD_RIGHT: change(+1); return true;
            case KeyEvent.KEYCODE_BUTTON_L1: switchTab(tab - 1); return true;
            case KeyEvent.KEYCODE_BUTTON_R1: switchTab(tab + 1); return true;
            case KeyEvent.KEYCODE_BUTTON_A:
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_ENTER:
                activate();
                return true;
            case KeyEvent.KEYCODE_BUTTON_B:
            case KeyEvent.KEYCODE_BACK:
                onBackAction();
                return true;
            default:
                return super.onKeyDown(code, e);
        }
    }

    protected int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    protected TextView text(String s, int sp, int color, Typeface tf) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setTypeface(tf);
        return t;
    }
}
