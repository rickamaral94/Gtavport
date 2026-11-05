import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import perf_report as P  # noqa: E402

HEADER = ("seq,t_ms,interval_ms,record_ms,gpu_ms,gpu_wait_ms,idle_ms,acquire_ms,submit_ms,draws,new_pipelines,textures,"
          "miss_ms,class,gpu_mhz,gpu_busy,gpu_temp_c,cpu_prime_mhz,cpu_perf_mhz,cpu_eff_mhz,thermal_headroom,"
          "thermal_status,rss_mb")


def row(seq, interval, record=5, gpu=10, wait=0, idle=0, newp=0, miss=0, mhz=680, head=0.3):
    return {"seq": seq, "interval_ms": interval, "record_ms": record, "gpu_ms": gpu, "gpu_wait_ms": wait, "idle_ms": idle,
            "new_pipelines": newp, "miss_ms": miss, "gpu_mhz": mhz, "thermal_headroom": head}


class Classify(unittest.TestCase):
    def test_rules(self):
        self.assertEqual(P.classify(row(1, 33.3), 33.33), "cap")
        self.assertEqual(P.classify(row(1, 40, gpu=36), 0), "gpu")
        self.assertEqual(P.classify(row(1, 40, gpu=20, wait=12), 0), "gpu")
        self.assertEqual(P.classify(row(1, 40, record=32), 0), "render")
        self.assertEqual(P.classify(row(1, 40, idle=20), 0), "jogo")
        self.assertEqual(P.classify(row(1, 40), 0), "misto")
        # GPU indisponível (-1) não conta como GPU-bound
        self.assertEqual(P.classify(row(1, 40, gpu=-1), 0), "misto")


class Analyze(unittest.TestCase):
    def test_gpu_bound_with_throttling_and_shader_hitch(self):
        rows = [row(i, 45, gpu=40, mhz=680 if i < 50 else 350, head=0.5 if i < 50 else 0.9) for i in range(100)]
        rows[10] = row(10, 120, gpu=40, newp=3, mhz=680)
        a = P.analyze(rows)
        self.assertEqual(a["bottleneck"], "gpu")
        self.assertTrue(a["throttling"])
        self.assertEqual(a["hitches_50ms"], 1)
        self.assertEqual(a["hitches_shader"], 1)
        self.assertEqual(a["gpu_mhz_min"], 350)

    def test_capped_30(self):
        rows = [row(i, 33.33, gpu=15) for i in range(300)]
        a = P.analyze(rows, 1000 / 30)
        self.assertEqual(a["bottleneck"], "cap")
        self.assertAlmostEqual(a["fps_avg"], 30.0, places=1)
        self.assertFalse(a["throttling"])

    def test_csv_roundtrip_and_cap_from_summary(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "perf_x.csv"
            lines = [HEADER] + [f"{i},{i*33.3},33.33,8,14,0.1,2,0.2,0.3,900,0,1,0.0,limite de fps/vsync,680,70,55,3000,2800,1800,0.40,1,2100"
                                for i in range(1, 61)]
            p.write_text("\n".join(lines) + "\n")
            (Path(d) / "perf_x.txt").write_text("opções: fps_limit=30 vsync=true\n")
            self.assertAlmostEqual(P.cap_from_summary(p), 1000 / 30)
            a = P.analyze(P.load(p), P.cap_from_summary(p))
            self.assertEqual(a["frames"], 60)
            self.assertEqual(a["bottleneck"], "cap")
            self.assertIn("| perf_x |", P.table([("perf_x", a)]))


if __name__ == "__main__":
    unittest.main()
