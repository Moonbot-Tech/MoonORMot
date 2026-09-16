// GetExtended: complete string-to-Double parser, without calls or a stack frame.
// - Nearest rounding is the value contract: retain 19 significant digits, with the
//   original Pascal's 15-digit accuracy. Short exact mantissas/scales use one MULSD/DIVSD;
//   long tails do not pay for arbitrary-precision decimal conversion.
// - Leading zeroes do not consume significant digits; dropped integer digits raise
//   the scale. Trailing zeroes are canonicalized so spellings and scan paths agree.
// - NUL after a number succeeds; other terminators return the value so far and err=1.
//   Optional spaces/sign and a leading/trailing dot follow the string grammar.
// - String -0 keeps its sign. A zero mantissa accepts any representable exponent;
//   overflowing the exponent itself still fails. Nonzero range errors return the
//   canonical mantissa with err=1.
// - Nongrowing byte phases are capped at 64 bytes, including bytes already consumed
//   in the window. Crossing a cap acts as an invalid terminator.
// - The SSSE3 window stays in one 4 KiB page; NumberNoSimd forces the SSE2 byte path.
//   It may load bytes beyond NUL in that page. Scalar continuation keeps the same bits.
// Win64: rcx=P, rdx=@err. SysV: rdi replaces rcx; rsi is moved to rdx for the shared
// MUL/DIV register plan. r8=mantissa, r9=scale, r10=scratch/error; r11 flags:
// bit 0 sign, bit 1 digit seen, bit 2 negative exponent, bit 3 consumed full window.
// The fractional checkpoint temporarily parks err/flags in xmm3/xmm4.
// Generated from number-layout/routine_string.py; see mormot.core.base.asmx64.layout.md.

function GetExtended(P: PUtf8Char; out err: integer): TSynExtended;
{$ifdef FPC} nostackframe; assembler; asm {$else} asm .noframe {$endif}
{$ifdef SYSVABI}
        mov     rdx, rsi
        test    rdi, rdi
        jz      @nil
        mov     eax, edi
        and     eax, 4095
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        xor     r11d, r11d
        cmp     byte ptr [rdi], '-'
        je      @negative
         db      $3e    // PAD: DS prefix, no uop
        mov     dword ptr [rdx], 0       // err for a NUL terminator; the other endings rewrite it
@unsigned:                               // eax = P mod 4096 (after the sign), err = 0 stored
        cmp     eax, 4079
        ja      @bytes                   // the window would cross the page: byte loops from the start
        lea     r8, [rip + NumberSimdData]
        movdqu  xmm0, [rdi]
        movdqa  xmm1, xmm0
        paddb   xmm1, [rip + NumberSimdData + NUMBER_BIAS]
        pcmpgtb xmm1, [rip + NumberSimdData + NUMBER_THRESHOLD]// digit lanes
         db      $3e    // PAD: DS prefix, no uop
        pmovmskb r10d, xmm1
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        not     r10d                     // non-digit lanes; bits 16..31 are set
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     eax, r10d                // p = leading digit count, 0..16
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        lea     r9d, [rax - 1]
        cmp     r9d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
         db      $3e    // PAD: DS prefix, no uop
        movzx   r9d, byte ptr [rdi + rax]// the byte after the integer digits
        cmp     r9d, '.'
        je      @fraction
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @integerWideRows         // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is M
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        packssdw xmm0, xmm0
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        test    r9d, r9d
        jnz     @integerShortEnded
        // ---- p integer digits (1..15), terminator at [rdi + p]: <= 8 digits, dword 0 = M ---------
@integerShortTail:
        test    r11b, 1
        jnz     @negateIntegerShort
         db      $3e    // PAD: DS prefix, no uop
        cvtdq2pd xmm0, xmm0
        ret
         db      $90    // gap after ret/jmp, never executed
@negateIntegerShort:
        cvtdq2pd xmm0, xmm0
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
         db      $90    // gap after ret/jmp, never executed
@integerShortEnded:                      // another byte after <= 8 digits: an exponent, or the integer so far with err = 1
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerShortExponent
        mov     dword ptr [rdx], 1
        test    r11b, 1
        jnz     @negateIntegerShort
        cvtdq2pd xmm0, xmm0
        ret
         db      $90    // gap after ret/jmp, never executed
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: M = H * 10^8 + L ----------------
@integerWideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        test    r9d, r9d
        jnz     @integerEnded
@integerWide:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        test    r11b, 1
        jnz     @negateInteger
        ret
         db      $90    // gap after ret/jmp, never executed
@negateInteger:
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@integerEnded:                           // another byte after 9 .. 15 digits
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerExponent
        mov     dword ptr [rdx], 1
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        test    r11b, 1
        jnz     @negateInteger
        ret
@integerExponent:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        shr     eax, 4
        lea     rdi, [rdi + rax + 1]
        xor     r9d, r9d
        or      r11d, 2
        jmp     @fastExponent
         db      $90    // gap after ret/jmp, never executed
@integerShortExponent:                   // rdi = after the e, M in r8, scale 0
        cvtdq2pd xmm0, xmm0
        shr     eax, 4
        lea     rdi, [rdi + rax + 1]
        xor     r9d, r9d
        or      r11d, 2
        jmp     @fastExponent
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
        // ---- p integer digits, the dot, fraction digits: eax = p, r10d = non-digit lanes -------
@fraction:                               // bit p of r10d is the dot
        btr     r10d, eax                // clear the dot
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     r10d, r10d               // n = first non-digit after the fraction digits, p+1 .. 16
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLDOT]// the dot removed: all digits left-aligned, lane 15 zero
        cmp     r10d, 9
        ja      @fractionWideRows
        movzx   r9d, byte ptr [rdi + r10]
        shl     r10d, 4
        pshufb  xmm0, [r8 + r10 + NUMBER_CTRLSHORT - 16]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        sub     r10d, eax
        shr     r10d, 1                  // (n - p) * 8 = (f + 1) * 8
        test    r9d, r9d
        jnz     @fractionShortEnded
@fractionShortValue:                     // M / 10^f: both exact, one rounding
        cvtdq2pd xmm0, xmm0
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
         db      $90    // gap after ret/jmp, never executed
@negateFraction:
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
         db      $90    // gap after ret/jmp, never executed
@fractionShortEnded:                     // another byte after <= 8 fraction digits
        or      r9d, 32
        cmp     r9d, 'e'
        je      @fractionShortExponent
        mov     dword ptr [rdx], 1
        cvtdq2pd xmm0, xmm0
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
         db      $90    // gap after ret/jmp, never executed
