import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import summarize as S  # noqa: E402

MS = 1_000_000
BASE = 5_000_000_000_000  # timestamps reais nunca são 0 (0 = slot vazio no SurfaceFlinger)


def sf_dump(stamps_ns):
    lines = ["16666666"]  # período de refresh
    for t in stamps_ns:
        lines.append(f"{t - 5 * MS}\t{t}\t{t - 2 * MS}")
    lines.append(f"0\t{S.PENDING}\t0")  # quadro pendente
    return "\n".join(lines)


class FrameStats(unittest.TestCase):
    def test_steady_30fps(self):
        stamps = [BASE + i * 33_333_333 for i in range(301)]
        st = S.frame_stats(S.parse_sf_latency(sf_dump(stamps)))
        self.assertEqual(st["frames"], 300)
        self.assertAlmostEqual(st["fps_avg"], 30.0, places=1)
        self.assertAlmostEqual(st["fps_1pct_low"], 30.0, places=1)
        self.assertEqual(st["hitches_over_50ms"], 0)

    def test_overlapping_dumps_dedup(self):
        stamps = [BASE + i * 33_333_333 for i in range(200)]
        text = "@ 1\n" + sf_dump(stamps[:128]) + "\n@ 2\n" + sf_dump(stamps[72:])
        self.assertEqual(S.parse_sf_latency(text), stamps)

    def test_one_percent_low_and_hitch(self):
        stamps, t = [], 0
        for i in range(201):
            stamps.append(t)
            t += 100 * MS if i == 100 else 33_333_333
        st = S.frame_stats(stamps)
        self.assertEqual(st["hitches_over_50ms"], 1)
        # 200 quadros -> 1 % = 2 piores: (100 + 33,33) / 2 = 66,67 ms -> 15 fps
        self.assertAlmostEqual(st["fps_1pct_low"], 15.0, places=1)
        self.assertAlmostEqual(st["ft_max_ms"], 100.0, places=1)

    def test_sampling_gap_ignored(self):
        stamps = [i * 33_333_333 for i in range(50)] + [10_000 * MS + i * 33_333_333 for i in range(50)]
        st = S.frame_stats(stamps)
        self.assertEqual(st["sampling_gaps"], 1)
        self.assertEqual(st["frames"], 98)


class Telemetry(unittest.TestCase):
    def test_parse_and_unavailable(self):
        text = """@ 1
therm cpu-0-0 45000
therm gpu 52000
cpufreq policy0 1785600
cpufreq policy7 3187200
gpu_mhz 680
gpu_busy NA
gpu_temp 52000
rss_kb 2097152
battery_dc 310
@ 2
therm cpu-0-0 60000
therm gpu 58000
cpufreq policy0 1500000
cpufreq policy7 2800000
gpu_mhz 500
gpu_busy NA
gpu_temp 58000
rss_kb 2200000
battery_dc 345
"""
        t = S.parse_telemetry(text)
        self.assertEqual(t["gpu_mhz"]["mean"], 590)
        self.assertEqual(t["soc_max_c"]["end"], 60.0)
        self.assertEqual(t["battery_c"]["start"], 31.0)
        self.assertEqual(t["cpu_policy7_mhz"]["start"], 3187)
        self.assertIn("gpu_busy_pct", t["indisponivel"])
        self.assertEqual(t["rss_mb"]["start"], 2048)


class GameLog(unittest.TestCase):
    def test_native_and_waits(self):
        log = ("[native] fps=29.5 avg=33.90ms max=48.00ms swap=1.20ms/frame dev=0x00000000 draws=900\n"
               "[native] fps=30.5 avg=32.79ms max=40.00ms swap=0.80ms/frame dev=0x00000000 draws=910\n"
               "[waits] Main (1234) waits 20.0% of the time\n"
               "[waits] Main (1234) waits 30.0% of the time\n")
        g = S.parse_game_log(log)
        self.assertEqual(g["game_fps_mean"], 30.0)
        self.assertEqual(g["game_swap_ms_mean"], 1.0)
        self.assertEqual(g["thread_wait_pct"]["Main"], 25.0)


class EndToEnd(unittest.TestCase):
    def test_missing_everything_is_marked_unavailable(self):
        with tempfile.TemporaryDirectory() as d:
            run = Path(d) / "x"
            (run / "raw").mkdir(parents=True)
            (run / "meta.txt").write_text("label=x\nbattery_start_c=30.0\n")
            s = S.summarize(run)
            self.assertIn("frametimes_surfaceflinger", s["telemetry"]["indisponivel"])
            self.assertIn("indisp.", S.table_row(s))


if __name__ == "__main__":
    unittest.main()
