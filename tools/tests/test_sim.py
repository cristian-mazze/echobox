import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
import pathlib
from unittest.mock import patch

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import sim


class TestVenvPython(unittest.TestCase):
    def test_returns_path_if_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            venv_bin = Path(tmpdir) / ".venv" / "bin" / "python"
            venv_bin.parent.mkdir(parents=True)
            venv_bin.touch()

            found = sim.venv_python()
            if found:
                self.assertTrue("python" in str(found).lower() or "python3" in str(found).lower())

    def test_returns_windows_path_if_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            venv_scripts = Path(tmpdir) / ".venv" / "Scripts" / "python.exe"
            venv_scripts.parent.mkdir(parents=True)
            venv_scripts.touch()

            with patch.object(sim, "repo_root", return_value=Path(tmpdir)):
                found = sim.venv_python()
                self.assertIsNotNone(found)
                self.assertIn("python", str(found).lower())

    def test_returns_none_when_no_venv(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            with patch.object(sim, "repo_root", return_value=Path(tmpdir)):
                found = sim.venv_python()
                self.assertIsNone(found)


class TestRepoRoot(unittest.TestCase):
    def test_returns_existing_directory(self):
        root = sim.repo_root()
        self.assertTrue(root.exists())
        self.assertTrue((root / "CMakeLists.txt").exists())


class TestLoadConfig(unittest.TestCase):
    def test_returns_defaults_when_no_config(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            with patch.object(sim, "repo_root", return_value=tmp_path):
                config = sim.load_config()
                self.assertIsNone(config["music_dir"])
                self.assertIsNone(config["system_dir"])
                self.assertEqual(config["scale"], 3)
                self.assertEqual(config["battery_percent"], 80)
                self.assertFalse(config["battery_charging"])

    def test_reads_json_config(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            config_path = tmp_path / "sim.local.json"
            config_path.write_text(json.dumps({
                "music_dir": "/test/music",
                "system_dir": "/test/system",
                "scale": 2,
                "battery_percent": 75,
                "battery_charging": True,
            }))

            with patch.object(sim, "repo_root", return_value=tmp_path):
                config = sim.load_config()
                self.assertEqual(config["music_dir"], "/test/music")
                self.assertEqual(config["system_dir"], "/test/system")
                self.assertEqual(config["scale"], 2)
                self.assertEqual(config["battery_percent"], 75)
                self.assertTrue(config["battery_charging"])

    def test_handles_invalid_json(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            config_path = tmp_path / "sim.local.json"
            config_path.write_text("not valid json")

            with patch.object(sim, "repo_root", return_value=tmp_path):
                config = sim.load_config()
                self.assertIsNone(config["music_dir"])


class TestSimulatorArguments(unittest.TestCase):
    def test_includes_music_dir(self):
        config = {
            "music_dir": "/path/to/music",
            "system_dir": "/path/to/system",
            "scale": 1,
            "battery_percent": 100,
            "battery_charging": False,
        }
        args = sim.build_simulator_args(config)
        self.assertIn("--card", args)
        self.assertIn("/path/to/music", args)

    def test_includes_system_dir(self):
        config = {
            "music_dir": None,
            "system_dir": "/path/to/system",
            "scale": 1,
            "battery_percent": 100,
            "battery_charging": False,
        }
        args = sim.build_simulator_args(config)
        self.assertIn("--system-dir", args)
        self.assertIn("/path/to/system", args)

    def test_includes_scale(self):
        config = {
            "music_dir": None,
            "system_dir": None,
            "scale": 2,
            "battery_percent": 80,
            "battery_charging": False,
        }
        args = sim.build_simulator_args(config)
        self.assertIn("--scale", args)
        self.assertIn("2", args)

    def test_includes_battery(self):
        config = {
            "music_dir": None,
            "system_dir": None,
            "scale": 3,
            "battery_percent": 75,
            "battery_charging": False,
        }
        args = sim.build_simulator_args(config)
        self.assertIn("--battery", args)
        self.assertIn("75", args)

    def test_includes_charging(self):
        config = {
            "music_dir": None,
            "system_dir": None,
            "scale": 3,
            "battery_percent": 80,
            "battery_charging": True,
        }
        args = sim.build_simulator_args(config)
        self.assertIn("--charging", args)

    def test_passes_extra_arguments(self):
        config = {
            "music_dir": None,
            "system_dir": None,
            "scale": 1,
            "battery_percent": 100,
            "battery_charging": False,
        }
        extra = ["--headless", "--keys", "down,play"]
        args = sim.build_simulator_args(config, extra)
        self.assertIn("--headless", args)
        self.assertIn("--keys", args)
        self.assertIn("down,play", args)


class TestParser(unittest.TestCase):
    def test_parse_build(self):
        args = sim.parse_arguments(["build"])
        self.assertEqual(args.command, "build")

    def test_parse_run(self):
        args = sim.parse_arguments(["run", "--headless", "--frames", "50"])
        self.assertEqual(args.command, "run")
        self.assertTrue(args.headless)
        self.assertEqual(args.frames, 50)

    def test_parse_watch(self):
        args = sim.parse_arguments(["watch"])
        self.assertEqual(args.command, "watch")

    def test_parse_test_no_filter(self):
        args = sim.parse_arguments(["test"])
        self.assertEqual(args.command, "test")
        self.assertEqual(args.filter, "")

    def test_parse_test_with_filter(self):
        args = sim.parse_arguments(["test", "sim_options"])
        self.assertEqual(args.command, "test")
        self.assertEqual(args.filter, "sim_options")

    def test_parse_shot(self):
        args = sim.parse_arguments(["shot", "down,play", "test.bmp", "--frames", "100"])
        self.assertEqual(args.command, "shot")
        self.assertEqual(args.keys, "down,play")
        self.assertEqual(str(args.screenshot), "test.bmp")
        self.assertEqual(args.frames, 100)

    def test_parse_clean_dev(self):
        args = sim.parse_arguments(["clean", "--dev"])
        self.assertEqual(args.command, "clean")
        self.assertTrue(args.dev)

    def test_parse_clean_sim(self):
        args = sim.parse_arguments(["clean", "--sim"])
        self.assertEqual(args.command, "clean")
        self.assertTrue(args.sim)

    def test_parse_config(self):
        args = sim.parse_arguments(["config", "card", "/new/path"])
        self.assertEqual(args.command, "config")
        self.assertEqual(args.key, "card")
        self.assertEqual(args.value, "/new/path")

    def test_parse_debug_no_args(self):
        args = sim.parse_arguments(["debug"])
        self.assertEqual(args.command, "debug")
        self.assertEqual(args.extra, [])

    def test_parse_debug_with_extra(self):
        args = sim.parse_arguments(["debug", "some-arg", "another-arg"])
        self.assertEqual(args.command, "debug")
        self.assertEqual(args.extra, ["some-arg", "another-arg"])


class TestSimulatorPath(unittest.TestCase):
    def test_returns_none_for_unbuilt_simulator(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmppath = pathlib.Path(tmpdir)
            with patch.object(sim, "repo_root", return_value=tmppath):
                result = sim.simulator_path()
                self.assertIsNone(result)

    def test_returns_path_for_built_simulator(self):
        result = sim.simulator_path()
        if result is not None:
            self.assertTrue("echo_sim" in result)


if __name__ == "__main__":
    unittest.main()