"""Build independent Clang SDK plugins and exercise loader failures in the real EXE."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument("--abi", choices=["msvc", "gnu"], required=True)
p.add_argument("--sdk", type=Path, required=True)
p.add_argument("--game", type=Path, required=True)
p.add_argument("--resources", type=Path, required=True)
p.add_argument("--profile", type=Path, required=True)
p.add_argument("--output", type=Path, required=True)
args = p.parse_args()
subprocess.run([sys.executable, str(Path(__file__).parent / "check_exports.py"), str(args.game)], check=True)
args.output.mkdir(parents=True, exist_ok=True)
llvm = Path("C:/Program Files/LLVM/bin")
source = Path(__file__).parent / "smoke.cpp"
includes = [args.sdk / "src", args.sdk / "src/SexyAppFramework",
            args.sdk / "src/SexyAppFramework/sound/SDL-Mixer-X/include"]
includes.extend(Path(s) for s in (args.sdk / "sdl-include.txt").read_text().split(";") if s)
base = "attach,initialize,validated"
cases = [
    ("paused_cursor", ["PVZP_TEST_PAUSED_CURSOR"], base + ",cursor-passed,shutdown,unload"),
    ("normal", [], base + ",update,shutdown,unload"),
    ("invalid_update", ["PVZP_TEST_INVALID_UPDATE"], base + ",update,shutdown,unload"),
    ("version", ["PVZP_TEST_BAD_VERSION"], "attach,unload"),
    ("lawnapp", ["PVZP_TEST_BAD_LAYOUT=1"], "attach,initialize,shutdown,unload"),
    ("board", ["PVZP_TEST_BAD_LAYOUT=2"], "attach,initialize,shutdown,unload"),
    ("pool", ["PVZP_TEST_BAD_LAYOUT=3"], "attach,initialize,shutdown,unload"),
    ("partial", ["PVZP_TEST_PARTIAL_INIT"], base + ",shutdown,unload"),
    ("shutdown_failure", ["PVZP_TEST_SHUTDOWN_FAILURE"], base + ",update,shutdown"),
    ("partial_shutdown_failure", ["PVZP_TEST_PARTIAL_INIT", "PVZP_TEST_SHUTDOWN_FAILURE"], base + ",shutdown"),
]
env = dict(os.environ, PATH="C:/msys64/ucrt64/bin;" + os.environ["PATH"])
for name, definitions, expected in cases:
    dll = args.output / (name + ".dll")
    if args.abi == "msvc":
        command = [str(llvm / "clang-cl.exe"), "/std:c++20", "/utf-8", "/MD", "/LD", str(source),
                   "/Fe" + str(dll), "/Fo" + str(args.output / (name + ".obj"))]
    else:
        command = [str(llvm / "clang++.exe"), "--target=x86_64-w64-windows-gnu", "--sysroot=C:/msys64/ucrt64",
                   "-std=c++20", "-shared", str(source), "-fuse-ld=lld", "-o", str(dll)]
    command.extend("-I" + str(path) for path in includes)
    command.extend("-D" + definition for definition in definitions)
    if args.abi == "msvc":
        command.extend(["/link", "/LIBPATH:" + str(args.sdk / "lib"), "pvz-portable.lib"])
    else:
        command.extend(["-L" + str(args.sdk / "lib"), "-lpvz-portable"])
    with (args.output / (name + ".build.log")).open("w") as log:
        subprocess.run(command, env=env, check=True, stdout=log, stderr=log)
    run = [sys.executable, str(Path(__file__).parent / "run_smoke.py"), "--game", str(args.game),
           "--plugin", str(dll), "--resources", str(args.resources), "--profile", str(args.profile),
           "--output", str(args.output / (name + ".json")), "--expect", expected]
    subprocess.run(run, env=env, check=True)
    print(name, "PASS", flush=True)
    if name == "normal":
        repeat = run.copy()
        repeat[repeat.index("--output") + 1] = str(args.output / "restart.json")
        subprocess.run(repeat, env=env, check=True)
        print("restart PASS", flush=True)
        no_plugin = repeat.copy()
        no_plugin[no_plugin.index("--output") + 1] = str(args.output / "no-plugin.json")
        no_plugin[no_plugin.index("--expect") + 1] = ""
        no_plugin.append("--without-plugin")
        subprocess.run(no_plugin, env=env, check=True)
        print("no-plugin PASS", flush=True)
