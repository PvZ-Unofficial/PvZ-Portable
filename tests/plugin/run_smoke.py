"""Run the independent SDK DLL against the real game, using a temporary profile."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

p = argparse.ArgumentParser()
p.add_argument("--game", type=Path, required=True)
p.add_argument("--plugin", type=Path, required=True)
p.add_argument("--resources", type=Path, required=True)
p.add_argument("--profile", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--expect", default="attach,initialize,validated,update,shutdown,unload")
p.add_argument("--without-plugin", action="store_true")
p.add_argument("--timeout", type=float, default=30)
args = p.parse_args()
expected = args.expect.split(",") if args.expect else []
with tempfile.TemporaryDirectory(prefix="pvzp-sdk-test-") as temporary:
    temporary = Path(temporary)
    profile = temporary / "profile"
    shutil.copytree(args.profile, profile)
    report = temporary / "plugin.log"
    env = dict(os.environ, PVZP_PLUGIN=str(args.plugin.resolve()), PVZP_TEST_REPORT=str(report))
    env.pop("RSVZ_PORTABLE_PLUGIN", None)
    if args.without_plugin:
        env.pop("PVZP_PLUGIN", None)
    env["PATH"] = "C:/msys64/ucrt64/bin;" + env["PATH"]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (temporary / "game.log").open("w") as log:
        process = subprocess.Popen(
            [str(args.game.resolve()), "-resdir", str(args.resources.resolve()), "-savedir", str(profile)],
            cwd=args.game.resolve().parent, env=env, stdout=log, stderr=log, startupinfo=startup,
        )
        actual = []
        passed = False
        try:
            if args.without_plugin:
                time.sleep(5)
            deadline = time.monotonic() + args.timeout
            actual = []
            while time.monotonic() < deadline:
                if report.exists():
                    actual = report.read_text().splitlines()
                if actual == expected:
                    # The host must remain alive after plugin shutdown/unload.
                    time.sleep(0.5)
                    assert process.poll() is None, "game exited during plugin shutdown"
                    break
                if process.poll() is not None:
                    break
                time.sleep(0.05)
            assert actual == expected, (actual, expected, (temporary / "game.log").read_text(errors="replace"))
            passed = True
        finally:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps({"passed": passed, "events": actual, "expected": expected,
                "game_alive": process.poll() is None, "exit_code": process.poll()}, indent=2) + "\n")
            log.flush()
            shutil.copyfile(temporary / "game.log", args.output.with_suffix(".game.log"))
            # This is a test-owned process with a disposable profile. Terminate
            # only after recording whether the DLL was unloaded by the host.
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
