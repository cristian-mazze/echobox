"""EchoPod simulator CLI - single script to build, run, and debug the simulator."""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time


def repo_root():
    """Return the repository root directory."""
    return pathlib.Path(__file__).resolve().parent.parent


def venv_python():
    """Return the path to the venv Python executable, or None if not found."""
    root = repo_root()
    for path in [
        root / ".venv" / "bin" / "python",
        root / ".venv" / "bin" / "python3",
        root / ".venv" / "Scripts" / "python.exe",
        root / ".venv" / "Scripts" / "python3.exe",
    ]:
        if path.exists():
            return path
    return None


def find_clang():
    """Find the clang compiler, checking system path and MSYS2."""
    if sys.platform != "win32":
        result = subprocess.run(["which", "clang"], capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout.strip()

    msys2_clang = r"C:\msys64\clang64\bin\clang.exe"
    if os.path.exists(msys2_clang):
        return msys2_clang

    homebrew_clang = "/opt/homebrew/opt/llvm/bin/clang"
    if os.path.exists(homebrew_clang):
        return homebrew_clang

    linux_clang = "/usr/bin/clang"
    if os.path.exists(linux_clang):
        return linux_clang

    return None


def tool_environment():
    """Return os.environ with clang, cmake and ninja at the front of PATH."""
    env = os.environ.copy()
    clang_path = find_clang()

    if clang_path:
        clang_bin = os.path.dirname(clang_path)
        current_path = env.get("PATH", "")
        if os.name == "nt":
            env["PATH"] = f"{clang_bin};{current_path}"
        else:
            env["PATH"] = f"{clang_bin}:{current_path}"

    venv_path = venv_python()
    if venv_path:
        venv_dir = venv_path.parent.parent
        for tool in ["cmake", "ninja"]:
            if os.name == "nt":
                tool_path = venv_dir / "Scripts" / f"{tool}.exe"
            else:
                tool_path = venv_dir / "bin" / tool
            if tool_path.exists():
                current_path = env.get("PATH", "")
                env["PATH"] = f"{tool_path}{os.pathsep}{current_path}"

    return env


def load_config():
    """Load simulation configuration from sim.local.json."""
    root = repo_root()
    config_path = root / "sim.local.json"

    config = {
        "music_dir": None,
        "system_dir": None,
        "scale": 3,
        "battery_percent": 80,
        "battery_charging": False,
    }

    if config_path.exists():
        try:
            with open(config_path, "r") as f:
                user_config = json.load(f)
            for key in config:
                if key in user_config:
                    config[key] = user_config[key]
        except (json.JSONDecodeError, IOError):
            pass

    return config


def save_config(config):
    """Save simulation configuration to sim.local.json."""
    root = repo_root()
    config_path = root / "sim.local.json"
    with open(config_path, "w") as f:
        json.dump(config, f, indent=2)


def configure_cmake():
    """Run cmake --preset dev if build directory doesn't exist."""
    root = repo_root()
    build_dir = root / "build" / "dev"

    if not build_dir.exists():
        print("Configuring build (cmake --preset dev)...")
        cmake_cmd = ["cmake", "--preset", "dev"]
        env = tool_environment()
        result = subprocess.run(cmake_cmd, cwd=root, capture_output=True, text=True, env=env)
        if result.returncode != 0:
            print("CMake configuration failed:")
            print(result.stderr)
            return False
    return True


def build_simulator():
    """Build the simulator with cmake --preset dev --target echo_sim."""
    root = repo_root()

    if not configure_cmake():
        return False

    cmake_cmd = ["cmake", "--build", "--preset", "dev", "--target", "echo_sim"]
    env = tool_environment()
    result = subprocess.run(cmake_cmd, cwd=root, capture_output=True, text=True, env=env)

    if result.returncode != 0:
        print("Build failed:")
        print(result.stdout)
        if result.stderr:
            print(result.stderr)
        return False

    print("Build succeeded.")
    return True


def simulator_path():
    """Return the path to the simulator executable."""
    root = repo_root()
    exe_path = root / "build" / "dev" / "platform" / "sim" / "echo_sim"
    exe_win_path = root / "build" / "dev" / "platform" / "sim" / "echo_sim.exe"

    if os.name == "nt":
        if exe_win_path.exists():
            return str(exe_win_path)
        elif exe_path.exists():
            return str(exe_path)
    elif exe_path.exists():
        return str(exe_path)

    return None


def build_simulator_args(config, extra=None):
    """Build the argument list for the simulator."""
    args = []

    if config.get("music_dir"):
        args.extend(["--card", str(config["music_dir"])])

    if config.get("system_dir"):
        args.extend(["--system-dir", str(config["system_dir"])])

    args.extend(["--scale", str(config.get("scale", 1))])

    if config.get("battery_percent") is not None:
        args.extend(["--battery", str(config["battery_percent"])])

    if config.get("battery_charging"):
        args.append("--charging")

    if extra:
        args.extend(extra)

    return args


def run_simulator(exe_path, simulator_args):
    """Run the simulator with the given arguments."""
    env = tool_environment()

    print("Running EchoPod simulator...")
    print(f"  Music: {load_config().get('music_dir') or 'not configured'}")
    print(f"  System: {load_config().get('system_dir') or 'not configured'}")

    proc = subprocess.Popen(
        [exe_path] + simulator_args,
        env=env,
    )
    return proc.returncode


def find_source_files():
    """Find all source files that trigger rebuild in watch mode."""
    root = repo_root()
    watched = []
    patterns = ["*.c", "*.h"]
    dirs = ["src", "platform/sim"]

    for d in dirs:
        dir_path = root / d
        if dir_path.exists():
            for pattern in patterns:
                watched.extend(dir_path.rglob(pattern))

    cmake_file = root / "CMakeLists.txt"
    if cmake_file.exists():
        watched.append(cmake_file)

    return watched


def watch_loop(config):
    """Run the watch mode - poll for changes and rebuild/restart."""
    exe_path = simulator_path()
    if exe_path is None:
        print("Simulator not built. Run 'sim build' first.")
        return 1

    source_files = find_source_files()
    if not source_files:
        print("No source files found to watch.")
        return 1

    def get_mtimes(files):
        return {str(f): f.stat().st_mtime for f in files}

    current_mtimes = get_mtimes(source_files)
    sim_process = None

    print("Watch mode active. Press Ctrl+C to stop.")
    print(f"Monitoring {len(source_files)} files...")

    try:
        while True:
            time.sleep(0.3)

            new_mtimes = get_mtimes(source_files)
            changed = new_mtimes != current_mtimes

            if changed:
                print("\nChange detected. Rebuilding and restarting...")

                if sim_process and sim_process.poll() is None:
                    sim_process.terminate()
                    try:
                        sim_process.wait(timeout=2)
                    except subprocess.TimeoutExpired:
                        sim_process.kill()

                if not build_simulator():
                    print("Build failed, continuing with current version...")
                    current_mtimes = new_mtimes
                    continue

                sim_process = subprocess.Popen(
                    [exe_path] + build_simulator_args(config),
                    env=tool_environment(),
                )
                print(f"Simulator started (PID {sim_process.pid})")

                current_mtimes = new_mtimes

    except KeyboardInterrupt:
        print("\nWatch mode stopped.")
        if sim_process and sim_process.poll() is None:
            sim_process.terminate()
        return 0


def parse_arguments(argv):
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="EchoPod simulator CLI tool",
        formatter_class=argparse.RawTextHelpFormatter,
    )

    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    subparsers.add_parser("setup", help="Set up the development environment")

    subparsers.add_parser("doctor", help="Check environment and configuration")

    subparsers.add_parser("build", help="Build the simulator")

    run_parser = subparsers.add_parser("run", help="Run the simulator")
    run_parser.add_argument("--card", type=pathlib.Path, default=None)
    run_parser.add_argument("--system-dir", type=pathlib.Path, default=None)
    run_parser.add_argument("--scale", type=int, default=None)
    run_parser.add_argument("--battery", type=int, default=None)
    run_parser.add_argument("--charging", action="store_true")
    run_parser.add_argument("--headless", action="store_true")
    run_parser.add_argument("--keys", type=str, default="")
    run_parser.add_argument("--screenshot", type=pathlib.Path, default="")
    run_parser.add_argument("--frames", type=int, default=0)
    run_parser.add_argument("extra", nargs=argparse.REMAINDER)

    watch_parser = subparsers.add_parser("watch", help="Watch and auto-rebuild")
    watch_parser.add_argument("--card", type=pathlib.Path, default=None)
    watch_parser.add_argument("--system-dir", type=pathlib.Path, default=None)
    watch_parser.add_argument("--scale", type=int, default=1)
    watch_parser.add_argument("--battery", type=int, default=100)
    watch_parser.add_argument("--charging", action="store_true")

    shot_parser = subparsers.add_parser("shot", help="Run headless test")
    shot_parser.add_argument("keys", type=str)
    shot_parser.add_argument("screenshot", type=pathlib.Path)
    shot_parser.add_argument("--frames", type=int, default=30)
    shot_parser.add_argument("--card", type=pathlib.Path, default=None)
    shot_parser.add_argument("--system-dir", type=pathlib.Path, default=None)

    test_parser = subparsers.add_parser("test", help="Run tests")
    test_parser.add_argument("filter", nargs="?", default="")

    subparsers.add_parser("debug", help="Run under lldb")

    card_parser = subparsers.add_parser("card", help="Generate test library")
    card_parser.add_argument("--tracks", type=int, default=48)
    card_parser.add_argument("--covers", action="store_true")

    config_parser = subparsers.add_parser("config", help="Set configuration")
    config_parser.add_argument("key")
    config_parser.add_argument("value")

    print_args_parser = subparsers.add_parser("print-args", help="Print simulator arguments as JSON")
    print_args_parser.add_argument("extra", nargs=argparse.REMAINDER)

    clean_parser = subparsers.add_parser("clean", help="Clean build artifacts")
    clean_parser.add_argument("--dev", action="store_true")
    clean_parser.add_argument("--sim", action="store_true")

    subparsers.add_parser("uninstall", help="Uninstall packages and tools installed by setup")

    return parser.parse_args(argv)


