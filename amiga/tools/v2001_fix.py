#!/usr/bin/env python3
# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# v2001_fix.py IN OUT -- the mechanical rewrites that make sv2v's output acceptable to XST 14.7.
#
# None of them changes behaviour.  tools/xst_scan.py finds the constructs XST refuses before a build is spent.
#
# Every file:
#   localparam in a generate block  sv2v writes `localparam i = _gv_i_1;` inside each generate-for iteration
#              and `localparam w = expr;` inside a generate-if.  XST's Verilog-2001 refuses
#              a localparam there (HDLCompilers:26 "expecting 'end', found 'localparam'").  The declaration is
#              dropped and the name replaced by its value, parenthesised, from there to the block's `end` --
#              except after a `.`, which is a named port or parameter connection, not a use.
#
#   memory read in always @(*)  `x = ARR[expr];` inside a combinational block, ARR a reg array
#              (Xst:902 "Unexpected ... event in always block sensitivity list").  Each read becomes `wire [w] ARR_rdN = ARR[expr];` declared just BEFORE that always
#              block, where everything the index uses is already declared, and the assignment reads the wire.
#
#   $clog2      in a declaration or a localparam (fpga-toccata's FIFO widths and counters): XST refuses the system
#              function there (HDLCompiler:815 "System function call clog2 is not allowed here").  A module that
#              uses it gets a constant function f_clog2 (the ceiling of log2, $clog2's value for every argument >= 1)
#              right after its port list, and every $clog2( in it becomes f_clog2(.
#
#   constant-index memory reads in always @(*)  `ARR[3][5:0]` inside a combinational block, ARR a reg array (fpga-toccata's
#              AD1848 register file; Xst:1468 as above).  Each element read becomes `wire [w] ARR_cN = ARR[N];`
#              declared just before that block, and the block reads the wire.
#
# FX68K only (applied when the file holds module excUnit / pla_lined):
#   excUnit    regs68L / regs68H (the 68000 register file) are read at actualRx / actualRy inside the
#              always @(*) bus muxes (Xst:1468 / Xst:902).  Each read becomes a continuous assign to a wire,
#              declared AFTER actualRx / actualRy: XST wants a name declared before it is used (HDLCompilers:28).
#   pla_lined  arA1 / arA23 are 16 x 10-bit tables written element by element, at constant indices only,
#              inside always @(*).  Each becomes one flat 160-bit vector: arr['hK] -> arr[10K+9:10K], and the
#              read arr[line] -> arr[line*10 +: 10] (a variable part-select READ, which XST accepts).
import re, sys

s = open(sys.argv[1]).read()

# ---- localparam inside a generate block ----------------------------------------------------------------------
lines = s.split('\n')
for n in range(len(lines) - 1, -1, -1):          # innermost (last) first, so nested blocks resolve correctly
    m = re.match(r'^\t\t+localparam\s+(?:\[[^\]]+\]\s*)?(\w+)\s*=\s*(.+?);\s*$', lines[n])
    if not m:
        continue
    name, val = m.group(1), m.group(2)
    depth, end = 1, None
    for k in range(n + 1, len(lines)):
        depth += len(re.findall(r'\bbegin\b', lines[k])) - len(re.findall(r'\bend\b', lines[k]))
        if depth == 0:
            end = k
            break
    if end is None:
        sys.exit('v2001_fix: no end for the block holding localparam %s (line %d)' % (name, n + 1))
    for k in range(n + 1, end + 1):
        lines[k] = re.sub(r'(?<![.\w])%s\b' % re.escape(name), '(%s)' % val, lines[k])
    del lines[n]
s = '\n'.join(lines)

# ---- memory reads inside always @(*) -----------------------------------------------------------------------
def hoist(mod):
    arrays = dict((m.group(2), m.group(1) or '') for m in
                  re.finditer(r'(?m)^\s*reg\s+(?:signed\s+)?(\[[^\]]+\])?\s*([A-Za-z_]\w*)\s*\[\d+:\d+\];', mod))
    if not arrays:
        return mod
    out, pos, count = [], 0, 0
    for blk in re.finditer(r'(?m)^(\t)always @\(\*\) begin\n(?:.*\n)*?\1end\n', mod):
        body, wires = blk.group(0), ''
        def rep(m):
            nonlocal wires, count
            if m.group(2) not in arrays:
                return m.group(0)
            count += 1
            w = '%s_rd%d' % (m.group(2), count)
            wires += '\twire %s %s = %s[%s];\n' % (arrays[m.group(2)], w, m.group(2), m.group(3))
            return '%s = %s;' % (m.group(1), w)
        body = re.sub(r'(\b\w+) = (\w+)\[([^\]\n]+)\];', rep, body)
        out.append(mod[pos:blk.start()] + wires.replace('  ', ' ') + body)
        pos = blk.end()
    out.append(mod[pos:])
    return ''.join(out)
