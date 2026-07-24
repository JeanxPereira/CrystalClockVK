import sys
from tools.vu.decode import decode_macro_cop2
from tools.vu.dump_vu import load_and_decode, format_listing

def main(argv):
    if not argv:
        print("usage: macro <hex...> | micro <file> [--base 0x..] | selfcheck")
        return 2
    mode, rest = argv[0], argv[1:]
    if mode == "macro":
        for h in rest:
            w = int(h, 16)
            print(f"0x{w:07x}: {decode_macro_cop2(w)}")
        return 0
    if mode == "micro":
        path = rest[0]; base = 0
        if "--base" in rest:
            base = int(rest[rest.index("--base") + 1], 16)
        print(format_listing(load_and_decode(path, base)))
        return 0
    if mode == "selfcheck":
        import subprocess
        r = subprocess.run([sys.executable, "-m", "pytest", "tools/vu/tests/", "-q", "--ignore=tools/vu/tests/test_cli.py"])
        print("OK" if r.returncode == 0 else "FAIL")
        return r.returncode
    print(f"unknown mode: {mode}")
    return 2
