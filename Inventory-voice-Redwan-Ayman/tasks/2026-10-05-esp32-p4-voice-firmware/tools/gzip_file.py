#!/usr/bin/env python3
"""
Gzip one file, for embedding the dashboard page into flash at build time.

mtime is forced to 0 so the output is byte-identical for identical input. Without
that, every build would produce a different .gz, which would dirty the embedded blob
and force a relink on every single build.
"""
import gzip
import shutil
import sys

if len(sys.argv) != 3:
    sys.exit("usage: gzip_file.py <input> <output.gz>")

src, dst = sys.argv[1], sys.argv[2]
with open(src, "rb") as fin, gzip.GzipFile(dst, "wb", compresslevel=9, mtime=0) as fout:
    shutil.copyfileobj(fin, fout)