@fractionShortExponent:                  // scale = -f
        cvtdq2pd xmm0, xmm0
        shr     eax, 4
        shr     r10d, 3
        lea     rdi, [rdi + r10]
        lea     rdi, [rdi + rax + 1]
        lea     r9d, [r10 - 1]
        neg     r9
        or      r11d, 2
        jmp     @fastExponent
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@fractionWideRows:                       // 9 .. 15 digits; n = 16 means the fraction runs past the window
        cmp     r10d, 16
        je      @windowFraction
        movzx   r9d, byte ptr [rdi + r10]
        shl     r10d, 4
        pshufb  xmm0, [r8 + r10 + NUMBER_CTRLINT - 16]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
         db      $3e    // PAD: DS prefix, no uop
        sub     r10d, eax
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        shr     r10d, 1                  // (n - p) * 8 = (f + 1) * 8
        test    r9d, r9d
        jnz     @fractionEnded
@fractionValue:                          // M exact, 10^f exact, one rounding
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $90    // gap after ret/jmp, never executed
@fractionEnded:                          // another byte after 9 .. 15 fraction digits
        or      r9d, 32
        cmp     r9d, 'e'
        je      @fractionExponent
        mov     dword ptr [rdx], 1
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
         db      $0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
@fractionExponent:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        shr     eax, 4
        shr     r10d, 3
        lea     rdi, [rdi + r10]
        lea     rdi, [rdi + rax + 1]
        lea     r9d, [r10 - 1]
        neg     r9
        or      r11d, 2
        // ---- the inline exponent: xmm0 = the digits as an exact double, rdi = after the e, r9 = -f ---
@fastExponent:                           // up to 3 exponent digits; |scale| <= 22 converted here, the rest goes generic
        movzx   eax, byte ptr [rdi]
        xor     r8d, r8d
        cmp     eax, 43
        je      @feSkip
        cmp     eax, 45
        jne     @feFirst
        or      r11d, 4
@feSkip:
        inc     r8d
        movzx   eax, byte ptr [rdi + r8]
@feFirst:
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @feGeneric
        movzx   r10d, byte ptr [rdi + r8 + 1]
        sub     r10d, 48
        cmp     r10d, 9
        ja      @feEnd
        lea     eax, [rax + rax * 4]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        lea     eax, [r10 + rax * 2]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movzx   r10d, byte ptr [rdi + r8 + 2]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     r10d, 48
         db      $3e    // PAD: DS prefix, no uop
        cmp     r10d, 9
        ja      @feEnd
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        lea     eax, [rax + rax * 4]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        lea     eax, [r10 + rax * 2]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movzx   r10d, byte ptr [rdi + r8 + 3]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @feGeneric
@feEnd:                                  // eax = exponent (0..999), r10d = terminator - 48
        test    r11b, 4
        jz      @fePlus
        neg     eax
@fePlus:
        movsxd  rax, eax
        add     r9, rax
        xor     r8d, r8d
        cmp     r10d, -48
        setne   r8b
        mov     dword ptr [rdx], r8d
         db      $3e    // PAD: DS prefix, no uop
        lea     rax, [r9 + 22]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        cmp     rax, 44                  // |scale| <= 22
        ja      @feWide                  // |scale| > 22
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        lea     rax, [rip + POW10]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        test    r9, r9
        jns     @feMultiply              // a negative scale falls through to the exact division
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        neg     r9
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$90    // gap after ret/jmp, never executed
@feMultiply:                             // scale 0 .. 22
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
        test    r11b, 1
        jnz     @negateFraction
        ret
         db      $90    // gap after ret/jmp, never executed
@feGeneric:                              // no exponent digit or 4+ digits: the byte-loop exponent from after the e (M as integer)
        cvttsd2si r8, xmm0
        jmp     @exponent
         db      $66,$90    // gap after ret/jmp, never executed
@feWide:                                 // |scale| > 22: the generic converter (canonical zeros, Pascal arithmetic, range)
        cvttsd2si r8, xmm0
        mov     eax, r10d
        jmp     @terminator
         db      $90    // gap after ret/jmp, never executed
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:
        inc     rdi
        or      r11d, 1
        mov     eax, edi
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        mov     dword ptr [rdx], 0       // err for a NUL terminator; the other endings rewrite it
        jmp     @unsigned
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
        // ---- 15 digits with the dot at p, more fraction digits follow (eax = p * 16) -----------
@windowFraction:                         // the fraction runs past the window
        pshufb  xmm0, [r8 + NUMBER_CTRLINT + 15 * 16]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     r9d, r8d
        shr     r8, 32
        imul    r9, r9, 100000000
        add     r8, r9
        shr     eax, 4
        lea     r9, [rax - 15]           // scale = -(15 - p), 64-bit (a negative value)
        add     rdi, 16
        or      r11d, 2
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        movq    xmm3, rdx                // the err pointer and the flags wait in xmm3/xmm4 during the loop
        movq    xmm4, r11
        mov     r11, r8
        mov     rdx, r9
        test    r8, r8
        jz      @fractionZeros           // a zero window mantissa: the continuation zeros are leading zeros
        jmp     @fractionNext
         db      $66,$90    // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes                   // no leading digit: spaces, '+', '.', letters
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     eax, r8d
        shr     r8, 32
        imul    rax, rax, 100000000
        add     r8, rax
        add     rdi, 16
        xor     r9d, r9d
        or      r11d, 10
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @integerFirst
         db      $0f,$1f,$80,$00,$00,$00,$00    // gap after ret/jmp, never executed
@nil:                                    // a nil text: 0 with err = 1
        mov     dword ptr [rdx], 1
        pxor    xmm0, xmm0
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
        // ---- byte loops: the start without a window, and every continuation ------------------
@bytes:                                  // rdi = first byte after an optional consumed -
        xor     r8d, r8d
        xor     r9d, r9d
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        test    r11b, 1
        jnz     @integerFirst
        movzx   eax, byte ptr [rdi]
        cmp     eax, 32
        jne     @signByte
@spaces:
        add     rdi, 1
        inc     r8
        cmp     r8, 64
        ja      @garbageZero             // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rdi]
        cmp     eax, 32
        je      @spaces
        xor     r8d, r8d
@signByte:
        cmp     eax, 45
        jne     @plusSign
        or      r11d, 1
        add     rdi, 1
        jmp     @integerFirst
         db      $90    // gap after ret/jmp, never executed
@plusSign:
        cmp     eax, 43
        jne     @integerFirst
        add     rdi, 1
@integerFirst:                           // the first byte decides whether a digit was seen at all
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        ja      @integerDone
        or      r11d, 2
         db      $3e    // PAD: DS prefix, no uop
        test    r8, r8
        jz      @integerMaybeZero
