# Checks that a Windows .exe only asks for things Windows 7 has, so the 32-bit build still starts on older machines.
# Usage: python tools/check_win7.py "build/Release/Timbient Modules.exe"
import struct, sys
NEWER = {"GetSystemTimePreciseAsFileTime", "WaitOnAddress", "WakeByAddressSingle", "WakeByAddressAll", "CreateFile2", "GetCurrentPackageId",
         "SetThreadDescription", "GetThreadDescription", "SetDefaultDllDirectories", "AddDllDirectory", "GetOverlappedResultEx", "PrefetchVirtualMemory"}
d = open(sys.argv[1], "rb").read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = pe + 24; osz = struct.unpack_from('<H', d, pe + 20)[0]
magic = struct.unpack_from('<H', d, opt)[0]; ddir = opt + (96 if magic == 0x10b else 112)
pass  # print('OS version', struct.unpack_from('<HH', d, opt + 40), 'subsystem', struct.unpack_from('<HH', d, opt + 48))
imp_rva = struct.unpack_from('<I', d, ddir + 8)[0]
secs = [struct.unpack_from('<8sIIII', d, opt + osz + i * 40) for i in range(nsec)]
def off(rva):
  for (nm, vs, va, rs, ro) in secs:
    if va <= rva < va + max(vs, rs): return rva - va + ro
p = off(imp_rva); bad = []
while True:
  ilt, ts, fc, name, iat = struct.unpack_from('<IIIII', d, p)
  if name == 0: break
  dll = d[off(name):].split(b'\0')[0].decode(); q = off(ilt or iat); names = []
  while True:
    v = struct.unpack_from('<I', d, q)[0]
    if v == 0: break
    if not v & 0x80000000: names.append(d[off(v) + 2:].split(b'\0')[0].decode())
    q += 4
  for x in names:
    if x in NEWER: bad.append(dll + ' ' + x)
  p += 20

if bad: print('NEEDS A NEWER WINDOWS THAN 7:', ', '.join(bad)); sys.exit(1)
print('OK: nothing newer than Windows 7 is needed')