def cmd_setup():
    """Handle setup command."""
    root = repo_root()

    print("Setting up development environment...")

    python_path = venv_python()
    if python_path is None:
        print("Creating virtual environment...")
        subprocess.run(
            [sys.executable, "-m", "venv", ".venv"],
            cwd=root, check=True
        )

        python_path = venv_python()
        if python_path is None:
            print("Error: Failed to create venv")
            return 1

    print(f"Python: {python_path}")

    pip_path = python_path.parent / "pip" if sys.platform != "win32" else python_path.parent / "pip.exe"

    requirements = root / "tools" / "requirements.txt"
    if requirements.exists():
        print("Installing requirements...")
        subprocess.run([str(pip_path), "install", "-r", str(requirements)], cwd=root, check=True)

    print("Installing cmake and ninja...")
    subprocess.run([str(pip_path), "install", "cmake", "ninja"], cwd=root, check=True)

    add_to_path = True

    print("Installing 'sim' command...")
    sim_script = root / "tools" / "sim.py"
    bin_dir = python_path.parent

    if add_to_path:
        if sys.platform == "win32":
            sim_cmd = bin_dir / "sim.cmd"
            sim_cmd.write_text(f'@echo off\npython "{sim_script}" %*\n')
        else:
            sim_cmd = bin_dir / "sim"
            sim_cmd.write_text(f'#!/bin/sh\nexec python3 "{sim_script}" "$@"\n')
            os.chmod(sim_cmd, 0o755)
        print(f"  Installed: {sim_cmd}")

    clang = find_clang()
    if clang:
        print(f"Clang found: {clang}")
    else:
        print("Warning: Clang not found.")

    system_dir = root / "build" / "sim" / "system"
    system_dir.mkdir(parents=True, exist_ok=True)
    print(f"System dir: {system_dir}")

    print("\nUsage:")
    if add_to_path:
        if sys.platform == "win32":
            print(f"  sim <args>          # From anywhere (after PATH)")
            print(f"  PATH += {bin_dir}   # Add to Windows PATH")
        else:
            print(f"  sim <args>          # From anywhere (after PATH)")
            print(f"  export PATH=\"$PATH:{bin_dir}\"  # Add to PATH")
            print(f"  OR: source .venv/bin/activate  # Activate venv")

    print("\nOr use from the project directory:")
    print("  ./sim <args>     (Unix/Linux/macOS)")
    print("  sim.cmd <args>   (Windows)")
    print("\nExample:")
    print("  sim doctor       # Check environment")
    print("  sim build        # Build the simulator")
    print("  sim run          # Run the simulator")
    print("Setup complete!")
    return 0