@integerLoop:                            // rdi = a digit
        add     rdi, 1
        cmp     r8, r10
        jae     @integerDrop             // 19 significant digits kept: this one raises the scale
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerLoop
@integerDone:                            // al = byte - 48; rdi moves past the byte
        add     rdi, 1
@integerDone1:
        cmp     al, 254                  // '.'
        jne     @afterDigits
        movzx   eax, byte ptr [rdi]
        add     rdi, 1
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @afterDigits
         db      $3e    // PAD: DS prefix, no uop
        or      r11d, 2
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movq    xmm3, rdx                // the err pointer and the flags wait in xmm3/xmm4 during the loop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movq    xmm4, r11
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     r11, r8
         db      $3e    // PAD: DS prefix, no uop
        test    r8, r8
        jz      @fractionMaybeZero
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     rdx, r9
@fractionLoop:                           // rdi = the byte after a digit; r11/rdx = (M, scale) after the last nonzero digit
        cmp     r8, r10
        jae     @fractionDrop
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
        sub     r9, 1
        test    eax, eax
        cmovnz  r11, r8                  // a nonzero digit: the checkpoint moves here
        cmovnz  rdx, r9
@fractionNext:
        movzx   eax, byte ptr [rdi]
        add     rdi, 1
        sub     eax, 48
        cmp     eax, 9
        jbe     @fractionLoop
@fractionExit:                           // the trailing zeros of the fraction are dropped (canonical M); flags and err pointer back
        mov     r8, r11                  // the trailing zeros of the fraction are dropped (canonical M)
        mov     r9, rdx
        movq    r11, xmm4
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movq    rdx, xmm3
        // ---- after the digits: rdi = past the terminator, al = terminator - 48 ----------------
@afterDigits:
        mov     r10d, eax
        or      r10b, 32
        cmp     r10b, 53                 // 'e' or 'E' (bit 5 set by the OR)
        jne     @terminator
        // ---- exponent: rdi = after 'e', r8 = M, r9 = scale ---------------------------------
@exponent:
        movzx   eax, byte ptr [rdi]
        cmp     eax, 43
        je      @exponentSign
        cmp     eax, 45
        jne     @exponentFirst
        or      r11d, 4
@exponentSign:
        inc     rdi
        movzx   eax, byte ptr [rdi]
@exponentFirst:
        sub     al, 48
        cmp     al, 9
        ja      @exponentNone
        xor     r10d, r10d
        test    eax, eax
        jz      @exponentZeros
@exponentLoop:                           // r10 = the exponent value so far
        inc     rdi
        imul    r10, r10, 10
        jo      @rangeM                  // the exponent value exceeds signed 64-bit magnitude
        add     r10, rax
        jo      @exponentLimit
@exponentNext:
        movzx   eax, byte ptr [rdi]
        sub     al, 48
        cmp     al, 9
        jbe     @exponentLoop
@exponentEnd:                            // r10 = the exponent value
        mov     rdi, r10
        neg     rdi
        test    r11b, 4
        cmovnz  r10, rdi                 // a negative exponent
        add     r9, r10
        jo      @scaleOverflow
@terminator:                             // err = 0 for NUL after a digit
        xor     r10d, r10d
        test    r11b, 2
        jz      @exponentNone
        cmp     al, 208                  // NUL
        setne   r10b
        // ---- convert: r8 = M, r9 = scale, r10d = err, r11 bit 0 = sign -----------------------
@convert:
        mov     dword ptr [rdx], r10d
         db      $3e    // PAD: DS prefix, no uop
        test    r8, r8
        jz      @zero                    // a zero mantissa with an absurd exponent: 0 with err = 1
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     rax, r8
        test    r9, r9
        jg      @inexact                 // a positive scale: strip the zeros first, then choose (spellings agree)
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @inexact
         db      $3e    // PAD: DS prefix, no uop
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        ja      @inexact
@exact:                                  // one exact operation: the nearest double
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
        test    r9, r9
        jns     @exactMultiply           // a negative scale (the exponent class) falls through to the exact division
        neg     r9
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
@done:
        test    r11b, 1
        jnz     @negate
        ret
@exactMultiply:                          // scale 0 .. 22 (10^0 = 1 exactly)
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
        test    r11b, 1
        jnz     @negate
        ret
@negate:
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
@exponentNone:                           // "1e", "1e+", "1ex": err 1, the value without an exponent
        mov     r10d, 1
        jmp     @convert
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
        // ---- inexact: strip the trailing zeros ("1.50" = "1.5", "2745024e26" = "27450240000e22") --
@inexact:
        mov     rdi, -3689348814741910323
        mov     r10, 1844674407370955161
        mov     rax, r8
        imul    rax, rdi
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, r10
        jbe     @stripLoop               // divisible by 10: strip the zeros out of line (the common M has none)
@stripped:                               // canonical (M, scale): the same for every spelling of the value
        mov     rax, r8
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @wideMantissa
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        jbe     @exact
        test    r9, r9
        jns     @recover                 // a positive scale: the 23..37 recovery, else the Pascal product
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
         db      $3e    // PAD: DS prefix, no uop
        lea     rdi, [r9 + 31]
        cmp     rdi, 62
        ja      @far
         db      $3e    // PAD: DS prefix, no uop
        mulsd   xmm0, qword ptr [rax + rdi * 8]// * POW10[scale], |scale| <= 31
        test    r11b, 1
        jnz     @negate
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@stripLoop:                              // M has trailing zeros: divide them out (M * inverse(5) ror 1 = M / 10 exactly)
        mov     r8, rax
        inc     r9
        mov     rax, r8
        imul    rax, rdi
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, r10
        jbe     @stripLoop               // divisible by 10: strip the zeros out of line (the common M has none)
        jmp     @stripped
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@recover:                                // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        lea     rax, [r9 - 23]
        cmp     rax, 14                  // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        ja      @pascal
        cvtsi2sd xmm0, r8
        lea     rdi, [rip + POW10]
        movsd   xmm1, qword ptr [rdi + rax * 8 + 32 * 8]
        mulsd   xmm1, xmm0
        comisd  xmm1, qword ptr [rip + NumberSimdData + NUMBER_EXACTINTEGER]
        ja      @pascalScale             // not an exact integer: the Pascal product
        mulsd   xmm1, qword ptr [rdi + 53 * 8]// * 10^22, exact: one rounding in all
        movapd  xmm0, xmm1
        jmp     @done
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@wideMantissa:                           // M >= 2^53: scale 1 .. 3 as one integer product when it fits 64 bits
        lea     rax, [r9 - 1]
        cmp     rax, 2
        ja      @pascal
        lea     rax, [rip + POW10]
        cvttsd2si rdi, qword ptr [rax + r9 * 8 + 31 * 8]// 10^scale as an integer
        mov     r10, rdx                 // MUL needs rdx: keep the err pointer in r10
        mov     rax, r8
        mul     rdi
        mov     rdi, rdx                 // DIV needs rdx: keep the err pointer in rdi
        mov     rdx, r10
        test    rdi, rdi
        jnz     @pascal                  // the product does not fit: the Pascal path
        mov     r8, rax
        xor     r9d, r9d
