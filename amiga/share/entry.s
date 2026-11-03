| Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the block transport and the changes; the a314fs code
| it is built on (Niklas Ekstrom, a314 repository) is CC0-1.0.
| SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 AND CC0-1.0
| amiga/share/entry.s -- the first code of the handler's segment: DOS starts the process here (GlobVec = -1)
	.text
	.globl	_entry
_entry:
	jmp	_start