def cmd_doctor():
    """Handle doctor command."""
    config = load_config()

    print("EchoPod simulator environment check")
    print("=" * 40)

    root = repo_root()
    venv_path = venv_python()
    if venv_path:
        print(f"Python: {venv_path}")
    else:
        print("Python: not found")
        print("  Run: sim setup")

    clang_path = find_clang()
    if clang_path:
        print(f"Clang: {clang_path}")
    else:
        print("Clang: not found")
        if os.name == "nt":
            print("  Install MSYS2 with mingw-w64-clang-x86_64-clang")
            print("  https://www.msys2.org/")

    cmake_result = subprocess.run(["cmake", "--version"], capture_output=True, text=True)
    if cmake_result.returncode == 0:
        cmake_version = cmake_result.stdout.split()[2] if cmake_result.stdout else "unknown"
        print(f"CMake: {cmake_version}")
    else:
        print("CMake: not found")
        print("  Run: pip install cmake (inside venv)")

    if venv_path:
        venv_dir = venv_path.parent.parent
        if sys.platform == "win32":
            ninja_path = venv_dir / "Scripts" / "ninja.exe"
        else:
            ninja_path = venv_dir / "bin" / "ninja"
        if ninja_path.exists():
            print(f"Ninja: {ninja_path}")
        else:
            print("Ninja: not found")
            print("  Run: pip install ninja (inside venv)")
    else:
        print("Ninja: not found")

    build_dir = root / "build" / "dev"
    cache_dir = root / ".cache" / "deps"

    if cache_dir.exists():
        print("SDL2: cached/bundled")
    elif build_dir.exists() and (build_dir / "platform" / "sim" / "echo_sim" in [build_dir / "platform" / "sim" / "echo_sim.exe", build_dir / "platform" / "sim" / "echo_sim"] if (build_dir / "platform" / "sim").exists() else False):
        print("SDL2: bundled (already downloaded)")
    else:
        print("SDL2: will download during build")

    music_dir = config.get("music_dir")
    if music_dir:
        print(f"Music dir: {music_dir}")
    else:
        print("Music dir: not set")
        print("  Run: sim config card D:/Music")

    system_dir = config.get("system_dir")
    if system_dir:
        print(f"System dir: {system_dir}")
    else:
        print("System dir: not set")
        print("  The system directory stores the music library database")
        print("  You can use a local path like: sim config system-dir build/sim/system")
        print("  Or use the default by not setting it (build will create it)")

    print()
    print("To build and run the simulator:")
    print("  1. sim setup           # Set up the development environment")
    print("  2. sim build           # Build the simulator")
    print("  3. sim config card <path>  # Set your music folder")
    print("  4. sim run             # Run the simulator")

    return 0