@pascal:                                 // the original Pascal arithmetic: M as a double, then the powers
        test    r8, r8
        js      @pascalBig
        cvtsi2sd xmm0, r8
@pascalScale:
        lea     rax, [rip + POW10]
        lea     rdi, [r9 + 31]
        cmp     rdi, 62
        ja      @far
        mulsd   xmm0, qword ptr [rax + rdi * 8]// * POW10[scale], |scale| <= 31
        test    r11b, 1
        jnz     @negate
        ret
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@pascalBig:                              // M >= 2^63: halve with a sticky bit, convert, double
        mov     rax, r8
        shr     rax, 1
        mov     edi, r8d
        and     edi, 1
        or      rax, rdi
        cvtsi2sd xmm0, rax
        addsd   xmm0, xmm0
        jmp     @pascalScale
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@far:                                    // |scale| > 31: the two POW10 constants of HugePower10Pos/Neg first
        lea     rdi, [r9 - 32]
        cmp     rdi, 256
        ja      @farEdge
        mov     edi, r9d
        shr     edi, 5
        mov     r10d, r9d
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rdi * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm0, xmm1
        jmp     @done
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
@farEdge:
        test    r9, r9
        jns     @huge
@wideNegative:
        mov     edi, r9d
        neg     edi
        cmp     r9, -307
        jl      @tiny
        mov     r10d, edi
        shr     edi, 5
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rdi * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, xmm1
        jmp     @done
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
@tiny:                                   // scale <= -308: the decimal cutoff decides underflow exactly
        cmp     r9, -342
        jl      @range
        lea     edi, [r9 + 342]
        cmp     r9, -324
        jg      @tinyScale
        cvttsd2si r10, qword ptr [rax + rdi * 8 + 31 * 8]
        mov     rdi, rdx                 // DIV needs rdx: keep the err pointer in rdi
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_UNDERFLOW]
        xor     edx, edx
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        div     r10                      // floor(cutoff midpoint / 10^n)
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     rdx, rdi
        cmp     r8, rax
        jbe     @range                   // rounds to zero
        lea     rax, [rip + POW10]
@tinyScale:                              // -324 < scale <= -308, or a nonzero result proved above
        lea     edi, [r9 + 160]
        neg     edi
        mov     r10d, edi
        shr     edi, 5
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rdi * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, qword ptr [rax + 81 * 8]// * 10^-160 (POW10[50]): a normal intermediate
        mulsd   xmm0, xmm1
        maxsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MINBITS]// the cutoff proved a nonzero result
        jmp     @done
         db      $90    // gap after ret/jmp, never executed
@huge:                                   // scale 289 .. 308: the decimal cutoff decides overflow exactly
        cmp     r9, 308
        ja      @range
        lea     edi, [r9 - 289]
        je      @lastPower
        cvttsd2si r10, qword ptr [rax + rdi * 8 + 31 * 8]
        mov     rdi, rdx                 // DIV needs rdx: keep the err pointer in rdi
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_OVERFLOW]
        xor     edx, edx
        div     r10                      // floor(cutoff midpoint / 10^n)
        mov     rdx, rdi
        cmp     r8, rax
        ja      @range
        lea     rax, [rip + POW10]
        jmp     @hugeScale
@lastPower:                              // 10^308: only M = 1 stays finite
        cmp     r8, 1
        ja      @range
@hugeScale:
        mov     edi, r9d
        shr     edi, 5
        mov     r10d, r9d
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rdi * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm1, qword ptr [rip + NumberSimdData + NUMBER_INVSCALE]// 10^scale * 2^-512
        mulsd   xmm0, xmm1
        minsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MAXSCALED]// the cutoff proved a finite result
        mulsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_SCALE]
        jmp     @done
@zero:
        pxor    xmm0, xmm0
        jmp     @done
         db      $66,$90    // gap after ret/jmp, never executed
@windowZeros:                            // the full SIMD window consumed 16 leading zeroes already
        mov     r8d, 16
        jmp     @integerZeros
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
        // ---- the capped phases that do not grow the mantissa: leading zeros of the integer, the fraction, the exponent; beyond 64 bytes the text ends like garbage (err = 1) ----
@integerMaybeZero:                       // M = 0 so far: a zero digit starts the zero-skip
        test    eax, eax
        jnz     @integerLoop
        test    r11b, 8
        jnz     @windowZeros
@integerZeros:                           // leading zeros of the integer while M = 0: at most 64 (r8 counts, then M = 0 again)
        add     rdi, 1
        inc     r8
        cmp     r8, 64
        ja      @garbageZero             // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rdi]
        sub     al, 48
        jz      @integerZeros
        xor     r8d, r8d
        jmp     @integerFirst
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@fractionMaybeZero:                      // M = 0 so far
        mov     rdx, r9
        test    eax, eax
        jnz     @fractionLoop
        dec     r9
@fractionZeros:                          // leading zeros of the fraction while M = 0: at most 64 (r9 counts them down)
        movzx   eax, byte ptr [rdi]
        add     rdi, 1
        sub     al, 48
        jnz     @fractionZerosEnd
        dec     r9
        cmp     r9, -64
        jge     @fractionZeros
        mov     eax, 1
        jmp     @fractionExit
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@fractionZerosEnd:
        dec     rdi
        jmp     @fractionNext
         db      $0f,$1f,$80,$00,$00,$00,$00    // gap after ret/jmp, never executed
@exponentZeros:                          // leading zeros of the exponent: at most 64 (r10 counts, then 0 again)
        add     rdi, 1
        inc     r10
        cmp     r10, 64
        ja      @garbageExp              // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rdi]
        sub     al, 48
        jz      @exponentZeros
        xor     r10d, r10d
        jmp     @exponentNext
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@integerDrop:                            // an integer digit beyond the 19 kept: it raises the scale
        add     r9, 1
        cmp     r9, 64
        ja      @garbageInt              // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rdi]
        add     rdi, 1
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerDrop
        jmp     @integerDone1
         db      $66,$90    // gap after ret/jmp, never executed
@fractionDrop:                           // fraction digits beyond the 19 kept: skipped, one taken jump per digit
        mov     r10d, 64
