# Drop the section header table and everything past the last loaded byte
# (same idea as sstrip from ELFkickers). The loader only reads program headers.
import struct, sys
f = sys.argv[1]; b = bytearray(open(f, "rb").read())
is64 = b[4] == 2
if is64:
    phoff, = struct.unpack_from("<Q", b, 0x20); phentsize, phnum = struct.unpack_from("<HH", b, 0x36)
else:
    phoff, = struct.unpack_from("<I", b, 0x1c); phentsize, phnum = struct.unpack_from("<HH", b, 0x2a)
end = phoff + phentsize * phnum
for i in range(phnum):
    o = phoff + i * phentsize
    if is64: off, fsz = struct.unpack_from("<Q", b, o + 8)[0], struct.unpack_from("<Q", b, o + 32)[0]
    else:    off, fsz = struct.unpack_from("<I", b, o + 4)[0], struct.unpack_from("<I", b, o + 16)[0]
    end = max(end, off + fsz)
if is64: struct.pack_into("<QIHHHHHH", b, 0x28, 0, 0, 64, 56, phnum, 0, 0, 0)   # e_shoff=0, shentsize/num/strndx=0
else:    struct.pack_into("<IIHHHHHH", b, 0x20, 0, 0, 52, 32, phnum, 0, 0, 0)
open(f, "wb").write(b[:end])
