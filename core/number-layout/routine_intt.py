"""Instruction sheet for GetIntegerText(P): the Pascal GetInteger(P) contract (bytes <= ' ' skipped, NUL
ends, one sign then spaces, digits, any other byte ends; the value wraps modulo 2^64 like the Pascal)
on the string routine's 16-byte window; bounded work: every byte phase (spaces, sign spaces, digits) stops after 65 bytes including the SIMD window (the value so far). Sizes as Delphi encodes them."""
from pathlib import Path
NAME = 'intt'
INC = ('inc rcx | add rcx, 1', [3, 4])
LOADB = ('movzx r10d, byte ptr [rcx]', 4)
# r11 bit 0 = negative; r9 = the phase budget in the byte loops; rax = p, then the value
hot = [
 ('xor r11d, r11d', 3), ('test rcx, rcx', 3), ('jz @zero', 2),
 ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('cmp byte ptr [rcx], 45', 3), ('je @negative', 6), ('lea r8, [rip + NumberSimdData]', 7),
 ('@unsigned:', 0), ('cmp eax, 4079', 5), ('ja @bytes', 6),
 ('movdqu xmm0, [rcx]', 4), ('movdqa xmm1, xmm0', 4), ('paddb xmm1, [bias]', 8), ('pcmpgtb xmm1, [threshold]', 8),
 ('pmovmskb r10d, xmm1', 5), ('not r10d', 3), ('tzcnt eax, r10d', 5),
 ('lea r10d, [rax - 1]', 4), ('cmp r10d, 14', 4), ('ja @window0or16', 6),
 ('psubb xmm0, [r8 + 32]', 6), ('cmp eax, 8', 3), ('ja @wideRows', 2),
 ('shl eax, 4', 3), ('pshufb xmm0, [r8 + rax + 576]', 11), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movd eax, xmm0', 4), ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@negate:', 0), ('neg rax', 3), ('ret', 1),
 ('@wideRows:', 0), ('shl eax, 4', 3), ('pshufb xmm0, [r8 + rax + 96]', 8), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6),
 ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movq rax, xmm0', 5), ('mov r10d, eax', 3), ('shr rax, 32', 4), ('imul r10, r10, 100000000', 7), ('add rax, r10', 3),
 ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@negative:', 0), ('inc rcx', 3), ('or r11d, 1', 4), ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('lea r8, [rip + NumberSimdData]', 7), ('jmp @unsigned', 2),
]
cold = [
 # ---- the window is full (16 digits): continue byte by byte (49 more: 65 digits in total); p = 0: the Pascal byte rules
 ('@window0or16:', 0), ('test eax, eax', 2), ('jz @bytes', 2),
 ('psubb xmm0, [r8 + 32]', 6), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6), ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movq rax, xmm0', 5), ('mov r10d, eax', 3), ('shr rax, 32', 4), ('imul r10, r10, 100000000', 7), ('add rax, r10', 3),
 ('add rcx, 16', 4), ('mov r9d, 49', 6), ('jmp @next', 2),
 # ---- digits byte by byte: rax = the value so far, r10 = the digit, r9 = the budget
 ('@digitLoop:', 0), ('imul rax, rax, 10', 4), ('add rax, r10', 3), ('inc rcx', 3), ('dec r9', 3), ('jz @signed', 2),
 ('@next:', 0), LOADB, ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('jbe @digitLoop', 2),
 ('@signed:', 0), ('mov r10, rax', 3), ('neg r10', 3), ('test r11b, 1', 4), ('cmovnz rax, r10', 4), ('ret', 1),
 # ---- nil, NUL, no digit, a phase beyond its budget: 0
 ('@zero:', 0), ('xor eax, eax', 2), ('ret', 1),
 # ---- the Pascal byte rules from the start: bytes <= ' ' (not NUL), one sign, spaces, digits
 ('@bytes:', 0), LOADB, ('mov r9d, 65', 6), ('test r11b, 1', 4), ('jnz @afterSign', 2),
 ('cmp r10d, 32', 4), ('ja @signByte', 2), ('xor eax, eax', 2),
 ('@spaces:', 0), ('test r10d, r10d', 3), ('jz @zero', 2), INC, ('dec r9', 3), ('jz @zero', 2), LOADB, ('cmp r10d, 32', 4), ('jbe @spaces', 2),
 ('mov r9d, 65', 6),
 ('@signByte:', 0), ('cmp r10d, 45', 4), ('jne @plusByte', 2), ('or r11d, 1', 4), ('jmp @signSkip', 2),
 ('@plusByte:', 0), ('cmp r10d, 43', 4), ('jne @firstDigit', 2),
 ('@signSkip:', 0), INC, LOADB,
 ('@afterSign:', 0), ('cmp r10d, 32', 4), ('jne @firstDigit', 2),
 ('@signSpaces:', 0), INC, ('dec r9', 3), ('jz @zero', 2), LOADB, ('cmp r10d, 32', 4), ('je @signSpaces', 2),
 ('@firstDigit:', 0), ('xor eax, eax', 2), ('mov r9d, 65', 6), ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('ja @zero', 2), ('jmp @digitLoop', 2),
]
LOOPS = {'@digitLoop': '@digitLoop', '@spaces': '@spaces', '@signSpaces': '@signSpaces'}
HARD_LOOPS = {'@digitLoop'}
LOOP64 = 0
NOGAP = None
HOT = {'@unsigned', '@wideRows'}
WARM = {'@negate', '@negative', '@window0or16', '@digitLoop', '@next', '@signed'}
DEEP = set()
MEM = {
 '[bias]': '[rip + NumberSimdData + NUMBER_BIAS]', '[rip + NOSIMD]': 'dword ptr [rip + NumberNoSimd]', '[threshold]': '[rip + NumberSimdData + NUMBER_THRESHOLD]',
 '[r8 + rax + 576]': '[r8 + rax + NUMBER_CTRLSHORT]', '[r8 + rax + 96]': '[r8 + rax + NUMBER_CTRLINT]',
 'cmp byte ptr [rcx], 45': "cmp byte ptr [rcx], '-'",
 '[r8 + 32]': '[r8 + NUMBER_ASCII0]', '[r8 + 48]': '[r8 + NUMBER_TEN]', '[r8 + 64]': '[r8 + NUMBER_HUNDRED]', '[r8 + 80]': '[r8 + NUMBER_TENTHOUSAND]',
}
COMMENT = {
 '@unsigned': (None, 'eax = P mod 4096 (after the sign), r11 = the sign, r8 = the table'),
 '@negate': ('        // ---- p digits (1..15): <= 8 digits, dword 0 = the value ----------------------------------------', ''),
 '@wideRows': ('        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------', ''),
 '@negative': ('        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------', 'the Pascal allows spaces after the sign: the byte path handles them (p = 0)'),
 '@window0or16': ('        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------', 'eax = p: 0 = no leading digit, 16 = full'),
 '@digitLoop': ('        // ---- digits byte by byte: rax = the value so far (wrapping like the Pascal), at most 64 here --', 'r10 = the digit, r9 = the budget'),
 '@next': (None, ''),
 '@signed': (None, 'the sign without a branch (the loop end leaves no room for a jump pair)'),
 '@zero': ('        // ---- nil, NUL, no digit, a phase beyond its 64-byte budget: 0 ---------------------------------', ''),
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
 'ja @wideRows': '9 .. 15 digits',
 'pshufb xmm0, [r8 + rax + 576]': '<= 8 digits right-aligned into lanes 0..7: dword 0 is the value',
 'movd eax, xmm0': 'the value',
 'jz @signed': 'the digit budget is spent: the value so far',
 'jz @zero': 'nil / NUL / the budget is spent',
 'mov r9d, 65': 'the budget of the next phase: 64 bytes beyond its first',
}
FILE = Path(__file__).resolve().parents[1] / 'mormot.core.base.asmx64.number.integer.inc'
FUNC = 'function GetInteger(P: PUtf8Char): PtrInt;'