@fractionDropLoop:                       // at most 64 dropped fraction digits (r10 counts down)
        movzx   eax, byte ptr [rdi]
        add     rdi, 1
        sub     eax, 48
        cmp     eax, 9
        ja      @fractionExit
        dec     r10
        jnz     @fractionDropLoop
        mov     eax, 1
        jmp     @fractionExit
        // ---- beyond a cap: the text ends here like a garbage terminator: err = 1, the value so far ----
@garbageZero:                            // a zero mantissa (spaces or leading zeros): M = 0
        xor     r8d, r8d
@garbageInt:                             // a dropped-digit run beyond the cap: M kept
        mov     eax, 1
        jmp     @afterDigits
         db      $0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@exponentLimit:
        mov     rax, r10
        shl     rax, 1
        jnz     @rangeM
        test    r11b, 4
        jz      @rangeM
        jmp     @exponentNext
         db      $0f,$1f,$80,$00,$00,$00,$00    // gap after ret/jmp, never executed
@scaleOverflow:                          // the exponent fits; a zero mantissa survives combined-scale overflow
        test    r8, r8
        jz      @terminator
@rangeM:                                 // from the exponent loop: r8 = M, not yet stripped
        mov     dword ptr [rdx], 1
        test    r8, r8
        jz      @zero                    // a zero mantissa with an absurd exponent: 0 with err = 1
        mov     rdi, -3689348814741910323
        mov     r10, 1844674407370955161
@rangeStrip:
        mov     rax, r8
        imul    rax, rdi
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, r10
        ja      @range
        mov     r8, rax
        jmp     @rangeStrip
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
@range:                                  // err = 1, the value is the stripped mantissa as an integer (the same on every path)
        mov     dword ptr [rdx], 1
        xor     r9d, r9d
        jmp     @pascal
@garbageExp:
        mov     eax, 1
        jmp     @terminator
{$else}
        test    rcx, rcx
        jz      @nil
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     eax, ecx
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        and     eax, 4095
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        xor     r11d, r11d
        cmp     byte ptr [rcx], '-'
        je      @negative
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     dword ptr [rdx], 0       // err for a NUL terminator; the other endings rewrite it
@unsigned:                               // eax = P mod 4096 (after the sign), err = 0 stored
        cmp     eax, 4079
        ja      @bytes                   // the window would cross the page: byte loops from the start
        lea     r8, [rip + NumberSimdData]
        movdqu  xmm0, [rcx]
        movdqa  xmm1, xmm0
        paddb   xmm1, [rip + NumberSimdData + NUMBER_BIAS]
        pcmpgtb xmm1, [rip + NumberSimdData + NUMBER_THRESHOLD]// digit lanes
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        pmovmskb r10d, xmm1
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        not     r10d                     // non-digit lanes; bits 16..31 are set
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     eax, r10d                // p = leading digit count, 0..16
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     r9d, [rax - 1]
        cmp     r9d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movzx   r9d, byte ptr [rcx + rax]// the byte after the integer digits
        cmp     r9d, '.'
        je      @fraction
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @integerWideRows         // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is M
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        packssdw xmm0, xmm0
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        test    r9d, r9d
        jnz     @integerShortEnded
        // ---- p integer digits (1..15), terminator at [rcx + p]: <= 8 digits, dword 0 = M ---------
@integerShortTail:
        test    r11b, 1
        jnz     @negateIntegerShort
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cvtdq2pd xmm0, xmm0
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@negateIntegerShort:
        cvtdq2pd xmm0, xmm0
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@integerShortEnded:                      // another byte after <= 8 digits: an exponent, or the integer so far with err = 1
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerShortExponent
        mov     dword ptr [rdx], 1
        test    r11b, 1
        jnz     @negateIntegerShort
        cvtdq2pd xmm0, xmm0
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: M = H * 10^8 + L ----------------
@integerWideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        test    r9d, r9d
        jnz     @integerEnded
@integerWide:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        test    r11b, 1
        jnz     @negateInteger
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@negateInteger:
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@integerEnded:                           // another byte after 9 .. 15 digits
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerExponent
        mov     dword ptr [rdx], 1
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        test    r11b, 1
        jnz     @negateInteger
        ret
@integerExponent:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        shr     eax, 4
        lea     rcx, [rcx + rax + 1]
        xor     r9d, r9d
        or      r11d, 2
        jmp     @fastExponent
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@integerShortExponent:                   // rcx = after the e, M in r8, scale 0
        cvtdq2pd xmm0, xmm0
        shr     eax, 4
        lea     rcx, [rcx + rax + 1]
        xor     r9d, r9d
        or      r11d, 2
        jmp     @fastExponent
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- p integer digits, the dot, fraction digits: eax = p, r10d = non-digit lanes -------
@fraction:                               // bit p of r10d is the dot
        btr     r10d, eax                // clear the dot
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     r10d, r10d               // n = first non-digit after the fraction digits, p+1 .. 16
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLDOT]// the dot removed: all digits left-aligned, lane 15 zero
        cmp     r10d, 9
        ja      @fractionWideRows
        movzx   r9d, byte ptr [rcx + r10]
        shl     r10d, 4
        pshufb  xmm0, [r8 + r10 + NUMBER_CTRLSHORT - 16]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        sub     r10d, eax
        shr     r10d, 1                  // (n - p) * 8 = (f + 1) * 8
        test    r9d, r9d
        jnz     @fractionShortEnded
@fractionShortValue:                     // M / 10^f: both exact, one rounding
        cvtdq2pd xmm0, xmm0
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@negateFraction:
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@fractionShortEnded:                     // another byte after <= 8 fraction digits
        or      r9d, 32
        cmp     r9d, 'e'
        je      @fractionShortExponent
        mov     dword ptr [rdx], 1
        cvtdq2pd xmm0, xmm0
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@fractionShortExponent:                  // scale = -f
        cvtdq2pd xmm0, xmm0
        shr     eax, 4
        shr     r10d, 3
        lea     rcx, [rcx + r10]
        lea     rcx, [rcx + rax + 1]
        lea     r9d, [r10 - 1]
        neg     r9
        or      r11d, 2
        jmp     @fastExponent
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@fractionWideRows:                       // 9 .. 15 digits; n = 16 means the fraction runs past the window
        cmp     r10d, 16
        je      @windowFraction
        movzx   r9d, byte ptr [rcx + r10]
        shl     r10d, 4
        pshufb  xmm0, [r8 + r10 + NUMBER_CTRLINT - 16]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r10d, eax
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        shr     r10d, 1                  // (n - p) * 8 = (f + 1) * 8
        test    r9d, r9d
        jnz     @fractionEnded
