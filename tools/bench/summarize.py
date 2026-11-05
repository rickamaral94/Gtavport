#!/usr/bin/env python3
"""Resume uma execução de tools/bench/run_scene.sh.

    python3 tools/bench/summarize.py bench/results/LABEL [--json]

Lê raw/sf_latency.txt (quadros apresentados pelo SurfaceFlinger), raw/telemetry.txt, raw/gtav.log
(linhas [native] fps=, [waits], tempos de GPU por passe) e meta.txt. Escreve summary.json e
summary.md em bench/results/LABEL e imprime a linha da tabela de docs/baseline.md.

Definições (fixas para todo o projeto):
- fps médio = quadros / tempo entre o primeiro e o último quadro apresentado.
- 1 % low = 1000 / média dos 1 % maiores frametimes (mínimo de 1 quadro).
- p50/p99 = percentis de frametime (interpolação linear).
- Telemetria que não pôde ser lida aparece em "indisponivel", nunca como zero.
"""
from __future__ import annotations

import json
import math
import re
import sys
from pathlib import Path

PENDING = (1 << 63) - 1  # o SurfaceFlinger usa INT64_MAX para quadros ainda não apresentados


def parse_sf_latency(text: str) -> list[int]:
    """Timestamps (ns) de apresentação real, sem repetição entre dumps sobrepostos."""
    seen: set[int] = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        try:
            actual = int(parts[1])
        except ValueError:
            continue
        if actual <= 0 or actual >= PENDING:
            continue
        seen.add(actual)
    return sorted(seen)


