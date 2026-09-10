"""Reject invalid PE export RVAs, including exports discarded by GNU LTO."""
import re
import subprocess
import sys

def check(path):
    text = subprocess.check_output([
        "C:/Program Files/LLVM/bin/llvm-readobj.exe", "--file-headers", "--coff-exports", str(path)
    ], text=True)
    size = int(re.search(r"SizeOfImage: (\d+)", text)[1])
    exports = re.findall(r"Export \{\s+Ordinal: \d+\s+Name: (\S+)\s+RVA: (0x[0-9A-Fa-f]+)", text)
    assert exports, "no native exports"
    for name, rva in exports:
        assert 0 < int(rva, 16) < size, f"invalid export {name}: RVA {rva}, image size {size}"
    print(f"{len(exports)} export addresses PASS")

if __name__ == "__main__":
    check(sys.argv[1])
