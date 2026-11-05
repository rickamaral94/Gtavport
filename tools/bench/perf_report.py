#!/usr/bin/env python3
"""Relatório das gravações da seção Performance do jogo (logs/perf/perf_*.csv).

    python3 tools/bench/perf_report.py perf_A.csv [perf_B.csv ...]      # tabela comparativa
    python3 tools/bench/perf_report.py --json perf_A.csv

Mesmas regras de odin2/perf/odin2_perf.cpp (manter iguais):
  cap      intervalo dentro de ±5 % do alvo do limite de fps (cap_ms > 0)
  gpu      tempo de GPU >= 85 % do intervalo, ou espera pela fence >= 25 % do intervalo
  render   gravação de comandos (CPU da thread de render) >= 75 % do intervalo
  jogo     thread de render esperando o jogo >= 40 % do intervalo
  misto    nenhum dominante
O alvo do cap vem do resumo .txt ao lado (linha "opções: fps_limit=N"); sem ele, cap não é usado.
"""
from __future__ import annotations

import csv
import json
import math
import re
import sys
from pathlib import Path

CLASSES = ["cap", "gpu", "render", "jogo", "misto"]


def classify(r: dict, cap_ms: float) -> str:
    t = max(1e-3, r["interval_ms"])
    if cap_ms > 0 and abs(t - cap_ms) <= 0.05 * cap_ms:
        return "cap"
    if (r["gpu_ms"] >= 0 and r["gpu_ms"] >= 0.85 * t) or r["gpu_wait_ms"] >= 0.25 * t:
        return "gpu"
    if r["record_ms"] >= 0.75 * t:
        return "render"
    if r["idle_ms"] >= 0.40 * t:
        return "jogo"
    return "misto"


def cap_from_summary(csv_path: Path) -> float:
    txt = csv_path.with_suffix(".txt")
    if txt.exists():
        m = re.search(r"fps_limit=(\d+)", txt.read_text(errors="replace"))
        if m and int(m.group(1)) > 0:
            return 1000.0 / int(m.group(1))
    return 0.0


def load(path: Path) -> list[dict]:
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            try:
                rows.append({k: (float(v) if k != "class" else v) for k, v in r.items()})
            except (TypeError, ValueError):
                continue
    return rows


def pct(sorted_vals: list[float], p: float) -> float:
    k = (len(sorted_vals) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def analyze(rows: list[dict], cap_ms: float = 0.0) -> dict:
    v = [r for r in rows if 0 < r["interval_ms"] <= 1000]
    if not v:
        return {"frames": 0}
    ft = sorted(r["interval_ms"] for r in v)
    n_low = max(1, len(ft) // 100)
    gpu = [r["gpu_ms"] for r in v if r["gpu_ms"] >= 0]
    counts = {c: 0 for c in CLASSES}
    for r in v:
        counts[classify(r, cap_ms)] += 1
    mhz = [r["gpu_mhz"] for r in v if r.get("gpu_mhz", -1) > 0]
    head = [r["thermal_headroom"] for r in v if r.get("thermal_headroom", -1) >= 0]
    hitches = [r for r in v if r["interval_ms"] > 50]
    out = {
        "frames": len(v),
        "fps_avg": round(1000.0 * len(ft) / sum(ft), 2),
        "fps_1pct_low": round(1000.0 / (sum(ft[-n_low:]) / n_low), 2),
        "ft_p50_ms": round(pct(ft, 50), 2),
        "ft_p99_ms": round(pct(ft, 99), 2),
        "hitches_50ms": len(hitches),
        "hitches_shader": sum(1 for r in hitches if r["new_pipelines"] > 0 or r["miss_ms"] > 5),
        "record_ms": round(sum(r["record_ms"] for r in v) / len(v), 2),
        "gpu_ms": round(sum(gpu) / len(gpu), 2) if gpu else None,
        "gpu_wait_ms": round(sum(r["gpu_wait_ms"] for r in v) / len(v), 2),
        "idle_ms": round(sum(r["idle_ms"] for r in v) / len(v), 2),
        "bottleneck_pct": {c: round(100.0 * counts[c] / len(v), 1) for c in CLASSES},
        "bottleneck": max(CLASSES, key=lambda c: counts[c]),
        "gpu_mhz_start": mhz[0] if mhz else None,
        "gpu_mhz_min": min(mhz) if mhz else None,
        "gpu_mhz_max": max(mhz) if mhz else None,
        "thermal_headroom_max": max(head) if head else None,
    }
    out["throttling"] = bool(mhz and min(mhz) < 0.7 * max(mhz)) or bool(head and max(head) >= 0.85)
    return out


def table(results: list[tuple[str, dict]]) -> str:
    cols = ["fps_avg", "fps_1pct_low", "ft_p99_ms", "record_ms", "gpu_ms", "gpu_wait_ms", "idle_ms", "bottleneck",
            "hitches_50ms", "gpu_mhz_min", "throttling"]
    head = "| gravação | " + " | ".join(cols) + " |"
    lines = [head, "|" + "---|" * (len(cols) + 1)]
    for name, r in results:
        cells = []
        for c in cols:
            val = r.get(c)
            cells.append("indisp." if val is None else str(val))
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        return 1
    results = []
    for a in args:
        p = Path(a)
        results.append((p.stem, analyze(load(p), cap_from_summary(p))))
    if "--json" in argv:
        print(json.dumps(dict(results), indent=2, ensure_ascii=False))
    else:
        print(table(results))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
