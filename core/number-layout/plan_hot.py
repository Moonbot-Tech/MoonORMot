"""Instruction sheet, hot section: the instruction list in source order with the sizes as Delphi encodes
them (labels end with ':'); the solver (solve.py) places the DS prefixes and the never-executed
gaps under the layout rules, the emitter (emit.py) writes the source. 'PAD' = one DS prefix byte on the
following instruction, 'NOPn' = a gap of n bytes after a ret/jmp. Rules checked by show(): every jump,
fused pair and ret inside one 16-byte block from the entry and not ending on a block boundary."""
import sys
FUSE = {'cmp', 'test', 'sub', 'add', 'and', 'or', 'xor', 'inc', 'dec'}
J = {'jz', 'jnz', 'je', 'jne', 'ja', 'jb', 'jae', 'jbe', 'jmp', 'ret', 'js', 'jns', 'jg', 'jl', 'jge', 'jle', 'jo', 'jno', 'jc', 'jnc'}
# the entry: the hot NUL paths carry no NOP; the quote/comma endings fall through into a copy of the
# value tail (one taken jump); the exponent endings jump out; the minus sign is handled out of line
hot = [
 ('test rcx, rcx', 3), ('jz @nil', 6), ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('xor r11d, r11d', 3),
 ('cmp byte ptr [rcx], 45', 3), ('je @negative', 6), ('mov dword ptr [rdx], 0', 6),
 ('@unsigned:', 0), ('cmp eax, 4079', 5), ('ja @bytes', 6), ('lea r8, [rip + NumberSimdData]', 7),
 ('movdqu xmm0, [rcx]', 4), ('movdqa xmm1, xmm0', 4), ('paddb xmm1, [bias]', 8), ('pcmpgtb xmm1, [threshold]', 8),
 ('pmovmskb r10d, xmm1', 5), ('not r10d', 3), ('tzcnt eax, r10d', 5),
 ('lea r9d, [rax - 1]', 4), ('cmp r9d, 14', 4), ('ja @window0or16', 6),
 ('movzx r9d, byte ptr [rcx + rax]', 5), ('cmp r9d, 46', 4), ('je @fraction', 2),
 # ---- integer, <= 8 digits: dword 0 of the product is M
 ('psubb xmm0, [r8 + 32]', 6), ('cmp eax, 8', 3), ('ja @integerWideRows', 2), ('shl eax, 4', 3),
 ('pshufb xmm0, [r8 + rax + 576]', 11), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('test r9d, r9d', 3), ('jnz @integerShortEnded', 2),
 ('@integerShortTail:', 0), ('test r11b, 1', 4), ('jnz @negateIntegerShort', 2), ('cvtdq2pd xmm0, xmm0', 4), ('ret', 1),
 ('@negateIntegerShort:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('xorpd xmm0, [sign]', 8), ('ret', 1),
 ('@integerShortEnded:', 0), ('or r9d, 32', 4), ('cmp r9d, 101', 4), ('je @integerShortExponent', 2),
 ('mov dword ptr [rdx], 1', 6), ('test r11b, 1', 4), ('jnz @negateIntegerShort', 2), ('cvtdq2pd xmm0, xmm0', 4), ('ret', 1),
 # ---- integer, 9..15 digits: dword 0 = H (lanes 0..7), dword 1 = L (lanes 8..15), M = H * 10^8 + L
 ('@integerWideRows:', 0), ('shl eax, 4', 3), ('pshufb xmm0, [r8 + rax + 96]', 8), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6), ('test r9d, r9d', 3), ('jnz @integerEnded', 2),
 ('@integerWide:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('movhlps xmm1, xmm0', 3), ('mulsd xmm0, [r8 + 104]', 6), ('addsd xmm0, xmm1', 4),
 ('test r11b, 1', 4), ('jnz @negateInteger', 2), ('ret', 1),
 ('@negateInteger:', 0), ('xorpd xmm0, [sign]', 8), ('ret', 1),
 ('@integerEnded:', 0), ('or r9d, 32', 4), ('cmp r9d, 101', 4), ('je @integerExponent', 2),
 ('mov dword ptr [rdx], 1', 6), ('cvtdq2pd xmm0, xmm0', 4), ('movhlps xmm1, xmm0', 3), ('mulsd xmm0, [r8 + 104]', 6), ('addsd xmm0, xmm1', 4),
 ('test r11b, 1', 4), ('jnz @negateInteger', 2), ('ret', 1),
 ('@integerExponent:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('movhlps xmm1, xmm0', 3), ('mulsd xmm0, [r8 + 104]', 6), ('addsd xmm0, xmm1', 4),
 ('shr eax, 4', 3), ('lea rcx, [rcx + rax + 1]', 5), ('xor r9d, r9d', 3), ('or r11d, 2', 4), ('jmp @fastExponent', 5),
 ('@integerShortExponent:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('shr eax, 4', 3), ('lea rcx, [rcx + rax + 1]', 5), ('xor r9d, r9d', 3), ('or r11d, 2', 4), ('jmp @fastExponent', 5),
 # ---- fraction: p integer digits, the dot, fraction digits up to lane 15
 ('@fraction:', 0), ('btr r10d, eax', 4), ('tzcnt r10d, r10d', 5),
 ('psubb xmm0, [r8 + 32]', 6), ('shl eax, 4', 3), ('pshufb xmm0, [r8 + rax + 336]', 11),
 ('cmp r10d, 9', 4), ('ja @fractionWideRows', 2), ('movzx r9d, byte ptr [rcx + r10]', 5), ('shl r10d, 4', 4),
 ('pshufb xmm0, [r8 + r10 + 560]', 11), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6), ('sub r10d, eax', 3), ('shr r10d, 1', 3),
 ('test r9d, r9d', 3), ('jnz @fractionShortEnded', 2),
 ('@fractionShortValue:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('divsd xmm0, [r8 + r10 + 712]', 10),
 ('test r11b, 1', 4), ('jnz @negateFraction', 2), ('ret', 1),
 ('@negateFraction:', 0), ('xorpd xmm0, [sign]', 8), ('ret', 1),
 ('@fractionShortEnded:', 0), ('or r9d, 32', 4), ('cmp r9d, 101', 4), ('je @fractionShortExponent', 2),
 ('mov dword ptr [rdx], 1', 6), ('cvtdq2pd xmm0, xmm0', 4), ('divsd xmm0, [r8 + r10 + 712]', 10), ('test r11b, 1', 4), ('jnz @negateFraction', 2), ('ret', 1),
 ('@fractionShortExponent:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('shr eax, 4', 3), ('shr r10d, 3', 4), ('lea rcx, [rcx + r10]', 4), ('lea rcx, [rcx + rax + 1]', 5),
 ('lea r9d, [r10 - 1]', 4), ('neg r9', 3), ('or r11d, 2', 4), ('jmp @fastExponent', 5),
 ('@fractionWideRows:', 0), ('cmp r10d, 16', 4), ('je @windowFraction', 6), ('movzx r9d, byte ptr [rcx + r10]', 5), ('shl r10d, 4', 4),
 ('pshufb xmm0, [r8 + r10 + 80]', 8), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6), ('sub r10d, eax', 3), ('shr r10d, 1', 3),
 ('test r9d, r9d', 3), ('jnz @fractionEnded', 2),
 ('@fractionValue:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('movhlps xmm1, xmm0', 3), ('mulsd xmm0, [r8 + 104]', 6), ('addsd xmm0, xmm1', 4),
 ('divsd xmm0, [r8 + r10 + 712]', 10), ('test r11b, 1', 4), ('jnz @negateFraction', 2), ('ret', 1),
 ('@fractionEnded:', 0), ('or r9d, 32', 4), ('cmp r9d, 101', 4), ('je @fractionExponent', 2),
 ('mov dword ptr [rdx], 1', 6), ('cvtdq2pd xmm0, xmm0', 4), ('movhlps xmm1, xmm0', 3), ('mulsd xmm0, [r8 + 104]', 6), ('addsd xmm0, xmm1', 4),
 ('divsd xmm0, [r8 + r10 + 712]', 10), ('test r11b, 1', 4), ('jnz @negateFraction', 2), ('ret', 1),
 ('@fractionExponent:', 0), ('cvtdq2pd xmm0, xmm0', 4), ('movhlps xmm1, xmm0', 3), ('mulsd xmm0, [r8 + 104]', 6), ('addsd xmm0, xmm1', 4),
 ('shr eax, 4', 3), ('shr r10d, 3', 4), ('lea rcx, [rcx + r10]', 4), ('lea rcx, [rcx + rax + 1]', 5),
 ('lea r9d, [r10 - 1]', 4), ('neg r9', 3), ('or r11d, 2', 4),
 # ---- the inline exponent: up to three digits, |scale| <= 22 converted here (the same operations as
 # the exact path of the converter: cvt of the same integer, one mulsd/divsd by the same power);
 # anything else (no digit, 4+ digits, |scale| > 22) goes to the generic exponent/converter
 ('@fastExponent:', 0), ('movzx eax, byte ptr [rcx]', 3), ('xor r8d, r8d', 3), ('cmp eax, 43', 3), ('je @feSkip', 2), ('cmp eax, 45', 3), ('jne @feFirst', 2), ('or r11d, 4', 4),
 ('@feSkip:', 0), ('inc r8d', 3), ('movzx eax, byte ptr [rcx + r8]', 5),
 ('@feFirst:', 0), ('sub eax, 48', 3), ('cmp eax, 9', 3), ('ja @feGeneric', 2),
 ('movzx r10d, byte ptr [rcx + r8 + 1]', 6), ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('ja @feEnd', 2),
 ('lea eax, [rax + rax * 4]', 3), ('lea eax, [r10 + rax * 2]', 4), ('movzx r10d, byte ptr [rcx + r8 + 2]', 6), ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('ja @feEnd', 2),
 ('lea eax, [rax + rax * 4]', 3), ('lea eax, [r10 + rax * 2]', 4), ('movzx r10d, byte ptr [rcx + r8 + 3]', 6), ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('jbe @feGeneric', 2),
 ('@feEnd:', 0), ('test r11b, 4', 4), ('jz @fePlus', 2), ('neg eax', 2),
 ('@fePlus:', 0), ('movsxd rax, eax', 3), ('add r9, rax', 3), ('xor r8d, r8d', 3), ('cmp r10d, -48', 4), ('setne r8b', 4), ('mov dword ptr [rdx], r8d', 3),
 ('lea rax, [r9 + 22]', 4), ('cmp rax, 44', 4), ('ja @feWide', 2),
 ('lea rax, [rip + POW10]', 7), ('test r9, r9', 3), ('jns @feMultiply', 2), ('neg r9', 3), ('divsd xmm0, [rax + r9 * 8 + 248]', 10), ('test r11b, 1', 4), ('jnz @negateFraction', 2), ('ret', 1),
 ('@feMultiply:', 0), ('mulsd xmm0, [rax + r9 * 8 + 248]', 10), ('test r11b, 1', 4), ('jnz @negateFraction', 2), ('ret', 1),
 ('@feGeneric:', 0), ('cvttsd2si r8, xmm0', 5), ('jmp @exponent', 5),
 ('@feWide:', 0), ('cvttsd2si r8, xmm0', 5), ('mov eax, r10d', 3), ('jmp @terminator', 5),
 # ---- the minus sign (out of line: positive numbers fall through at the entry)
 ('@negative:', 0), ('inc rcx', 3), ('or r11d, 1', 4), ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('mov dword ptr [rdx], 0', 6), ('jmp @unsigned', 5),
]
FAR = set()

def sizes_with_targets(seq, start=0, shorten=False):
    """Delphi grows short jumps; MoonCompiler relaxes initially near jumps.
    The latter uses (instruction+2)-target in x86/aasmcpu.pas, with the reverse
    signed-byte test. Borderline forward branches can have different fixpoints.
    Neither model substitutes for checking the linked binary."""
    size_of = {i: (5 if t.startswith('jmp ') else 6) if shorten else 2
        for i, (t, sz) in enumerate(seq) if not t.endswith(':') and t != 'PAD' and t.split()[0] in J and t.split()[0] != 'ret'}
    for _ in range(64):
        off = start; pend = 0; labels = {}
        rows = []
        for idx, (text, size) in enumerate(seq):
            if text == 'PAD': pend += 1; continue
            if text.endswith(':'): labels[text[:-1]] = off; continue
            mn = text.split()[0]
            if mn in J and mn != 'ret':
                size = size_of.get(idx, size)
            size += pend; pend = 0
            rows.append((idx, off, size)); off += size
        changed = False
        for idx, o, size in rows:
            text = seq[idx][0]; mn = text.split()[0]
            if mn in J and mn != 'ret':
                tgt = text.split()[1]
                if tgt in labels and tgt not in FAR:
                    d = labels[tgt] - (o + 2)
                    fits = -127 <= d <= 128 if shorten else -128 <= d <= 127
                    want = 2 if fits else (5 if mn == 'jmp' else 6)
                else:
                    want = 5 if mn == 'jmp' else 6
                old = size_of[idx]
                if (want < old if shorten else want > old): size_of[idx] = want; changed = True
        if not changed: break
    return size_of, labels

def show(seq, start=0, quiet=False, shorten=False):
    size_of, labels = sizes_with_targets(seq, start, shorten)
    off = start; pend = 0; prev = None; bad = []; rows = []
    for idx, (text, size) in enumerate(seq):
        if text == 'PAD': pend += 1; continue
        if text.endswith(':'): rows.append(f'      {text}'); prev = None; continue
        mn = text.split()[0]
        if idx in size_of: size = size_of[idx]
        size += pend; pend = 0
        note = ''
        if mn in J:
            s0 = prev[1] if prev and prev[0] in FUSE and mn not in ('jmp', 'ret') else off
            e = off + size
            if (s0 // 16) != ((e - 1) // 16) or e % 16 == 0:
                note = '  <<< VIOLATION'; bad.append(off)
        rows.append(f'{off:04x} {size:2d} +{off % 16:2d}..{(off + size - 1) % 16:2d}  {text}{note}')
        prev = (mn, off); off += size
    if not quiet: print(chr(10).join(rows))
    print('end', hex(off), 'violations', len(bad), [hex(b) for b in bad])
    return off
if __name__ == '__main__':
    show(hot, quiet='-q' in sys.argv)