@fractionValue:                          // M exact, 10^f exact, one rounding
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@fractionEnded:                          // another byte after 9 .. 15 fraction digits
        or      r9d, 32
        cmp     r9d, 'e'
        je      @fractionExponent
        mov     dword ptr [rdx], 1
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        divsd   xmm0, qword ptr [r8 + r10 + NUMBER_POWERS - 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
        {$ifndef FPC} db      $0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
@fractionExponent:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        shr     eax, 4
        shr     r10d, 3
        lea     rcx, [rcx + r10]
        lea     rcx, [rcx + rax + 1]
        lea     r9d, [r10 - 1]
        neg     r9
        or      r11d, 2
        // ---- the inline exponent: xmm0 = the digits as an exact double, rcx = after the e, r9 = -f ---
@fastExponent:                           // up to 3 exponent digits; |scale| <= 22 converted here, the rest goes generic
        movzx   eax, byte ptr [rcx]
        xor     r8d, r8d
        cmp     eax, 43
        je      @feSkip
        cmp     eax, 45
        jne     @feFirst
        or      r11d, 4
@feSkip:
        inc     r8d
        movzx   eax, byte ptr [rcx + r8]
@feFirst:
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @feGeneric
        movzx   r10d, byte ptr [rcx + r8 + 1]
        sub     r10d, 48
        cmp     r10d, 9
        ja      @feEnd
        lea     eax, [rax + rax * 4]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     eax, [r10 + rax * 2]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movzx   r10d, byte ptr [rcx + r8 + 2]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r10d, 48
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     r10d, 9
        ja      @feEnd
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     eax, [rax + rax * 4]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     eax, [r10 + rax * 2]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movzx   r10d, byte ptr [rcx + r8 + 3]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @feGeneric
@feEnd:                                  // eax = exponent (0..999), r10d = terminator - 48
        test    r11b, 4
        jz      @fePlus
        neg     eax
@fePlus:
        movsxd  rax, eax
        add     r9, rax
        xor     r8d, r8d
        cmp     r10d, -48
        setne   r8b
        mov     dword ptr [rdx], r8d
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     rax, [r9 + 22]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     rax, 44                  // |scale| <= 22
        ja      @feWide                  // |scale| > 22
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     rax, [rip + POW10]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    r9, r9
        jns     @feMultiply              // a negative scale falls through to the exact division
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        neg     r9
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
        test    r11b, 1
        jnz     @negateFraction
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@feMultiply:                             // scale 0 .. 22
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
        test    r11b, 1
        jnz     @negateFraction
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@feGeneric:                              // no exponent digit or 4+ digits: the byte-loop exponent from after the e (M as integer)
        cvttsd2si r8, xmm0
        jmp     @exponent
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@feWide:                                 // |scale| > 22: the generic converter (canonical zeros, Pascal arithmetic, range)
        cvttsd2si r8, xmm0
        mov     eax, r10d
        jmp     @terminator
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:
        inc     rcx
        or      r11d, 1
        mov     eax, ecx
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        mov     dword ptr [rdx], 0       // err for a NUL terminator; the other endings rewrite it
        jmp     @unsigned
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- 15 digits with the dot at p, more fraction digits follow (eax = p * 16) -----------
@windowFraction:                         // the fraction runs past the window
        pshufb  xmm0, [r8 + NUMBER_CTRLINT + 15 * 16]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     r9d, r8d
        shr     r8, 32
        imul    r9, r9, 100000000
        add     r8, r9
        shr     eax, 4
        lea     r9, [rax - 15]           // scale = -(15 - p), 64-bit (a negative value)
        add     rcx, 16
        or      r11d, 2
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        movq    xmm3, rdx                // the err pointer and the flags wait in xmm3/xmm4 during the loop
        movq    xmm4, r11
        mov     r11, r8
        mov     rdx, r9
        test    r8, r8
        jz      @fractionZeros           // a zero window mantissa: the continuation zeros are leading zeros
        jmp     @fractionNext
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes                   // no leading digit: spaces, '+', '.', letters
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     eax, r8d
        shr     r8, 32
        imul    rax, rax, 100000000
        add     r8, rax
        add     rcx, 16
        xor     r9d, r9d
        or      r11d, 10
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @integerFirst
        {$ifndef FPC} db      $0f,$1f,$80,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
@nil:                                    // a nil text: 0 with err = 1
        mov     dword ptr [rdx], 1
        pxor    xmm0, xmm0
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- byte loops: the start without a window, and every continuation ------------------
@bytes:                                  // rcx = first byte after an optional consumed -
        xor     r8d, r8d
        xor     r9d, r9d
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        test    r11b, 1
        jnz     @integerFirst
        movzx   eax, byte ptr [rcx]
        cmp     eax, 32
        jne     @signByte
@spaces:
        add     rcx, 1
        inc     r8
        cmp     r8, 64
        ja      @garbageZero             // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rcx]
        cmp     eax, 32
        je      @spaces
        xor     r8d, r8d
@signByte:
        cmp     eax, 45
        jne     @plusSign
        or      r11d, 1
        add     rcx, 1
        jmp     @integerFirst
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@plusSign:
        cmp     eax, 43
        jne     @integerFirst
        add     rcx, 1
@integerFirst:                           // the first byte decides whether a digit was seen at all
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        ja      @integerDone
        or      r11d, 2
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    r8, r8
        jz      @integerMaybeZero
@integerLoop:                            // rcx = a digit
        add     rcx, 1
        cmp     r8, r10
        jae     @integerDrop             // 19 significant digits kept: this one raises the scale
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerLoop
@integerDone:                            // al = byte - 48; rcx moves past the byte
        add     rcx, 1
@integerDone1:
        cmp     al, 254                  // '.'
        jne     @afterDigits
        movzx   eax, byte ptr [rcx]
        add     rcx, 1
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @afterDigits
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        or      r11d, 2
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movq    xmm3, rdx                // the err pointer and the flags wait in xmm3/xmm4 during the loop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movq    xmm4, r11
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     r11, r8
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    r8, r8
        jz      @fractionMaybeZero
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     rdx, r9
@fractionLoop:                           // rcx = the byte after a digit; r11/rdx = (M, scale) after the last nonzero digit
        cmp     r8, r10
        jae     @fractionDrop
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
        sub     r9, 1
        test    eax, eax
        cmovnz  r11, r8                  // a nonzero digit: the checkpoint moves here
        cmovnz  rdx, r9
@fractionNext:
        movzx   eax, byte ptr [rcx]
        add     rcx, 1
        sub     eax, 48
        cmp     eax, 9
        jbe     @fractionLoop
@fractionExit:                           // the trailing zeros of the fraction are dropped (canonical M); flags and err pointer back
        mov     r8, r11                  // the trailing zeros of the fraction are dropped (canonical M)
        mov     r9, rdx
        movq    r11, xmm4
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movq    rdx, xmm3
        // ---- after the digits: rcx = past the terminator, al = terminator - 48 ----------------
@afterDigits:
        mov     r10d, eax
        or      r10b, 32
        cmp     r10b, 53                 // 'e' or 'E' (bit 5 set by the OR)
        jne     @terminator
        // ---- exponent: rcx = after 'e', r8 = M, r9 = scale ---------------------------------
@exponent:
        movzx   eax, byte ptr [rcx]
        cmp     eax, 43
        je      @exponentSign
        cmp     eax, 45
        jne     @exponentFirst
        or      r11d, 4
@exponentSign:
        inc     rcx
        movzx   eax, byte ptr [rcx]
@exponentFirst:
        sub     al, 48
        cmp     al, 9
        ja      @exponentNone
        xor     r10d, r10d
        test    eax, eax
        jz      @exponentZeros
@exponentLoop:                           // r10 = the exponent value so far
        inc     rcx
        imul    r10, r10, 10
        jo      @rangeM                  // the exponent value exceeds signed 64-bit magnitude
        add     r10, rax
        jo      @exponentLimit
@exponentNext:
        movzx   eax, byte ptr [rcx]
        sub     al, 48
        cmp     al, 9
        jbe     @exponentLoop
@exponentEnd:                            // r10 = the exponent value
        mov     rcx, r10
        neg     rcx
        test    r11b, 4
        cmovnz  r10, rcx                 // a negative exponent
        add     r9, r10
        jo      @scaleOverflow
@terminator:                             // err = 0 for NUL after a digit
        xor     r10d, r10d
        test    r11b, 2
        jz      @exponentNone
        cmp     al, 208                  // NUL
        setne   r10b
        // ---- convert: r8 = M, r9 = scale, r10d = err, r11 bit 0 = sign -----------------------
@convert:
        mov     dword ptr [rdx], r10d
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    r8, r8
        jz      @zero                    // a zero mantissa with an absurd exponent: 0 with err = 1
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     rax, r8
        test    r9, r9
        jg      @inexact                 // a positive scale: strip the zeros first, then choose (spellings agree)
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @inexact
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        ja      @inexact
@exact:                                  // one exact operation: the nearest double
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
        test    r9, r9
        jns     @exactMultiply           // a negative scale (the exponent class) falls through to the exact division
        neg     r9
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
@done:
        test    r11b, 1
        jnz     @negate
        ret
@exactMultiply:                          // scale 0 .. 22 (10^0 = 1 exactly)
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
        test    r11b, 1
        jnz     @negate
        ret
@negate:
        xorpd   xmm0, [rip + NumberSimdData + NUMBER_SIGN]
        ret
@exponentNone:                           // "1e", "1e+", "1ex": err 1, the value without an exponent
        mov     r10d, 1
        jmp     @convert
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- inexact: strip the trailing zeros ("1.50" = "1.5", "2745024e26" = "27450240000e22") --
@inexact:
        mov     rcx, -3689348814741910323
        mov     r10, 1844674407370955161
        mov     rax, r8
        imul    rax, rcx
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, r10
        jbe     @stripLoop               // divisible by 10: strip the zeros out of line (the common M has none)
@stripped:                               // canonical (M, scale): the same for every spelling of the value
        mov     rax, r8
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @wideMantissa
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        jbe     @exact
        test    r9, r9
        jns     @recover                 // a positive scale: the 23..37 recovery, else the Pascal product
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     rcx, [r9 + 31]
        cmp     rcx, 62
        ja      @far
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mulsd   xmm0, qword ptr [rax + rcx * 8]// * POW10[scale], |scale| <= 31
        test    r11b, 1
        jnz     @negate
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@stripLoop:                              // M has trailing zeros: divide them out (M * inverse(5) ror 1 = M / 10 exactly)
        mov     r8, rax
        inc     r9
        mov     rax, r8
        imul    rax, rcx
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, r10
        jbe     @stripLoop               // divisible by 10: strip the zeros out of line (the common M has none)
        jmp     @stripped
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@recover:                                // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        lea     rax, [r9 - 23]
        cmp     rax, 14                  // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        ja      @pascal
        cvtsi2sd xmm0, r8
        lea     rcx, [rip + POW10]
        movsd   xmm1, qword ptr [rcx + rax * 8 + 32 * 8]
        mulsd   xmm1, xmm0
        comisd  xmm1, qword ptr [rip + NumberSimdData + NUMBER_EXACTINTEGER]
        ja      @pascalScale             // not an exact integer: the Pascal product
        mulsd   xmm1, qword ptr [rcx + 53 * 8]// * 10^22, exact: one rounding in all
        movapd  xmm0, xmm1
        jmp     @done
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@wideMantissa:                           // M >= 2^53: scale 1 .. 3 as one integer product when it fits 64 bits
        lea     rax, [r9 - 1]
        cmp     rax, 2
        ja      @pascal
        lea     rax, [rip + POW10]
        cvttsd2si rcx, qword ptr [rax + r9 * 8 + 31 * 8]// 10^scale as an integer
        mov     r10, rdx                 // MUL needs rdx: keep the err pointer in r10
        mov     rax, r8
        mul     rcx
        mov     rcx, rdx                 // DIV needs rdx: keep the err pointer in rcx
        mov     rdx, r10
        test    rcx, rcx
        jnz     @pascal                  // the product does not fit: the Pascal path
        mov     r8, rax
        xor     r9d, r9d
@pascal:                                 // the original Pascal arithmetic: M as a double, then the powers
        test    r8, r8
        js      @pascalBig
        cvtsi2sd xmm0, r8
@pascalScale:
        lea     rax, [rip + POW10]
        lea     rcx, [r9 + 31]
        cmp     rcx, 62
        ja      @far
        mulsd   xmm0, qword ptr [rax + rcx * 8]// * POW10[scale], |scale| <= 31
        test    r11b, 1
        jnz     @negate
        ret
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@pascalBig:                              // M >= 2^63: halve with a sticky bit, convert, double
        mov     rax, r8
        shr     rax, 1
        mov     ecx, r8d
        and     ecx, 1
        or      rax, rcx
        cvtsi2sd xmm0, rax
        addsd   xmm0, xmm0
        jmp     @pascalScale
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@far:                                    // |scale| > 31: the two POW10 constants of HugePower10Pos/Neg first
        lea     rcx, [r9 - 32]
        cmp     rcx, 256
        ja      @farEdge
        mov     ecx, r9d
        shr     ecx, 5
        mov     r10d, r9d
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rcx * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm0, xmm1
        jmp     @done
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
@farEdge:
        test    r9, r9
        jns     @huge
@wideNegative:
        mov     ecx, r9d
        neg     ecx
        cmp     r9, -307
        jl      @tiny
        mov     r10d, ecx
        shr     ecx, 5
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rcx * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, xmm1
        jmp     @done
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
@tiny:                                   // scale <= -308: the decimal cutoff decides underflow exactly
        cmp     r9, -342
        jl      @range
        lea     ecx, [r9 + 342]
        cmp     r9, -324
        jg      @tinyScale
        cvttsd2si r10, qword ptr [rax + rcx * 8 + 31 * 8]
        mov     rcx, rdx                 // DIV needs rdx: keep the err pointer in rcx
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_UNDERFLOW]
        xor     edx, edx
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        div     r10                      // floor(cutoff midpoint / 10^n)
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     rdx, rcx
        cmp     r8, rax
        jbe     @range                   // rounds to zero
        lea     rax, [rip + POW10]
