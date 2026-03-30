"""Build the Linux server using externally installed, verified source dependencies.
Does not download dependencies or change project sources/manifests/lockfiles.
"""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--uwebsockets-root", type=Path, required=True)
parser.add_argument("--json-include-root", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
uws = args.uwebsockets_root.resolve()
json_include = args.json_include_root.resolve()
output = args.output.resolve()
root = Path(__file__).resolve().parents[1]
assert (uws / "src/App.h").is_file(), "uWebSockets source missing"
assert (uws / "uSockets/src/libusockets.h").is_file(), "Initialize pinned uSockets submodule"
assert (json_include / "nlohmann/json.hpp").is_file(), "JSON include root missing"
output.parent.mkdir(parents=True, exist_ok=True)
includes = output.parent / "native-includes"
includes.mkdir(exist_ok=True)
alias = includes / "uwebsockets"
if alias.is_symlink():
    assert alias.resolve() == uws / "src", "Existing include alias points elsewhere"
elif alias.exists():
    raise RuntimeError("Refusing to replace existing uwebsockets include directory")
else:
    alias.symlink_to(uws / "src", target_is_directory=True)
subprocess.run(["make", "-C", str(uws / "uSockets"), "WITH_LTO=0", "-j2"], check=True)
# The repository uses uWS::WebSocket<false,...> (plain WS); no TLS bypass is
# applied to dependency downloads or certificate verification.
command = ["c++", "-std=c++20", "-O0", "-pthread", "-DLIBUS_NO_SSL"]
command += ["-I" + str(path) for path in [root / "include", root / "src", includes,
                                         json_include, uws / "uSockets/src"]]
command += [str(path) for path in sorted((root / "src").rglob("*.cpp"))]
command += [str(uws / "uSockets/uSockets.a"), "-lz", "-o", str(output)]
subprocess.run(command, check=True)
