#!/usr/bin/env python3
# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# sksrv.py -- the host side of sktest get and put (amiga/panotcp/sktest.c): one line "GET <n>" is answered with n bytes,
# "PUT <n>" is followed by n bytes from the Amiga and answered with a line of the count and the time taken.
# Mike Wolak, EPROM Foundry Inc., pano-g2-amiga.  Usage: sksrv.py [port]  (default 5001)
import socket, sys, threading, time

def serve(c, a):
    f = c.makefile('rb')
    line = f.readline().decode(errors='replace').split()
    if len(line) == 2 and line[0] == 'GET':
        n = int(line[1]); blk = bytes((i * 7 + 3) & 255 for i in range(65536)); t0 = time.time(); left = n
        while left:
            k = min(left, len(blk)); c.sendall(blk[:k]); left -= k
        print('%s GET %d in %.2f s' % (a[0], n, time.time() - t0), flush=True)
    elif len(line) == 2 and line[0] == 'PUT':
        n = int(line[1]); got = 0; t0 = None; bad = 0
        while got < n:
            d = f.read1(65536)
            if not d: break
            if t0 is None: t0 = time.time()
            for i, b in enumerate(d):
                if b != (((got + i) % 65536) * 7 + 3) & 255: bad += 1
            got += len(d)
        dt = time.time() - (t0 or time.time())
        c.sendall(('OK %d bytes %.3f s %d bad\n' % (got, dt, bad)).encode())
        print('%s PUT %d in %.2f s, %d bad' % (a[0], got, dt, bad), flush=True)
    c.close()

s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('', int(sys.argv[1]) if len(sys.argv) > 1 else 5001)); s.listen(4)
while True:
    c, a = s.accept(); threading.Thread(target=serve, args=(c, a), daemon=True).start()