def cmd_build():
    """Handle build command."""
    if build_simulator():
        print("Build ready.")
        return 0
    return 1


def cmd_run(args):
    """Handle run command."""
    config = load_config()

    if args.card:
        config["music_dir"] = args.card
    if args.system_dir:
        config["system_dir"] = args.system_dir

    if not config.get("system_dir"):
        root = repo_root()
        system_dir = root / "build" / "sim" / "system"
        system_dir.mkdir(parents=True, exist_ok=True)
        config["system_dir"] = str(system_dir)
        print(f"Using default system directory: {system_dir}")

    if args.scale is not None:
        config["scale"] = args.scale
    if args.battery is not None:
        config["battery_percent"] = args.battery
    if args.charging:
        config["battery_charging"] = True

    if not build_simulator():
        print("Build failed.")
        return 1

    exe_path = simulator_path()
    if exe_path is None:
        print("Simulator executable not found.")
        return 1

    simulator_args = build_simulator_args(config, args.extra)

    print("Running simulator...")
    env = tool_environment()
    proc = subprocess.Popen(
        [exe_path] + simulator_args,
        env=env,
    )
    return proc.returncode


def cmd_watch(args):
    """Handle watch command."""
    config = load_config()

    if args.card:
        config["music_dir"] = args.card
    if args.system_dir:
        config["system_dir"] = args.system_dir
    if args.scale is not None:
        config["scale"] = args.scale
    if args.battery is not None:
        config["battery_percent"] = args.battery
    if args.charging:
        config["battery_charging"] = True

    if not build_simulator():
        print("Build failed.")
        return 1

    return watch_loop(config)


