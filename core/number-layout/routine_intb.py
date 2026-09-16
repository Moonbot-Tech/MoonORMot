"""Instruction sheet for GetIntegerBounded(P, PEnd): the Pascal GetInteger(P, PEnd) contract (bytes below
PEnd only, exclusive end; bytes <= ' ' skipped, one sign then spaces, digits, any other byte or PEnd
ends; the value wraps modulo 2^64 like the Pascal) on the string routine's 16-byte window: the digit
count is clamped to PEnd - P, the rows give the value of up to 15 digits, 16 digits continue byte by
byte. Sizes as Delphi encodes them; solve.py / emit.py / layout.py place, write and check it."""
from pathlib import Path
NAME = 'intb'
INC = ('inc rcx | add rcx, 1', [3, 4])
LOADB = ('movzx r10d, byte ptr [rcx]', 4)
# r11 bit 0 = negative; r9 = PEnd - P (the remaining length); rax = p, then the value
hot = [
 ('mov r9, rdx', 3), ('test rcx, rcx', 3), ('jz @zero', 2), ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('sub r9, rcx', 3), ('jle @zero', 2),
 ('xor r11d, r11d', 3), ('cmp byte ptr [rcx], 45', 3), ('je @negative', 6), ('lea r8, [rip + NumberSimdData]', 7),
 ('@unsigned:', 0), ('cmp eax, 4079', 5), ('ja @bytes', 6),
 ('movdqu xmm0, [rcx]', 4), ('movdqa xmm1, xmm0', 4), ('paddb xmm1, [bias]', 8), ('pcmpgtb xmm1, [threshold]', 8),
 ('pmovmskb r10d, xmm1', 5), ('not r10d', 3), ('tzcnt eax, r10d', 5),
 ('cmp rax, r9', 3), ('cmova rax, r9', 4),
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
 ('@negative:', 0), ('inc rcx', 3), ('or r11d, 1', 4), ('mov eax, ecx', 2), ('and eax, 4095', 5), ('or eax, [rip + NOSIMD]', 6), ('lea r8, [rip + NumberSimdData]', 7), ('dec r9', 3), ('jnz @unsigned', 2), ('xor eax, eax', 2), ('ret', 1),
]
cold = [
 # ---- the window is full (16 digits within the bound): continue byte by byte; p = 0: the Pascal byte rules
 ('@window0or16:', 0), ('test eax, eax', 2), ('jz @bytes', 2),
 ('psubb xmm0, [r8 + 32]', 6), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6), ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movq rax, xmm0', 5), ('mov r10d, eax', 3), ('shr rax, 32', 4), ('imul r10, r10, 100000000', 7), ('add rax, r10', 3),
 ('add rcx, 16', 4), ('jmp @next', 2),
 # ---- digits byte by byte up to PEnd: rax = the value so far, r10 = the digit
 ('@digitLoop:', 0), ('imul rax, rax, 10', 4), ('add rax, r10', 3), ('inc rcx', 3),
 ('@next:', 0), ('cmp rcx, rdx', 3), ('je @signed', 2), LOADB, ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('jbe @digitLoop', 2),
 ('@signed:', 0), ('mov r10, rax', 3), ('neg r10', 3), ('test r11b, 1', 4), ('cmovnz rax, r10', 4), ('ret', 1),
 # ---- the Pascal byte rules from the start: bytes <= ' ' (not NUL), one sign, spaces, digits
 ('@zero:', 0), ('xor eax, eax', 2), ('ret', 1),
 ('@bytes:', 0), LOADB, ('test r11b, 1', 4), ('jnz @afterSign', 2),
 ('cmp r10d, 32', 4), ('ja @signByte', 2), ('xor eax, eax', 2),
 ('@spaces:', 0), ('test r10d, r10d', 3), ('jz @zero', 2), INC, ('cmp rcx, rdx', 3), ('je @zero', 2), LOADB, ('cmp r10d, 32', 4), ('jbe @spaces', 2),
 ('@signByte:', 0), ('cmp r10d, 45', 4), ('jne @plusByte', 2), ('or r11d, 1', 4), ('jmp @signSpaces', 2),
 ('@plusByte:', 0), ('cmp r10d, 43', 4), ('jne @firstDigit', 2),
 ('@signSpaces:', 0), INC, ('cmp rcx, rdx', 3), ('je @zero', 2), LOADB,
 ('@afterSign:', 0), ('cmp r10d, 32', 4), ('je @signSpaces', 2),
 ('@firstDigit:', 0), ('xor eax, eax', 2), ('sub r10d, 48', 4), ('cmp r10d, 9', 4), ('ja @zero', 2), ('jmp @digitLoop', 2),
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
 '@unsigned': (None, 'eax = P mod 4096 (after the sign), r9 = PEnd - P > 0, r11 = the sign, r8 = the table'),
 '@negate': ('        // ---- p digits (1..15, within the bound): <= 8 digits, dword 0 = the value ----------------', ''),
 '@wideRows': ('        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------', ''),
 '@negative': ('        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------', 'the Pascal allows spaces after the sign: the byte path handles them (p = 0)'),
 '@window0or16': ('        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------', 'eax = p: 0 = no leading digit, 16 = full'),
 '@digitLoop': ('        // ---- digits byte by byte up to PEnd: rax = the value so far (wrapping like the Pascal) ---', 'r10 = the digit (IMUL keeps the bound check inside one block)'),
 '@next': (None, 'rcx = the next byte, or PEnd'),
 '@signed': (None, 'the sign without a branch (the loop end leaves no room for a jump pair)'),
 '@bytes': ('        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --', 'rcx = the first byte (after a consumed -: spaces only)'),
 '@spaces': (None, 'bytes <= 32 except NUL'),
 '@signByte': (None, ''), '@plusByte': (None, ''),
 '@signSpaces': (None, 'spaces after the sign'), '@afterSign': (None, ''),
 '@firstDigit': (None, ''),
 '@zero': ('        // ---- nil, an empty range, no digit: 0 ------------------------------------------------------', ''),
}
INS = {
 'or eax, [rip + NOSIMD]': '4096 on a CPU without SSSE3: the byte path then (the same contract)',
 'jle @zero': 'P >= PEnd',
 'ja @bytes': 'the window would cross the page: byte loops from the start',
 'pcmpgtb xmm1, [threshold]': 'digit lanes',
 'not r10d': 'non-digit lanes; bits 16..31 are set',
 'bsf eax, r10d': 'p = leading digit count, 0..16',
 'cmova rax, r9': 'p = min(p, PEnd - P): digits at or beyond PEnd do not count',
 'ja @window0or16': 'p = 0: no leading digit; p = 16: the window is full',
 'ja @wideRows': '9 .. 15 digits',
 'pshufb xmm0, [r8 + rax + 576]': '<= 8 digits right-aligned into lanes 0..7: dword 0 is the value',
 'movd eax, xmm0': 'the value',
 'je @signed': 'PEnd reached',
 'jz @zero': 'nil / NUL',
}
FILE = Path(__file__).resolve().parents[1] / 'mormot.core.base.asmx64.number.integer.inc'
FUNC = 'function GetInteger(P, PEnd: PUtf8Char): PtrInt;'
