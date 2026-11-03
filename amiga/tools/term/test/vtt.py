#!/usr/bin/env python3
# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
"""vtt.py -- libvterm's t/*.test run against a harness whose output was recorded on another machine.

   vtt.py gen  TEST CMDFILE          the commands the harness gets, in order (run-test.pl's conversion)
   vtt.py check TEST OUTFILE         compare the harness's recorded output with the test's expectations

The conversion and the comparison follow t/run-test.pl of libvterm 0.3.3; the Perl expressions of the test files
(double-quoted strings with escapes, '.' concatenation, 'x' repetition, number lists) are parsed here, not run.
"""
import re, sys

def parse_expr(s):
    """a Perl expression of the tests: a list of items, each a string or number; returns a list of values"""
    i, n = 0, len(s)
    def ws():
        nonlocal i
        while i < n and s[i] in ' \t': i += 1
    def string():
        nonlocal i
        q = s[i]; i += 1; out = []
        while i < n and s[i] != q:
            c = s[i]
            if c == '\\' and q == '"':
                i += 1; c = s[i]
                if c == 'e': out.append(0x1b); i += 1
                elif c == 'a': out.append(7); i += 1
                elif c == 'b': out.append(8); i += 1
                elif c == 'n': out.append(10); i += 1
                elif c == 'r': out.append(13); i += 1
                elif c == 't': out.append(9); i += 1
                elif c == '0': out.append(0); i += 1
                elif c == 'x':
                    i += 1
                    if s[i] == '{':
                        j = s.index('}', i); v = int(s[i+1:j], 16)
                        out.extend([v] if v < 256 else chr(v).encode('utf-8')); i = j + 1   # Perl: a code point below 256 is one byte
                    else:
                        m = re.match(r'[0-9A-Fa-f]{1,2}', s[i:]); out.append(int(m.group(0), 16)); i += len(m.group(0))
                else: out.append(ord(c)); i += 1
            elif c == '\\' and q == "'" and i + 1 < n and s[i+1] in "'\\":
                out.append(ord(s[i+1])); i += 2
            else:
                out.extend(c.encode('utf-8')); i += 1
        i += 1
        return bytes(out)
    def term():
        nonlocal i
        ws()
        if s[i] in '"\'':
            v = string()
        else:
            m = re.match(r'0x[0-9A-Fa-f]+|\d+', s[i:]); t = m.group(0)
            v = int(t, 16) if t.startswith('0x') else int(t, 8) if len(t) > 1 and t[0] == '0' else int(t)
            i += len(t)
        ws()
        while i < n and s[i] == 'x' and isinstance(v, bytes):
            i += 1; ws()
            m = re.match(r'\d+', s[i:]); v = v * int(m.group(0)); i += len(m.group(0)); ws()
        return v
    def concat():
        nonlocal i
        v = term()
        while i < n and s[i] == '.':
            i += 1; v = v + term()
        return v
    vals = []
    ws()
    while i < n:
        vals.append(concat()); ws()
        if i < n and s[i] == ',': i += 1; ws()
    return vals

def as_bytes(expr):
    return b''.join(v if isinstance(v, bytes) else bytes([v]) for v in parse_expr(expr))

def convert_expect(line):
    m = re.match(r'^(text|encout) (.*)$', line)
    if m: return m.group(1) + ' ' + ','.join('%x' % v for v in parse_expr(m.group(2)))
    m = re.match(r'^(output) (.*)$', line)
    if m: return m.group(1) + ' ' + ','.join('%x' % b for b in as_bytes(m.group(2)))
    m = re.match(r'^control (.*)$', line)
    if m: return 'control %02x' % parse_expr(m.group(1))[0]
    m = re.match(r'^csi (\S+) (.*)$', line)
    if m: return 'csi %02x %s' % (parse_expr(m.group(1))[0], m.group(2))
    m = re.match(r'^(osc) (\[\d+)? *(.*?)(\]?)$', line)
    if m:
        cmd, init, data, fin = m.groups(); init = init or ''
        if re.search(r'\d+', init): init += ';'
        return '%s %s%s%s' % (cmd, init, ''.join('%02x' % b for b in (as_bytes(data) if data else b'')), fin)
    m = re.match(r'^(escape|dcs|apc|pm|sos) (\[?)(.*?)(\]?)$', line)
    if m:
        return '%s %s%s%s' % (m.group(1), m.group(2), ''.join('%02x' % b for b in (as_bytes(m.group(3)) if m.group(3) else b'')), m.group(4))
    m = re.match(r'^putglyph (\S+) (.*)$', line)
    if m: return 'putglyph ' + ','.join('%x' % v for v in parse_expr(m.group(1))) + ' ' + m.group(2)
    if re.match(r'^(?:movecursor|scrollrect|moverect|erase|damage|sb_pushline|sb_popline|sb_clear|settermprop|setmousefunc|selection-query) ?', line):
        return line
    m = re.match(r'^(selection-set) (.*?) (\[?)(.*?)(\]?)$', line)
    if m: return '%s %s %s%s%s' % (m.group(1), m.group(2), m.group(3), ''.join('%02x' % b for b in as_bytes(m.group(4))), m.group(5))
    sys.stderr.write("unrecognised expectation %r\n" % line)
    return line