def cmd_shot(args):
    """Handle shot command for deterministic testing."""
    config = load_config()

    if args.card:
        config["music_dir"] = args.card
    if args.system_dir:
        config["system_dir"] = args.system_dir

    extra = []
    if args.keys:
        extra.extend(["--keys", args.keys])
    if args.screenshot:
        extra.extend(["--screenshot", str(args.screenshot)])
    extra.extend(["--headless"])
    if args.frames:
        extra.extend(["--frames", str(args.frames)])

    if not build_simulator():
        print("Build failed.")
        return 1

    exe_path = simulator_path()
    if exe_path is None:
        print("Simulator executable not found.")
        return 1

    simulator_args = build_simulator_args(config, extra)

    print(f"Running headless simulation...")
    env = tool_environment()
    proc = subprocess.Popen(
        [exe_path] + simulator_args,
        env=env,
    )
    return proc.returncode


def cmd_test(args):
    """Handle test command."""
    root = repo_root()

    ctest_cmd = ["ctest", "--preset", "dev"]
    if args.filter:
        ctest_cmd.extend(["-R", args.filter])

    print(f"Running tests{' with filter: ' + args.filter if args.filter else ''}...")
    result = subprocess.run(ctest_cmd, cwd=root, text=True)
    return result.returncode


def cmd_debug(args):
    """Handle debug command."""
    config = load_config()

    if args.extra:
        config["scale"] = 1
        config["battery_percent"] = 100
        config["battery_charging"] = False

    if not build_simulator():
        print("Build failed.")
        return 1

    exe_path = simulator_path()
    if exe_path is None:
        print("Simulator executable not found.")
        return 1

    simulator_args = build_simulator_args(config, args.extra)

    lldb_cmd = ["lldb", "--", exe_path] + simulator_args

    print(f"Launching: {' '.join(lldb_cmd)}")
    result = subprocess.run(lldb_cmd, text=True)
    return result.returncode


def cmd_card(args):
    """Handle card command - generate test library."""
    root = repo_root()
    fixtures_dir = root / "fixtures"
    fixtures_dir.mkdir(parents=True, exist_ok=True)

    python_path = venv_python() or sys.executable
    script = root / "tools" / "make_test_library.py"

    cmd = [
        str(python_path),
        str(script),
        "--output", str(fixtures_dir),
        "--tracks", str(args.tracks),
    ]

    if args.covers:
        cmd.append("--covers")

    print(f"Generating {args.tracks} tracks...")
    result = subprocess.run(cmd, cwd=root, text=True)

    if result.returncode != 0:
        print("Failed to generate test library:")
        print(result.stdout)
        if result.stderr:
            print(result.stderr)
        return 1

    print(result.stdout.strip())
    return 0


def cmd_config(args):
    """Handle config command."""
    config = load_config()

    key_map = {
        "card": "music_dir",
        "system-dir": "system_dir",
        "system_dir": "system_dir",
        "scale": "scale",
        "battery": "battery_percent",
        "charging": "battery_charging",
    }

    if args.key not in key_map:
        print(f"Error: unknown key '{args.key}'")
        print(f"Valid keys: card, system-dir, scale, battery, charging")
        return 1

    config_key = key_map[args.key]

    if config_key in ["music_dir", "system_dir"]:
        config[config_key] = args.value
    elif config_key == "scale":
        config[config_key] = int(args.value)
    elif config_key == "battery_percent":
        config[config_key] = int(args.value)
    elif config_key == "battery_charging":
        config[config_key] = args.value.lower() in ["true", "1", "yes"]

    save_config(config)
    print(f"Set {args.key} = {args.value}")
    return 0


