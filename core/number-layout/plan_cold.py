"""Instruction sheet for the cold section (after the hot section of plan_hot.py): the byte loops, the
exponent, the converter. Same rules: every jump/fused pair/ret inside one 16-byte block from the
entry; the loops inside one 32-byte window from the entry (no PAD, no NOP inside a loop); NOPs
only in never-executed gaps after ret/jmp."""

E18 = 'mov r10, 1000000000000000000'
LOOPS = {'@spaces': '@spaces', '@integerLoop': '@integerLoop', '@fractionLoop': '@fractionLoop',
         '@exponentLoop': '@exponentLoop', '@stripLoop': '@stripLoop', '@rangeStrip': '@rangeStrip',
         '@integerDrop': '@integerDrop', '@fractionDropLoop': '@fractionDropLoop', '@integerZeros': '@integerZeros',
         '@fractionZeros': '@fractionZeros', '@exponentZeros': '@exponentZeros'}   # label -> back-edge target
# an entry ('a | b', [sa, sb]) offers alternative encodings of the same work (0 uop difference); the
# solver picks one; the exit tests are byte-wide (cmp al, ...), so every digit-test site may use either
# the 32-bit or the AL form of sub/cmp
SUB = ('sub eax, 48 | sub al, 48', [3, 2])
CMP9 = ('cmp eax, 9 | cmp al, 9', [3, 2])
INC = ('inc rcx | add rcx, 1', [3, 4])
LOAD = ('movzx eax, byte ptr [rcx]', 3)
cold = [
 # ---- 15 digits with the dot, the fraction runs past the window: eax = p * 16, xmm0 = digits, r8 = table
 ('@windowFraction:', 0), ('pshufb xmm0, [r8 + 336]', 10), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6), ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movq r8, xmm0', 5), ('mov r9d, r8d', 3), ('shr r8, 32', 4), ('imul r9, r9, 100000000', 7), ('add r8, r9', 3),
 ('shr eax, 4', 3), ('lea r9, [rax - 15]', 4), ('add rcx, 16', 4), ('or r11d, 2', 4), (E18, 10),
 ('movq xmm3, rdx', 5), ('movq xmm4, r11', 5), ('mov r11, r8', 3), ('mov rdx, r9', 3),
 ('test r8, r8', 3), ('jz @fractionZeros', 2), ('jmp @fractionNext', 2),
 # ---- the window is full (16 digits) or has no leading digit
 ('@window0or16:', 0), ('test eax, eax', 2), ('jz @bytes', 2),
 ('psubb xmm0, [r8 + 32]', 6), ('pmaddubsw xmm0, [r8 + 48]', 7), ('pmaddwd xmm0, [r8 + 64]', 6), ('packssdw xmm0, xmm0', 4), ('pmaddwd xmm0, [r8 + 80]', 6),
 ('movq r8, xmm0', 5), ('mov eax, r8d', 3), ('shr r8, 32', 4), ('imul rax, rax, 100000000', 7), ('add r8, rax', 3),
 ('add rcx, 16', 4), ('xor r9d, r9d', 3), ('or r11d, 10', 4), (E18, 10), ('jmp @integerFirst', 2),
 # ---- nil text (reached by the entry jz only)
 ('@nil:', 0), ('mov dword ptr [rdx], 1', 6), ('pxor xmm0, xmm0', 4), ('ret', 1),
 # ---- byte scan from the start: no window (page end) or no leading digit (spaces, '+', '.')
 ('@bytes:', 0), ('xor r8d, r8d', 3), ('xor r9d, r9d', 3), (E18, 10), ('test r11b, 1', 4), ('jnz @integerFirst', 2),
 LOAD, ('cmp eax, 32', 3), ('jne @signByte', 2),
 ('@spaces:', 0), INC, ('inc r8', 3), ('cmp r8, 64', 4), ('ja @garbageZero', 2), LOAD, ('cmp eax, 32', 3), ('je @spaces', 2),
 ('xor r8d, r8d', 3),
 ('@signByte:', 0), ('cmp eax, 45', 3), ('jne @plusSign', 2), ('or r11d, 1', 4), INC, ('jmp @integerFirst', 2),
 ('@plusSign:', 0), ('cmp eax, 43', 3), ('jne @integerFirst', 2), INC,
 # ---- integer digits: rcx = the first byte; exit with rcx = the terminator, al = byte - 48
 ('@integerFirst:', 0), LOAD, SUB, CMP9, ('ja @integerDone', 2), ('or r11d, 2', 4),
 ('test r8, r8', 3), ('jz @integerMaybeZero', 2),
 ('@integerLoop:', 0), INC, ('cmp r8, r10', 3), ('jae @integerDrop', 2), ('lea r8, [r8 + r8 * 4]', 4), ('lea r8, [rax + r8 * 2]', 4),
 LOAD, SUB, CMP9, ('jbe @integerLoop', 2),
 # ---- after the integer digits: rcx moves past the terminator ('.' or another byte)
 ('@integerDone:', 0), INC,
 ('@integerDone1:', 0), ('cmp al, 254', 2), ('jne @afterDigits', 2), LOAD, INC, SUB, CMP9, ('ja @afterDigits', 2), ('or r11d, 2', 4),
 ('movq xmm3, rdx', 5), ('movq xmm4, r11', 5), ('mov r11, r8', 3),
 ('test r8, r8', 3), ('jz @fractionMaybeZero', 2), ('mov rdx, r9', 3),
 ('@fractionLoop:', 0), ('cmp r8, r10', 3), ('jae @fractionDrop', 2), ('lea r8, [r8 + r8 * 4]', 4), ('lea r8, [rax + r8 * 2]', 4), ('dec r9 | sub r9, 1', [3, 4]),
 ('test eax, eax', 2), ('cmovnz r11, r8', 4), ('cmovnz rdx, r9', 4),
 ('@fractionNext:', 0), LOAD, INC, SUB, CMP9, ('jbe @fractionLoop', 2),
 ('@fractionExit:', 0), ('mov r8, r11', 3), ('mov r9, rdx', 3), ('movq r11, xmm4', 5), ('movq rdx, xmm3', 5),
 # ---- after the digits (rcx = past the terminator, al = terminator - 48): an exponent or the end
 # ---- after the digits (rcx = past the terminator, al = terminator - 48): an exponent or the end
 ('@afterDigits:', 0), ('mov r10d, eax', 3), ('or r10b, 32', 4), ('cmp r10b, 53', 4), ('jne @terminator', 2),
 ('@exponent:', 0), LOAD, ('cmp eax, 43', 3), ('je @exponentSign', 2), ('cmp eax, 45', 3), ('jne @exponentFirst', 2), ('or r11d, 4', 4),
 ('@exponentSign:', 0), INC, LOAD,
 ('@exponentFirst:', 0), SUB, CMP9, ('ja @exponentNone', 2), ('xor r10d, r10d', 3), ('test eax, eax', 2), ('jz @exponentZeros', 2),
 ('@exponentLoop:', 0), INC, ('imul r10, r10, 10', 4), ('jo @rangeM', 2), ('add r10, rax', 3), ('jo @exponentLimit', 2),
 ('@exponentNext:', 0), LOAD, SUB, CMP9, ('jbe @exponentLoop', 2),
 ('@exponentEnd:', 0), ('mov rcx, r10', 3), ('neg rcx', 3), ('test r11b, 4', 4), ('cmovnz r10, rcx', 4), ('add r9, r10', 3), ('jo @scaleOverflow', 2),
 ('@terminator:', 0), ('xor r10d, r10d', 3), ('test r11b, 2', 4), ('jz @exponentNone', 2), ('cmp al, 208', 2), ('setne r10b', 4),
 # ---- convert: r8 = M, r9 = scale, r10d = err, r11 bit 0 = sign
 ('@convert:', 0), ('mov dword ptr [rdx], r10d', 3), ('test r8, r8', 3), ('jz @zero', 2), ('mov rax, r8', 3), ('test r9, r9', 3), ('jg @inexact', 2),
 ('shr rax, 53', 4), ('jnz @inexact', 2), ('lea rax, [r9 + 22]', 4), ('cmp rax, 44', 4), ('ja @inexact', 2),
 ('@exact:', 0), ('cvtsi2sd xmm0, r8', 5), ('lea rax, [rip + POW10]', 7), ('test r9, r9', 3), ('jns @exactMultiply', 2),
 ('neg r9', 3), ('divsd xmm0, [rax + r9 * 8 + 248]', 10),
 ('@done:', 0), ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@exactMultiply:', 0), ('mulsd xmm0, [rax + r9 * 8 + 248]', 10), ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@negate:', 0), ('xorpd xmm0, [sign]', 8), ('ret', 1),
 ('@exponentNone:', 0), ('mov r10d, 1', 6), ('jmp @convert', 2),
 # ---- inexact: strip the trailing decimal zeros, then the recoveries and the Pascal arithmetic
 ('@inexact:', 0), ('mov rcx, -3689348814741910323', 10), ('mov r10, 1844674407370955161', 10),
 ('mov rax, r8', 3), ('imul rax, rcx', 4), ('ror rax, 1', 3), ('cmp rax, r10', 3), ('jbe @stripLoop', 2),
 ('@stripped:', 0), ('mov rax, r8', 3), ('shr rax, 53', 4), ('jnz @wideMantissa', 2), ('lea rax, [r9 + 22]', 4), ('cmp rax, 44', 4), ('jbe @exact', 2),
 ('test r9, r9', 3), ('jns @recover', 2),
 ('cvtsi2sd xmm0, r8', 5), ('lea rax, [rip + POW10]', 7), ('lea rcx, [r9 + 31]', 4), ('cmp rcx, 62', 4), ('ja @far', 2),
 ('mulsd xmm0, [rax + rcx * 8]', 5), ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@stripLoop:', 0), ('mov r8, rax', 3), ('inc r9', 3), ('mov rax, r8', 3), ('imul rax, rcx', 4), ('ror rax, 1', 3), ('cmp rax, r10', 3), ('jbe @stripLoop', 2), ('jmp @stripped', 2),
 ('@recover:', 0), ('lea rax, [r9 - 23]', 4), ('cmp rax, 14', 4), ('ja @pascal', 2),
 ('cvtsi2sd xmm0, r8', 5), ('lea rcx, [rip + POW10]', 7), ('movsd xmm1, [rcx + rax * 8 + 256]', 9), ('mulsd xmm1, xmm0', 4),
 ('comisd xmm1, [rip + EXACTINTEGER]', 8), ('ja @pascalScale', 2), ('mulsd xmm1, [rcx + 424]', 8), ('movapd xmm0, xmm1', 4), ('jmp @done', 2),
 ('@wideMantissa:', 0), ('lea rax, [r9 - 1]', 4), ('cmp rax, 2', 4), ('ja @pascal', 2), ('lea rax, [rip + POW10]', 7), ('cvttsd2si rcx, [rax + r9 * 8 + 248]', 10),
 ('mov r10, rdx', 3), ('mov rax, r8', 3), ('mul rcx', 3), ('mov rcx, rdx', 3), ('mov rdx, r10', 3), ('test rcx, rcx', 3), ('jnz @pascal', 2), ('mov r8, rax', 3), ('xor r9d, r9d', 3),
 ('@pascal:', 0), ('test r8, r8', 3), ('js @pascalBig', 2), ('cvtsi2sd xmm0, r8', 5),
 ('@pascalScale:', 0), ('lea rax, [rip + POW10]', 7), ('lea rcx, [r9 + 31]', 4), ('cmp rcx, 62', 4), ('ja @far', 2), ('mulsd xmm0, [rax + rcx * 8]', 5), ('test r11b, 1', 4), ('jnz @negate', 2), ('ret', 1),
 ('@pascalBig:', 0), ('mov rax, r8', 3), ('shr rax, 1', 3), ('mov ecx, r8d', 3), ('and ecx, 1', 3), ('or rax, rcx', 3), ('cvtsi2sd xmm0, rax', 5), ('addsd xmm0, xmm0', 4), ('jmp @pascalScale', 2),
 ('@far:', 0), ('lea rcx, [r9 - 32]', 4), ('cmp rcx, 256', 7), ('ja @farEdge', 2),
 ('mov ecx, r9d', 3), ('shr ecx, 5', 3), ('mov r10d, r9d', 3), ('and r10d, 31', 4), ('movsd xmm1, [rax + rcx * 8 + 520]', 9), ('mulsd xmm1, [rax + r10 * 8 + 248]', 10), ('mulsd xmm0, xmm1', 4), ('jmp @done', 2),
 ('@farEdge:', 0), ('test r9, r9', 3), ('jns @huge', 2),
 ('@wideNegative:', 0), ('mov ecx, r9d', 3), ('neg ecx', 2), ('cmp r9, -307', 7), ('jl @tiny', 2), ('mov r10d, ecx', 3), ('shr ecx, 5', 3), ('and r10d, 31', 4),
 ('movsd xmm1, [rax + rcx * 8 + 608]', 9), ('divsd xmm1, [rax + r10 * 8 + 248]', 10), ('mulsd xmm0, xmm1', 4), ('jmp @done', 2),
 ('@tiny:', 0), ('cmp r9, -342', 7), ('jl @range', 2), ('lea ecx, [r9 + 342]', 7), ('cmp r9, -324', 7), ('jg @tinyScale', 2), ('cvttsd2si r10, [rax + rcx * 8 + 248]', 10),
 ('mov rcx, rdx', 3), ('mov rax, [rip + UNDERFLOW]', 7), ('xor edx, edx', 2), ('div r10', 3), ('mov rdx, rcx', 3), ('cmp r8, rax', 3), ('jbe @range', 2), ('lea rax, [rip + POW10]', 7),
 ('@tinyScale:', 0), ('lea ecx, [r9 + 160]', 7), ('neg ecx', 2), ('mov r10d, ecx', 3), ('shr ecx, 5', 3), ('and r10d, 31', 4),
 ('movsd xmm1, [rax + rcx * 8 + 608]', 9), ('divsd xmm1, [rax + r10 * 8 + 248]', 10), ('mulsd xmm0, [rax + 648]', 8), ('mulsd xmm0, xmm1', 4), ('maxsd xmm0, [rip + MINBITS]', 8), ('jmp @done', 2),
 ('@huge:', 0), ('cmp r9, 308', 7), ('ja @range', 2), ('lea ecx, [r9 - 289]', 7), ('je @lastPower', 2), ('cvttsd2si r10, [rax + rcx * 8 + 248]', 10),
 ('mov rcx, rdx', 3), ('mov rax, [rip + OVERFLOW]', 7), ('xor edx, edx', 2), ('div r10', 3), ('mov rdx, rcx', 3), ('cmp r8, rax', 3), ('ja @range', 2), ('lea rax, [rip + POW10]', 7), ('jmp @hugeScale', 2),
 ('@lastPower:', 0), ('cmp r8, 1', 4), ('ja @range', 2),
 ('@hugeScale:', 0), ('mov ecx, r9d', 3), ('shr ecx, 5', 3), ('mov r10d, r9d', 3), ('and r10d, 31', 4), ('movsd xmm1, [rax + rcx * 8 + 520]', 9), ('mulsd xmm1, [rax + r10 * 8 + 248]', 10),
 ('mulsd xmm1, [rip + INVSCALE]', 8), ('mulsd xmm0, xmm1', 4), ('minsd xmm0, [rip + MAXSCALED]', 8), ('mulsd xmm0, [rip + SCALE]', 8), ('jmp @done', 2),
 ('@zero:', 0), ('pxor xmm0, xmm0', 4), ('jmp @done', 2),
 # ---- the capped phases that do not grow the mantissa (out of line): leading zeros of the integer,
 # the fraction and the exponent; beyond 64 bytes the text ends like a garbage terminator (err = 1)
 ('@windowZeros:', 0), ('mov r8d, 16', 6), ('jmp @integerZeros', 2),
 ('@integerMaybeZero:', 0), ('test eax, eax', 2), ('jnz @integerLoop', 2), ('test r11b, 8', 4), ('jnz @windowZeros', 2),
 ('@integerZeros:', 0), INC, ('inc r8', 3), ('cmp r8, 64', 4), ('ja @garbageZero', 2), LOAD, ('sub al, 48', 2), ('jz @integerZeros', 2),
 ('xor r8d, r8d', 3), ('jmp @integerFirst', 2),
 ('@fractionMaybeZero:', 0), ('mov rdx, r9', 3), ('test eax, eax', 2), ('jnz @fractionLoop', 2), ('dec r9', 3),
 ('@fractionZeros:', 0), LOAD, INC, ('sub al, 48', 2), ('jnz @fractionZerosEnd', 2), ('dec r9', 3), ('cmp r9, -64', 4), ('jge @fractionZeros', 2),
 ('mov eax, 1', 5), ('jmp @fractionExit', 2),
 ('@fractionZerosEnd:', 0), ('dec rcx', 3), ('jmp @fractionNext', 2),
 ('@exponentZeros:', 0), INC, ('inc r10', 3), ('cmp r10, 64', 4), ('ja @garbageExp', 2), LOAD, ('sub al, 48', 2), ('jz @exponentZeros', 2),
 ('xor r10d, r10d', 3), ('jmp @exponentNext', 2),

 # ---- an integer digit beyond the 19 kept: it raises the scale (rcx = the next byte)
 ('@integerDrop:', 0), ('inc r9 | add r9, 1', [3, 4]), ('cmp r9, 64', 4), ('ja @garbageInt', 2), LOAD, INC, SUB, CMP9, ('jbe @integerDrop', 2), ('jmp @integerDone1', 2),
 ('@fractionDrop:', 0), ('mov r10d, 64', 6),
 ('@fractionDropLoop:', 0), LOAD, INC, SUB, CMP9, ('ja @fractionExit', 2), ('dec r10', 3), ('jnz @fractionDropLoop', 2), ('mov eax, 1', 5), ('jmp @fractionExit', 2),
 # ---- beyond a cap: the text ends here like a garbage terminator (err = 1, the value so far)
 ('@garbageZero:', 0), ('xor r8d, r8d', 3),
 ('@garbageInt:', 0), ('mov eax, 1', 5), ('jmp @afterDigits', 2),
 # ---- range error (err = 1, the value is the stripped mantissa))
 ('@exponentLimit:', 0), ('mov rax, r10', 3), ('shl rax, 1', 3), ('jnz @rangeM', 2),
 ('test r11b, 4', 4), ('jz @rangeM', 2), ('jmp @exponentNext', 2),
 ('@scaleOverflow:', 0), ('test r8, r8', 3), ('jz @terminator', 2),
 ('@rangeM:', 0), ('mov dword ptr [rdx], 1', 6), ('test r8, r8', 3), ('jz @zero', 2), ('mov rcx, -3689348814741910323', 10), ('mov r10, 1844674407370955161', 10),
 ('@rangeStrip:', 0), ('mov rax, r8', 3), ('imul rax, rcx', 4), ('ror rax, 1', 3), ('cmp rax, r10', 3), ('ja @range', 2), ('mov r8, rax', 3), ('jmp @rangeStrip', 2),
 ('@range:', 0), ('mov dword ptr [rdx], 1', 6), ('xor r9d, r9d', 3), ('jmp @pascal', 5),
 ('@garbageExp:', 0), ('mov eax, 1', 5), ('jmp @terminator', 2),
]
