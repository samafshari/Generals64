"""Extract a single file from a Generals .big archive.

.big format (Generals / ZH):
    4B  magic: 'BIGF' or 'BIG4'
    4B  file size (big-endian)
    4B  num files (big-endian)
    4B  data offset (big-endian)
    ----
    for each file:
        4B  offset (big-endian)
        4B  size   (big-endian)
        null-terminated name (\0-terminated ASCII)

Usage: python extract_big.py <big-path> <needle> <out-dir>
       needle is matched case-insensitively as a substring.
"""
import struct, sys, os, io

def extract(big_path, needle, out_dir):
    needle = needle.lower()
    os.makedirs(out_dir, exist_ok=True)
    with open(big_path, 'rb') as f:
        magic = f.read(4)
        if magic not in (b'BIGF', b'BIG4'):
            raise SystemExit(f'bad magic: {magic!r}')
        _totalSize = struct.unpack('>I', f.read(4))[0]
        numFiles   = struct.unpack('>I', f.read(4))[0]
        _dataOff   = struct.unpack('>I', f.read(4))[0]
        entries = []
        for _ in range(numFiles):
            off  = struct.unpack('>I', f.read(4))[0]
            size = struct.unpack('>I', f.read(4))[0]
            name_bytes = bytearray()
            while True:
                c = f.read(1)
                if c == b'\0' or c == b'':
                    break
                name_bytes.extend(c)
            name = name_bytes.decode('latin-1', errors='replace')
            entries.append((name, off, size))
        print(f'{big_path}: {numFiles} files')
        matches = [e for e in entries if needle in e[0].lower()]
        print(f'{len(matches)} matched "{needle}"')
        for name, off, size in matches:
            f.seek(off)
            data = f.read(size)
            safe = name.replace('\\', '/').replace('/', '__')
            out_path = os.path.join(out_dir, safe)
            with open(out_path, 'wb') as g:
                g.write(data)
            print(f'  {name} ({size}B) -> {out_path}')

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__)
        raise SystemExit(1)
    extract(sys.argv[1], sys.argv[2], sys.argv[3])
