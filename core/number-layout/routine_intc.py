"""Instruction sheet for GetIntegerChecked(P, err): the current library's GetInteger(P, var err) contract
(commit 332c86b: bytes <= ' ' skipped, one sign then spaces, leading zeros free, at most 19 significant
digits, NUL must end the text, the magnitude must fit Int64 (2^63 when negative); err = 0 and the signed
value on success, else err = 1 and the magnitude read so far, unnegated) on the string routine's 16-byte
window; bounded work: spaces, sign spaces and leading zeros stop after 64 bytes (err = 1, 0)."""
from pathlib import Path
NAME = 'intc'
INC = ('inc rcx | add rcx, 1', [3, 4])
LOADB = ('movzx r10d, byte ptr [rcx]', 4)
E18 = 'mov r9, 1000000000000000000'
# r11 bit 0 = negative; r9 = the terminator (window) / 10^18 (loops); r8 = the table / the phase budget
hot = [
 ('xor r11d, r11d', 3), ('test rcx, rcx', 3), ('jz @zeroInvalid', 6),
 ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('cmp byte ptr [rcx], 45', 3), ('je @negative', 6), ('lea r8, [rip + NumberSimdData]', 7),
 ('@unsigned:', 0), ('cmp eax, 4079', 5), ('ja @bytes', 6),
 ('movdqu xmm0, [rcx]', 4), ('movdqa xmm1, xmm0', 4), ('paddb xmm1, [bias]', 8), ('pcmpgtb xmm1, [threshold]', 8),
 ('pmovmskb r10d, xmm1', 5), ('not r10d', 3), ('tzcnt eax, r10d', 5),
 ('lea r10d, [rax - 1]', 4), ('cmp r10d, 14', 4), ('ja @window0or16', 6),
 ('movzx r9d, byte ptr [rcx + rax]', 5), ('psubb xmm0, [r8 + 32]', 6), ('cmp eax, 8', 3), ('ja @wideRows', 2),
 ('shl eax, 4', 3), ('pshufb xmm0, [r8 + rax + 576]', 11), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movd eax, xmm0', 4), ('mov dword ptr [rdx], 0', 6), ('test r9d, r9d', 3), ('jnz @invalid', 2),
 ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@negate:', 0), ('neg rax', 3), ('ret', 1),
 ('@invalid:', 0), ('mov dword ptr [rdx], 1', 6), ('ret', 1),
 ('@wideRows:', 0), ('shl eax, 4', 3), ('pshufb xmm0, [r8 + rax + 96]', 8), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movq rax, xmm0', 5), ('mov r10d, eax', 3), ('shr rax, 32', 4), ('imul r10, r10, 100000000', 7), ('add rax, r10', 3),
 ('mov dword ptr [rdx], 0', 6), ('test r9d, r9d', 3), ('jnz @invalid', 2),
 ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@negative:', 0), ('inc rcx', 3), ('or r11d, 1', 4), ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('lea r8, [rip + NumberSimdData]', 7), ('jmp @unsigned', 2),
]
cold = [
 # ---- the window is full (16 digits): continue byte by byte; p = 0: the Pascal byte rules
 ('@window0or16:', 0), ('test eax, eax', 2), ('jz @bytes', 2),
 ('psubb xmm0, [r8 + 32]', 6), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6), ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movq rax, xmm0', 5), ('mov r10d, eax', 3), ('shr rax, 32', 4), ('imul r10, r10, 100000000', 7), ('add rax, r10', 3),
 ('add rcx, 15', 4), (E18, 10), ('test rax, rax', 3), ('jnz @next', 2), ('mov r8d, 50', 6), ('jmp @zeroLoop', 2),
 ('@invalidLoop:', 0), ('mov dword ptr [rdx], 1', 6), ('ret', 1),
 # ---- digits byte by byte: rax = the magnitude (19 significant digits at most), r10 = the digit
 ('@digitLoop:', 0), ('cmp rax, r9', 3), ('jae @invalidLoop', 2), ('imul rax, rax, 10', 4), ('add rax, r10', 3),
 ('@next:', 0), ('inc rcx', 3), LOADB, ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('jbe @digitLoop', 2),
 ('@ended:', 0), ('mov dword ptr [rdx], 0', 6), ('cmp r10d, -48', 4), ('jne @invalid', 2),
 ('mov r8d, r11d', 3), ('and r8d, 1', 4), ('mov r9, 9223372036854775807', 10), ('add r9, r8', 3), ('cmp rax, r9', 3), ('ja @invalid', 2),
 ('mov r10, rax', 3), ('neg r10', 3), ('test r11b, 1', 4), ('cmovnz rax, r10', 4), ('ret', 1),
 # ---- leading zeros while the magnitude is 0: free, at most 64 (r8 counts)
 ('@zeros:', 0), ('mov r8d, 65', 6),
 ('@zeroLoop:', 0), INC, LOADB, ('sub r10d, 48', 4), ('jnz @zeroEnd', 2), ('dec r8', 3), ('jnz @zeroLoop', 2), ('mov dword ptr [rdx], 1', 6), ('ret', 1),
 ('@zeroEnd:', 0), ('cmp r10d, 9', 4), ('jbe @digitLoop', 2), ('jmp @ended', 2),
 # ---- nil: 0 with err = 1
 ('@zeroInvalid:', 0), ('xor eax, eax', 2), ('mov dword ptr [rdx], 1', 6), ('ret', 1),
 # ---- the Pascal byte rules from the start: bytes <= ' ' (not NUL), one sign, spaces, digits
 ('@bytes:', 0), ('xor eax, eax', 2), LOADB, ('mov r8d, 65', 6), ('test r11b, 1', 4), ('jnz @afterSign', 2),
 ('cmp r10d, 32', 4), ('ja @signByte', 2), ('xor eax, eax', 2),
 ('@spaces:', 0), ('test r10d, r10d', 3), ('jz @zeroInvalid', 2), INC, ('dec r8', 3), ('jz @zeroInvalid', 2), LOADB, ('cmp r10d, 32', 4), ('jbe @spaces', 2),
 ('mov r8d, 65', 6),
 ('@signByte:', 0), ('cmp r10d, 45', 4), ('jne @plusByte', 2), ('or r11d, 1', 4), ('jmp @signSkip', 2),
 ('@plusByte:', 0), ('cmp r10d, 43', 4), ('jne @firstDigit', 2),
 ('@signSkip:', 0), INC, LOADB,
 ('@afterSign:', 0), ('cmp r10d, 32', 4), ('jne @firstDigit', 2),
 ('@signSpaces:', 0), INC, ('dec r8', 3), ('jz @zeroInvalid', 2), LOADB, ('cmp r10d, 32', 4), ('je @signSpaces', 2),
 ('@firstDigit:', 0), (E18, 10), ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('ja @zeroInvalid', 2), ('xor eax, eax', 2), ('test r10d, r10d', 3), ('jz @zeros', 2), ('jmp @digitLoop', 2),
]
LOOPS = {'@digitLoop': '@digitLoop', '@zeroLoop': '@zeroLoop', '@spaces': '@spaces', '@signSpaces': '@signSpaces'}
HARD_LOOPS = {'@digitLoop'}
LOOP64 = 0
NOGAP = None
HOT = {'@unsigned', '@wideRows'}
WARM = {'@negate', '@invalid', '@negative', '@window0or16', '@digitLoop', '@next', '@ended'}
DEEP = set()
MEM = {
 '[bias]': '[rip + NumberSimdData + NUMBER_BIAS]', '[rip + NOSIMD]': 'dword ptr [rip + NumberNoSimd]', '[threshold]': '[rip + NumberSimdData + NUMBER_THRESHOLD]',
 '[r8 + rax + 576]': '[r8 + rax + NUMBER_CTRLSHORT]', '[r8 + rax + 96]': '[r8 + rax + NUMBER_CTRLINT]',
 'cmp byte ptr [rcx], 45': "cmp byte ptr [rcx], '-'",
 '[r8 + 32]': '[r8 + NUMBER_ASCII0]', '[r8 + 48]': '[r8 + NUMBER_TEN]', '[r8 + 64]': '[r8 + NUMBER_HUNDRED]', '[r8 + 80]': '[r8 + NUMBER_TENTHOUSAND]',
}
COMMENT = {
 '@unsigned': (None, 'eax = P mod 4096 (after the sign), r11 = the sign, r8 = the table'),
 '@negate': ('        // ---- p digits (1..15), terminator at [rcx + p]: <= 8 digits, dword 0 = the magnitude ------', ''),
 '@invalid': (None, 'err = 1, rax = the magnitude read so far (unnegated, like the Pascal exit)'),
 '@wideRows': ('        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------', ''),
 '@negative': ('        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------', 'the Pascal allows spaces after the sign: the byte path handles them (p = 0)'),
 '@window0or16': ('        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------', 'eax = p: 0 = no leading digit, 16 = full'),
 '@invalidLoop': (None, 'err = 1, rax = the 19 digits (a copy next to the loop: a short jump keeps it in one window)'),
 '@digitLoop': ('        // ---- digits byte by byte: rax = the magnitude, 19 significant digits at most ----------------', 'r10 = the digit'),
 '@next': (None, ''),
 '@ended': (None, 'r10d = terminator - 48: NUL, then the Int64 range (2^63 allowed when negative)'),
 '@zeros': ('        // ---- leading zeros while the magnitude is 0: free, at most 64 (r8 counts) ------------------', ''),
 '@zeroLoop': (None, ''), '@zeroEnd': (None, 'r10d = byte - 48, nonzero'),
 '@zeroInvalid': ('        // ---- nil, NUL or no digit before any digit, a phase beyond its 64-byte budget: 0 with err = 1 ----', ''),
 '@bytes': ('        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --', 'rcx = the first byte (after a consumed -: spaces only); each phase at most 64 bytes'),
 '@spaces': (None, 'bytes <= 32 except NUL'),
 '@signByte': (None, ''), '@plusByte': (None, ''),
 '@signSkip': (None, 'past the sign (not charged to the budget)'), '@signSpaces': (None, 'spaces after the sign: at most 64'), '@afterSign': (None, ''),
 '@firstDigit': (None, ''),
}
INS = {
 'or eax, [rip + NOSIMD]': '4096 on a CPU without SSSE3: the byte path then (the same contract)',
 'ja @bytes': 'the window would cross the page: byte loops from the start',
 'pcmpgtb xmm1, [threshold]': 'digit lanes',
 'not r10d': 'non-digit lanes; bits 16..31 are set',
 'bsf eax, r10d': 'p = leading digit count, 0..16',
 'ja @window0or16': 'p = 0: no leading digit; p = 16: the window is full',
 'movzx r9d, byte ptr [rcx + rax]': 'the byte after the digits: NUL for err = 0',
 'ja @wideRows': '9 .. 15 digits',
 'pshufb xmm0, [r8 + rax + 576]': '<= 8 digits right-aligned into lanes 0..7: dword 0 is the magnitude',
 'movd eax, xmm0': 'the magnitude',
 'jnz @invalid': 'not NUL: err = 1, the magnitude',
 'jae @invalidLoop': 'a 20th significant digit: err = 1, the 19 digits',
 'mov r9, 1000000000000000000': '10^18: 19 significant digits kept',
 'ja @invalid': 'beyond Int64: err = 1, the magnitude',
 'jz @zeroInvalid': 'NUL before a digit / the budget is spent: err = 1, 0',
 'mov r8d, 65': 'the budget of the next phase: 64 bytes beyond its first',
}
FILE = Path(__file__).resolve().parents[1] / 'mormot.core.base.asmx64.number.integer.inc'
FUNC = 'function GetInteger(P: PUtf8Char; var err: integer): PtrInt;'
