"""Ad-hoc disassembler for game_win64_bankrelease.exe (RVA in, asm out)."""
import sys
import pefile
import capstone

EXE = r"X:\gta5\build\dev_ng\game_win64_bankrelease.exe"
pe = pefile.PE(EXE, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase


def read_rva(rva, n):
    for s in pe.sections:
        va = s.VirtualAddress
        if va <= rva < va + s.Misc_VirtualSize:
            off = s.PointerToRawData + (rva - va)
            return s.get_data()[rva - va: rva - va + n]
    raise ValueError("rva %x not in any section" % rva)


def disasm(rva, n=200, base_va=None):
    code = read_rva(rva, n)
    va = image_base + rva if base_va is None else base_va
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    out = []
    for ins in md.disasm(code, va):
        out.append("%016X  %-32s %s %s" % (ins.address, ins.bytes.hex(),
                                           ins.mnemonic, ins.op_str))
    return "\n".join(out)


def joaat(name):
    name = name.lower().encode()
    h = 0
    for c in name:
        h = (h + c) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h ^= h >> 6
    h = (h + (h << 3)) & 0xFFFFFFFF
    h ^= h >> 11
    h = (h + (h << 15)) & 0xFFFFFFFF
    return h


if __name__ == "__main__":
    if sys.argv[1] == "d":
        rva = int(sys.argv[2], 16)
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 200
        print(disasm(rva, n))
    elif sys.argv[1] == "h":
        for name in sys.argv[2:]:
            print("%08X %s" % (joaat(name), name))