@tinyScale:                              // -324 < scale <= -308, or a nonzero result proved above
        lea     ecx, [r9 + 160]
        neg     ecx
        mov     r10d, ecx
        shr     ecx, 5
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rcx * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, qword ptr [rax + 81 * 8]// * 10^-160 (POW10[50]): a normal intermediate
        mulsd   xmm0, xmm1
        maxsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MINBITS]// the cutoff proved a nonzero result
        jmp     @done
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@huge:                                   // scale 289 .. 308: the decimal cutoff decides overflow exactly
        cmp     r9, 308
        ja      @range
        lea     ecx, [r9 - 289]
        je      @lastPower
        cvttsd2si r10, qword ptr [rax + rcx * 8 + 31 * 8]
        mov     rcx, rdx                 // DIV needs rdx: keep the err pointer in rcx
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_OVERFLOW]
        xor     edx, edx
        div     r10                      // floor(cutoff midpoint / 10^n)
        mov     rdx, rcx
        cmp     r8, rax
        ja      @range
        lea     rax, [rip + POW10]
        jmp     @hugeScale
@lastPower:                              // 10^308: only M = 1 stays finite
        cmp     r8, 1
        ja      @range
@hugeScale:
        mov     ecx, r9d
        shr     ecx, 5
        mov     r10d, r9d
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + rcx * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm1, qword ptr [rip + NumberSimdData + NUMBER_INVSCALE]// 10^scale * 2^-512
        mulsd   xmm0, xmm1
        minsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MAXSCALED]// the cutoff proved a finite result
        mulsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_SCALE]
        jmp     @done
