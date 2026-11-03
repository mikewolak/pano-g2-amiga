#!/usr/bin/env python3
# Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc.  All rights reserved.
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# pano-g2-amiga: licensed for non-commercial use only; the terms are in LICENSE at the top of the repository.
# xst_scan.py FILE -- known XST 14.7 refusals, found in the generated Verilog before a build is spent:
#   A  a memory (reg array) read inside a combinational always block      (Xst:1468 / Xst:902)
#   P  a variable part-select used as an assignment TARGET                 (Xst:2545)
#   L  a localparam inside a generate block                               (HDLCompilers:26)
#   D  a name used before its declaration in the same module              (HDLCompilers:28)
#   F  a system function XST 14.7 does not implement ($clog2, $bits, ...)  (HDLCompilers:26 "unexpected token")
#   I  a part-select of an integer variable (i[W:0])                      (not in XST 14.7's Verilog-2001 subset)
#   O  an ordered parameter override on a module this file does not define (a VHDL entity)       (Xst:1860)
#   S  an always @(*) block that reads no signal (constants only)       (HDLCompilers:168)
#   M  an instance, connected by name, that leaves out a port of its module, the module defined in any of the
#      files given (GHDL's netlists supply the ports of the VHDL entities)                      (Xst:1931)
#
# xst_scan.py FILE [OTHER ...] scans FILE; the OTHER files only supply module port lists for M.
import re, sys
KW = set('''module endmodule input output inout wire reg integer genvar parameter localparam real time tri
assign always initial begin end if else case casez casex endcase default for while repeat forever generate
endgenerate function endfunction task endtask posedge negedge or and not signed unsigned automatic'''.split())
DECL = re.compile(r'^\s*(?:input|output|inout|wire|reg|integer|genvar|parameter|localparam|real|time|tri)\b(.*)$')
def use_before_decl(name, mod):
    body = mod.split(');', 1)[1] if ');' in mod else mod        # the non-ANSI port list names ports before they are declared
    body = re.sub(r'//[^\n]*|/\*.*?\*/|"[^"\n]*"', ' ', body, flags=re.S)
    lines = body.split('\n')
    decl_at = {}
    for i, ln in enumerate(lines):
        m = DECL.match(ln)
        if not m: continue
        rest = re.sub(r'\[[^\]]*\]', ' ', m.group(1)).split('=')[0]
        for n in re.findall(r'[A-Za-z_]\w*', rest):
            if n not in KW: decl_at.setdefault(n, i)
    found = []
    for i, ln in enumerate(lines):
        code = re.sub(r"\d*'[sS]?[bBoOdDhH][0-9a-fA-FxXzZ_?]+|\.[A-Za-z_]\w*\s*\(|\$\w+|`\w+", ' ', ln)
        for n in set(re.findall(r'\b[A-Za-z_]\w*\b', code)):
            if n in decl_at and decl_at[n] > i: found.append((n, i))
    for n in sorted(set(n for n, _ in found)): print('D', name, n)
    return len(set(n for n, _ in found))
s = open(sys.argv[1]).read(); bad = 0
for mod in re.split(r'(?m)^(?=module\s)', s):
    m = re.match(r'module\s+(\S+)', mod)
    if not m: continue
    name = m.group(1).rstrip('(;')
    arrays = re.findall(r'(?m)^\s*reg\s+(?:signed\s+)?(?:\[[^\]]+\])?\s*([A-Za-z_]\w*)\s*\[\d+:\d+\];', mod)
    for blk in re.finditer(r'(?ms)^\talways @\(\*\).*?(?=^\t(?:always|initial|assign|wire|reg|endmodule|function|generate|\w+ (?:#\(|\w+ \()))', mod):
        for a in arrays:
            if re.search(r'\b' + a + r'\s*\[', blk.group(0)): print('A', name, a); bad += 1
    # sv2v's own layout ("\talways @(*) begin ... \tend"): the block above ends at the next top-level keyword and
    # misses a block followed by anything else (Xst:902)
    for blk in re.finditer(r'(?m)^(\t)always @\(\*\) begin\n(?:.*\n)*?\1end\n', mod):
        for a in arrays:
            if re.search(r'=\s*' + a + r'\s*\[', blk.group(0)): print('A', name, a); bad += 1
    bad += use_before_decl(name, mod)
    for t in re.finditer(r'(?m)^\s*([A-Za-z_]\w*)\[((?:[^\[\]\n]|\[[^\]\n]*\])*[A-Za-z_](?:[^\[\]\n]|\[[^\]\n]*\])*)[+-]:\s*\d+\]\s*<?=(?!=)', mod):
        print('P', name, t.group(0).strip()[:90]); bad += 1
for t in re.finditer(r'(?m)^\t\t+localparam\s+(\w+)', s):
    print('L', t.group(1)); bad += 1
for t in re.finditer(r'\$(clog2|bits|size|high|low|left|right|increment|countones|onehot0?|isunknown)\b', s):
    print('F', '$' + t.group(1)); bad += 1
for t in re.finditer(r'(?m)^.*\bi\[[^\]:]+:[^\]]+\].*$', s):
    print('I', t.group(0).strip()[:90]); bad += 1
defined = set(re.findall(r'(?m)^module\s+(\w+)', s))
for t in re.finditer(r'(?m)^\s*(\w+)\s*#\s*\((?!\s*\.)', s):
    if t.group(1) not in defined and t.group(1) not in KW: print('O', t.group(1)); bad += 1
for blk in re.finditer(r'(?m)^(\t)always @\(\*\) begin\n((?:.*\n)*?)\1end\n|^\talways @\(\*\) (?!begin)([^\n]*)\n', s):
    body = blk.group(2) if blk.group(2) is not None else blk.group(3)
    rhs = ' '.join(re.findall(r'<?=\s*([^;]*);', body)) + ' ' + ' '.join(re.findall(r'\b(?:if|case[zx]?)\s*\(([^\n]*)\)', body))
    rhs = re.sub(r"\d*'[sS]?[bBoOdDhH][0-9a-fA-FxXzZ_?]+|\b\d+\b", ' ', rhs)
    if not re.search(r'[A-Za-z_]\w*', rhs): print('S', body.strip().split('\n')[0][:80]); bad += 1
ports = {}                                           # lower-case port names: VHDL names are case-insensitive
for f in sys.argv[2:]:
    for m in re.finditer(r'(?ms)^module\s+(\w+)\s*(?:#\s*\((?:[^()]|\([^()]*\))*\))?\s*\((.*?)\);', open(f).read()):
        if m.group(1) in defined: continue                # M is for modules FILE itself does not define
        hdr = re.sub(r'//[^\n]*', '', m.group(2))
        names = [re.findall(r'(\w+)\s*$', p.strip())[0].lower() for p in hdr.split(',') if re.findall(r'(\w+)\s*$', p.strip())]
        ports[m.group(1)] = set(names)
for t in re.finditer(r'(?ms)^\s*(\w+)\s*(?:#\s*\((?:[^()]|\([^()]*\))*\))?\s*(\w+)\s*\(\s*(\..*?)\);', s):
    mod, inst, conn = t.group(1), t.group(2), t.group(3)
    if mod in ports:
        missing = ports[mod] - set(n.lower() for n in re.findall(r'\.(\w+)\s*\(', conn))
        if missing: print('M', mod, inst, ' '.join(sorted(missing))); bad += 1
print('%s: %d refusals' % (sys.argv[1], bad))