def events(path):
    """the test as a list: ('cmd', command, [expectations], line) and ('ask', query, want, kind, line)"""
    ev, cur = [], None
    def flush():
        nonlocal cur
        if cur: ev.append(cur); cur = None
    def do_line(line, ln):
        nonlocal cur
        if line.startswith('!'):
            flush()
        elif re.match(r'^[A-Z]+', line):
            m = re.match(r'^(PUSH|ENCIN) (.*)$', line)
            if m: line = m.group(1) + ' ' + as_bytes(m.group(2)).hex()
            m = re.match(r'^(SELECTION \d+) +(\[?)(.*?)(\]?)$', line)
            if m: line = '%s %s %s %s' % (m.group(1), m.group(2), as_bytes(m.group(3)).hex(), m.group(4))
            flush(); cur = ['cmd', line, [], ln]
        elif re.match(r'^[a-z]+', line):
            cur[2].append(convert_expect(line))
        elif re.match(r'^\?screen_row\s+(\d+)\s*=\s*', line):
            m = re.match(r'^\?screen_row\s+(\d+)\s*=\s*(.*)$', line)
            flush()
            want = list(as_bytes(m.group(2))) if m.group(2).startswith('"') else [int(x, 16) & 255 for x in m.group(2).split(',') if x]
            ev.append(['ask', '?screen_chars ' + m.group(1), want, 'row', ln])
        elif re.match(r'^\?([a-z]+.*?=)\s*', line):
            m = re.match(r'^\?([a-z]+.*?=)\s*(.*)$', line)
            flush()
            assertion = re.match(r'^(.*)\s+=', m.group(1)).group(1); want = m.group(2)
            if assertion.startswith('screen_chars') and want.startswith('"'):
                want = ','.join('0x%02x' % b for b in as_bytes(want))
            ev.append(['ask', '?' + assertion, want, 'eq', ln])
        elif re.match(r'^\$SEQ\s+(\d+)\s+(\d+):\s*', line):
            m = re.match(r'^\$SEQ\s+(\d+)\s+(\d+):\s*(.*)$', line)
            for v in range(int(m.group(1)), int(m.group(2)) + 1): do_line(m.group(3).replace('\\#', str(v)), ln)
        elif re.match(r'^\$REP\s+(\d+):\s*', line):
            m = re.match(r'^\$REP\s+(\d+):\s*(.*)$', line)
            for _ in range(int(m.group(1))): do_line(m.group(2), ln)
        else:
            raise SystemExit('unrecognised test line %r' % line)
    for ln, raw in enumerate(open(path, encoding='utf-8'), 1):
        line = raw.strip()
        if not line or line.startswith('#'): continue
        if line == '__END__': break
        do_line(line, ln)
    flush()
    return ev

def gen(test, out):
    with open(out, 'w', newline='\n') as f:
        for e in events(test): f.write(e[1] + '\n')

def check(test, outfile):
    lines = [l.rstrip('\n') for l in open(outfile, encoding='latin-1')]
    k, fails = 0, 0
    for e in events(test):
        if e[0] == 'cmd':
            exp = list(e[2]); bad = 0
            while k < len(lines):
                o = lines[k]; k += 1
                if o in ('DONE', '?'): break
                if not exp: bad += 1; print('# line %d: expected nothing more, got %s' % (e[3], o)); continue
                x = exp.pop(0)
                if x != o: bad += 1; print('# line %d: expected %s, got %s' % (e[3], x, o))
            if exp: bad += 1; print('# line %d: did not happen: %s' % (e[3], exp))
            fails += bool(bad)
        else:
            o = lines[k].strip() if k < len(lines) else None; k += 1
            if e[3] == 'row':
                got = [int(x, 16) & 255 for x in o.split(',') if x] if o else []
                ok = got == e[2]
            else:
                ok = o == e[2]
            if not ok: fails += 1; print('# line %d: %s expected %r, got %r' % (e[4], e[1], e[2], o))
    return fails

if __name__ == '__main__':
    if sys.argv[1] == 'gen': gen(sys.argv[2], sys.argv[3])
    else:
        f = check(sys.argv[2], sys.argv[3]); print('%s: %s' % (sys.argv[2], 'PASS' if not f else '%d FAIL' % f)); sys.exit(1 if f else 0)