@zero:
        pxor    xmm0, xmm0
        jmp     @done
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@windowZeros:                            // the full SIMD window consumed 16 leading zeroes already
        mov     r8d, 16
        jmp     @integerZeros
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- the capped phases that do not grow the mantissa: leading zeros of the integer, the fraction, the exponent; beyond 64 bytes the text ends like garbage (err = 1) ----
@integerMaybeZero:                       // M = 0 so far: a zero digit starts the zero-skip
        test    eax, eax
        jnz     @integerLoop
        test    r11b, 8
        jnz     @windowZeros
@integerZeros:                           // leading zeros of the integer while M = 0: at most 64 (r8 counts, then M = 0 again)
        add     rcx, 1
        inc     r8
        cmp     r8, 64
        ja      @garbageZero             // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rcx]
        sub     al, 48
        jz      @integerZeros
        xor     r8d, r8d
        jmp     @integerFirst
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@fractionMaybeZero:                      // M = 0 so far
        mov     rdx, r9
        test    eax, eax
        jnz     @fractionLoop
        dec     r9
@fractionZeros:                          // leading zeros of the fraction while M = 0: at most 64 (r9 counts them down)
        movzx   eax, byte ptr [rcx]
        add     rcx, 1
        sub     al, 48
        jnz     @fractionZerosEnd
        dec     r9
        cmp     r9, -64
        jge     @fractionZeros
        mov     eax, 1
        jmp     @fractionExit
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@fractionZerosEnd:
        dec     rcx
        jmp     @fractionNext
        {$ifndef FPC} db      $0f,$1f,$80,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
@exponentZeros:                          // leading zeros of the exponent: at most 64 (r10 counts, then 0 again)
        add     rcx, 1
        inc     r10
        cmp     r10, 64
        ja      @garbageExp              // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rcx]
        sub     al, 48
        jz      @exponentZeros
        xor     r10d, r10d
        jmp     @exponentNext
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@integerDrop:                            // an integer digit beyond the 19 kept: it raises the scale
        add     r9, 1
        cmp     r9, 64
        ja      @garbageInt              // more than 64 bytes in this phase: garbage
        movzx   eax, byte ptr [rcx]
        add     rcx, 1
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerDrop
        jmp     @integerDone1
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@fractionDrop:                           // fraction digits beyond the 19 kept: skipped, one taken jump per digit
        mov     r10d, 64
@fractionDropLoop:                       // at most 64 dropped fraction digits (r10 counts down)
        movzx   eax, byte ptr [rcx]
        add     rcx, 1
        sub     eax, 48
        cmp     eax, 9
        ja      @fractionExit
        dec     r10
        jnz     @fractionDropLoop
        mov     eax, 1
        jmp     @fractionExit
        // ---- beyond a cap: the text ends here like a garbage terminator: err = 1, the value so far ----
@garbageZero:                            // a zero mantissa (spaces or leading zeros): M = 0
        xor     r8d, r8d
@garbageInt:                             // a dropped-digit run beyond the cap: M kept
        mov     eax, 1
        jmp     @afterDigits
        {$ifndef FPC} db      $0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@exponentLimit:
        mov     rax, r10
        shl     rax, 1
        jnz     @rangeM
        test    r11b, 4
        jz      @rangeM
        jmp     @exponentNext
        {$ifndef FPC} db      $0f,$1f,$80,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
@scaleOverflow:                          // the exponent fits; a zero mantissa survives combined-scale overflow
        test    r8, r8
        jz      @terminator
@rangeM:                                 // from the exponent loop: r8 = M, not yet stripped
        mov     dword ptr [rdx], 1
        test    r8, r8
        jz      @zero                    // a zero mantissa with an absurd exponent: 0 with err = 1
        mov     rcx, -3689348814741910323
        mov     r10, 1844674407370955161
@rangeStrip:
        mov     rax, r8
        imul    rax, rcx
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, r10
        ja      @range
        mov     r8, rax
        jmp     @rangeStrip
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
@range:                                  // err = 1, the value is the stripped mantissa as an integer (the same on every path)
        mov     dword ptr [rdx], 1
        xor     r9d, r9d
        jmp     @pascal
@garbageExp:
        mov     eax, 1
        jmp     @terminator
{$endif SYSVABI}
end;