if not re.search(r'(?m)^module excUnit\b', s):          # FX68K's register file has its own rewrite below
    s = ''.join(hoist(m) for m in re.split(r'(?m)^(?=module\s)', s))

# ---- FX68K: hoist the register-file reads --------------------------------------------------------------------
if re.search(r'(?m)^module excUnit\b', s):
    after = re.search(r'(?m)^(\s*)reg \[4:0\] actualRy;\n', s)
    if not after or not re.search(r'(?m)^\s*reg \[4:0\] actualRx;\n', s[:after.start()]):
        sys.exit('v2001_fix: actualRx / actualRy declarations not found in that order')
    wires = ''
    for half in ('L', 'H'):
        arr = 'regs68' + half
        if not re.search(r'(?m)^\s*reg \[15:0\] %s \[0:17\];\n' % arr, s):
            sys.exit('v2001_fix: %s declaration not found' % arr)
        for idx, tag in (('actualRx', 'rx'), ('actualRy', 'ry')):
            name = '%s_%s' % (arr, tag)
            s, k = re.subn(r'= %s\[%s\];' % (arr, idx), '= %s;' % name, s)
            if k:
                wires += '%swire [15:0] %s = %s[%s];\n' % (after.group(1), name, arr, idx)
    after = re.search(r'(?m)^(\s*)reg \[4:0\] actualRy;\n', s)
    s = s[:after.end()] + wires + s[after.end():]

# ---- FX68K: flatten the constant-index PLA tables ------------------------------------------------------------
if re.search(r'(?m)^module pla_lined\b', s):
    for arr in ('arA1', 'arA23'):
        s, n = re.subn(r'(?m)^(\s*)reg \[9:0\] %s \[15:0\];' % arr, r'\1reg [159:0] %s;' % arr, s)
        if n != 1:
            sys.exit('v2001_fix: %s declaration not found' % arr)
        s = re.sub(r'\b%s\[\'h([0-9a-fA-F]+)\]' % arr,
                   lambda m: '%s[%d:%d]' % (arr, int(m.group(1), 16) * 10 + 9, int(m.group(1), 16) * 10), s)
        s = s.replace('%s[line]' % arr, '%s[line*10 +: 10]' % arr)
        left = re.findall(r'\b%s\[(?!\d+:\d+\]|line\*10)' % arr, s)
        if left:
            sys.exit('v2001_fix: %d %s index(es) not rewritten' % (len(left), arr))

# ---- constant-index reads of a reg array inside always @(*) -> wires ------------------------------------------
parts = re.split(r'(?m)^(?=module\s)', s)
for i, part in enumerate(parts):
    arrays = {m[1]: m[0] for m in re.findall(r'(?m)^\s*reg\s+(\[[^\]]+\])\s+(\w+)\s*\[[^\]]+\];', part)}
    if not arrays:
        continue
    out, pos = [], 0
    for m in re.finditer(r'(?m)^(\s*)always @\(\*\) begin\n(.*?)^\1end\n', part, flags=re.S):
        body = m.group(2)
        uses = sorted(set((a, int(n)) for a, n in re.findall(r'\b(\w+)\[(\d+)\]', body) if a in arrays))
        if not uses:
            continue
        decl = ''.join('%swire %s %s_c%d = %s[%d];\n' % (m.group(1), arrays[a], a, n, a, n) for a, n in uses)
        for a, n in uses:
            body = re.sub(r'\b%s\[%d\]' % (a, n), '%s_c%d' % (a, n), body)
        out.append(part[pos:m.start()] + decl + m.group(1) + 'always @(*) begin\n' + body + m.group(1) + 'end\n')
        pos = m.end()
    out.append(part[pos:])
    parts[i] = ''.join(out)
s = ''.join(parts)

# ---- $clog2 -> a constant function per module ----------------------------------------------------------------
CLOG2_FN = ('\tfunction integer f_clog2;\n\t\tinput integer v;\n\t\tinteger k;\n\t\tbegin\n'
            '\t\t\tf_clog2 = 0;\n\t\t\tfor (k = v - 1; k > 0; k = k >> 1) f_clog2 = f_clog2 + 1;\n'
            '\t\tend\n\tendfunction\n')
parts = re.split(r'(?m)^(?=module\s)', s)
for i, part in enumerate(parts):
    if '$clog2(' not in part or not part.startswith('module'):
        continue
    hdr = part.index(');\n') + 3                   # the end of the port list
    parts[i] = part[:hdr] + CLOG2_FN + part[hdr:].replace('$clog2(', 'f_clog2(')
s = ''.join(parts)
if '$clog2(' in s:
    sys.exit('v2001_fix: $clog2 left outside a module')

open(sys.argv[2], 'w').write(s)