def cmd_print_args(args):
    """Handle print-args command."""
    config = load_config()
    simulator_args = build_simulator_args(config, args.extra)

    output = {
        "simulator": simulator_path() or "not built",
        "arguments": simulator_args,
    }
    print(json.dumps(output, indent=2))
    return 0


def cmd_clean(args):
    """Handle clean command."""
    root = repo_root()

    if args.dev:
        dev_dir = root / "build" / "dev"
        if dev_dir.exists():
            print(f"Removing {dev_dir}")
            shutil.rmtree(dev_dir)
            print("Cleaned build/dev")

    if args.sim:
        sim_dir = root / "build" / "sim"
        if sim_dir.exists():
            print(f"Removing {sim_dir}")
            shutil.rmtree(sim_dir)
            print("Cleaned build/sim")

    if not args.dev and not args.sim:
        print("Nothing cleaned. Use --dev or --sim.")

    return 0


def cmd_uninstall():
    """Handle uninstall command - remove packages and directories installed by setup."""
    root = repo_root()

    print("Uninstalling packages and tools...")

    python_path = venv_python()
    pip_path = python_path.parent / "pip" if python_path else None

    if pip_path and pip_path.exists():
        print(f"Uninstalling pip packages...")
        packages = ["cmake", "ninja", "pillow", "fonttools", "mutagen"]
        for package in packages:
            result = subprocess.run(
                [str(pip_path), "uninstall", "-y", package],
                cwd=root, capture_output=True, text=True
            )
            if result.returncode == 0:
                print(f"  Uninstalled: {package}")
            else:
                print(f"  Failed to uninstall {package}: {result.stderr}")

    if python_path and python_path.exists():
        venv_dir = python_path.parent
        venv_pip = venv_dir / "Scripts" / "pip.exe" if sys.platform == "win32" else venv_dir / "bin" / "pip"

        if venv_pip.exists():
            print("Removing .venv directory...")
            shutil.rmtree(venv_dir)
            print("  .venv removed")

    build_dir = root / "build"
    if build_dir.exists():
        print("Removing build directory...")
        shutil.rmtree(build_dir)
        print("  build/ removed")

    system_dir = root / "build" / "sim" / "system"
    if system_dir.exists() or (build_dir / "sim").exists():
        sim_build = build_dir / "sim"
        if sim_build.exists():
            shutil.rmtree(sim_build)
            print("  build/sim/ removed")

    cache_dir = root / ".cache"
    if cache_dir.exists():
        print("Removing .cache directory...")
        shutil.rmtree(cache_dir)
        print("  .cache/ removed")

    config_file = root / "sim.local.json"
    if config_file.exists():
        print("Removing sim.local.json...")
        config_file.unlink()
        print("  sim.local.json removed")

    print("Uninstall complete!")
    return 0


def main(argv=None):
    """Main entry point."""
    args = parse_arguments(argv or sys.argv[1:])

    if args.command is None:
        parser = argparse.ArgumentParser()
        parser.print_help()
        return 1

    handlers = {
        "setup": lambda: cmd_setup(),
        "doctor": lambda: cmd_doctor(),
        "build": lambda: cmd_build(),
        "run": lambda: cmd_run(args),
        "watch": lambda: cmd_watch(args),
        "shot": lambda: cmd_shot(args),
        "test": lambda: cmd_test(args),
        "debug": lambda: cmd_debug(args),
        "card": lambda: cmd_card(args),
        "uninstall": lambda: cmd_uninstall(),
        "config": lambda: cmd_config(args),
        "print-args": lambda: cmd_print_args(args),
        "clean": lambda: cmd_clean(args),
    }

    handler = handlers.get(args.command)
    if handler:
        return handler()

    print(f"Unknown command: {args.command}")
    return 1


if __name__ == "__main__":
    sys.exit(main())