def percentile(sorted_vals: list[float], p: float) -> float:
    if not sorted_vals:
        return math.nan
    k = (len(sorted_vals) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def frame_stats(present_ns: list[int], gap_limit_ms: float = 1000.0) -> dict:
    """Estatísticas de frametime. Intervalos > gap_limit_ms são buracos de amostragem (o
    SurfaceFlinger só guarda 128 quadros) e não contam como quadros."""
    ft = []
    gaps = 0
    for a, b in zip(present_ns, present_ns[1:]):
        ms = (b - a) / 1e6
        if ms > gap_limit_ms:
            gaps += 1
            continue
        ft.append(ms)
    if not ft:
        return {"frames": 0}
    s = sorted(ft)
    n_low = max(1, len(s) // 100)
    worst = s[-n_low:]
    total_ms = sum(ft)
    return {
        "frames": len(ft),
        "fps_avg": round(len(ft) * 1000.0 / total_ms, 2),
        "fps_1pct_low": round(1000.0 / (sum(worst) / len(worst)), 2),
        "ft_p50_ms": round(percentile(s, 50), 2),
        "ft_p99_ms": round(percentile(s, 99), 2),
        "ft_max_ms": round(s[-1], 2),
        "hitches_over_50ms": sum(1 for x in ft if x > 50.0),
        "sampling_gaps": gaps,
    }


def parse_telemetry(text: str) -> dict:
    samples: list[dict] = []
    cur: dict | None = None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("@ "):
            cur = {"therm": {}, "cpufreq": {}}
            samples.append(cur)
            continue
        if cur is None or not line:
            continue
        parts = line.split()
        key = parts[0]
        if key in ("therm", "cpufreq") and len(parts) >= 3:
            cur[key][parts[1]] = parts[2]
        elif len(parts) >= 2:
            cur[key] = parts[1]

    def nums(get):
        out = []
        for s in samples:
            v = get(s)
            try:
                out.append(float(v))
            except (TypeError, ValueError):
                pass
        return out

    res: dict = {"samples": len(samples)}
    unavailable: list[str] = []

    def series(name, get, scale=1.0, nd=1):
        vals = nums(get)
        if not vals:
            unavailable.append(name)
            return
        vals = [v * scale for v in vals]
        res[name] = {"start": round(vals[0], nd), "end": round(vals[-1], nd),
                     "mean": round(sum(vals) / len(vals), nd), "max": round(max(vals), nd)}

    series("gpu_mhz", lambda s: s.get("gpu_mhz"), nd=0)
    series("gpu_busy_pct", lambda s: s.get("gpu_busy"), nd=0)
    series("gpu_temp_c", lambda s: s.get("gpu_temp"), 1 / 1000)
    series("battery_c", lambda s: s.get("battery_dc"), 1 / 10)
    series("rss_mb", lambda s: s.get("rss_kb"), 1 / 1024, 0)
    policies = sorted({p for s in samples for p in s["cpufreq"]})
    for p in policies:
        series(f"cpu_{p}_mhz", lambda s, p=p: s["cpufreq"].get(p), 1 / 1000, 0)
    if not policies:
        unavailable.append("cpufreq")
    # Zona térmica mais quente por amostra (o nome das zonas muda por aparelho).
    hottest = []
    for s in samples:
        temps = []
        for v in s["therm"].values():
            try:
                t = float(v)
            except ValueError:
                continue
            if t > 1000:
                t /= 1000.0
            if 0 < t < 150:
                temps.append(t)
        if temps:
            hottest.append(max(temps))
    if hottest:
        res["soc_max_c"] = {"start": round(hottest[0], 1), "end": round(hottest[-1], 1),
                            "max": round(max(hottest), 1)}
    else:
        unavailable.append("thermal_zones")
    res["indisponivel"] = unavailable
    return res


FPS_RE = re.compile(r"\[native\] fps=([\d.]+) avg=([\d.]+)ms max=([\d.]+)ms swap=([\d.]+)ms")
WAITS_RE = re.compile(r"\[waits\] (.+?) \((\d+)\) waits ([\d.]+)% of the time")


def parse_game_log(text: str) -> dict:
    fps = [tuple(map(float, m.groups())) for m in FPS_RE.finditer(text)]
    waits: dict[str, list[float]] = {}
    for m in WAITS_RE.finditer(text):
        waits.setdefault(m.group(1), []).append(float(m.group(3)))
    res: dict = {}
    if fps:
        res["game_fps_mean"] = round(sum(f[0] for f in fps) / len(fps), 2)
        res["game_swap_ms_mean"] = round(sum(f[3] for f in fps) / len(fps), 2)
        res["game_fps_windows"] = len(fps)
    if waits:
        res["thread_wait_pct"] = {k: round(sum(v) / len(v), 1) for k, v in waits.items()}
    pass_lines = [l for l in text.splitlines() if "pass" in l and "GPU" in l and "ms" in l]
    if pass_lines:
        res["gpu_pass_times_last"] = pass_lines[-1].strip()[-300:]
    return res


def read_meta(path: Path) -> dict:
    meta: dict = {}
    if path.exists():
        for line in path.read_text(errors="replace").splitlines():
            if "=" in line:
                k, v = line.split("=", 1)
                meta.setdefault(k, v)
    return meta


def summarize(run_dir: Path) -> dict:
    raw = run_dir / "raw"
    meta = read_meta(run_dir / "meta.txt")
    out: dict = {"label": meta.get("label", run_dir.name), "meta": meta}
    sf = raw / "sf_latency.txt"
    out["frames"] = frame_stats(parse_sf_latency(sf.read_text())) if sf.exists() else {"frames": 0}
    tel = raw / "telemetry.txt"
    out["telemetry"] = parse_telemetry(tel.read_text()) if tel.exists() else {"indisponivel": ["telemetry"]}
    log = raw / "gtav.log"
    out["game"] = parse_game_log(log.read_text(errors="replace")) if log.exists() else {}
    if out["frames"].get("frames", 0) == 0:
        out["telemetry"].setdefault("indisponivel", []).append("frametimes_surfaceflinger")
    return out


def table_row(s: dict) -> str:
    f, t, m = s["frames"], s["telemetry"], s["meta"]
    def g(d, k, sub=None):
        v = d.get(k)
        if v is None:
            return "indisp."
        return str(v[sub]) if sub else str(v)
    temp = f"{m.get('battery_start_c', '?')}/{m.get('battery_end_c', '?')}"
    return (f"| {s['label']} | {g(f, 'fps_avg')} | {g(f, 'fps_1pct_low')} | {g(f, 'ft_p99_ms')} | "
            f"{temp} | {g(t, 'gpu_mhz', 'mean')} | {g(t, 'rss_mb', 'max')} | "
            f"{'crash' if 'crash' in m else ''} {', '.join(t.get('indisponivel', []))} |")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 1
    run_dir = Path(argv[1])
    s = summarize(run_dir)
    (run_dir / "summary.json").write_text(json.dumps(s, indent=2, ensure_ascii=False))
    row = table_row(s)
    (run_dir / "summary.md").write_text(row + "\n")
    print(json.dumps(s, indent=2, ensure_ascii=False) if "--json" in argv else row)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
