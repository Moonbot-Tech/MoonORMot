// GetNumericVariantFromJson: complete JSON-number parser, no calls or stack frame.
// - Strict JSON number grammar; the returned cursor identifies the delimiter for
//   the caller to check. Failure returns nil and leaves Value untouched.
// - Integers use varInteger through 999999999, then varInt64 through the signed
//   64-bit range. Fractional scale, dropped digits or integer overflow require
//   AllowVarDouble. Canonical scale cancellation may keep an integer type.
// - Double values follow GetExtended in nearest mode, including canonical trailing
//   zeroes and 19 significant digits. JSON zero is always +0, even in directed modes;
//   directed rounding of other values is not part of the contract.
// - Zero accepts a representable exponent even if adding the fractional scale
//   overflows. Exponent-value overflow and nonzero Double range errors fail.
// - Nongrowing byte phases stop after 64 bytes. The SSSE3 window never crosses a
//   4 KiB page; it may load bytes after the delimiter. NumberNoSimd selects byte loops.
// Win64: rcx=P, rdx=@Value, r8b=AllowVarDouble. SysV: rdi replaces rcx; dl supplies
// AllowVarDouble before rsi is moved to rdx. r8=mantissa, r9=scale, r10=scratch/type;
// r11 bits 0=AllowVarDouble, 1=sign, 2=negative exponent, 3=discarded digits.
// The Value pointer waits in xmm2 around implicit RDX use by MUL/DIV.
// Generated from number-layout/routine_json.py; see mormot.core.base.asmx64.layout.md.

function GetNumericVariantFromJson(Json: PUtf8Char; var Value: TVarData; AllowVarDouble: boolean): PUtf8Char;
{$ifdef FPC} nostackframe; assembler; asm {$else} asm .noframe {$endif}
{$ifdef SYSVABI}
        movzx   r11d, dl
        test    rdi, rdi
        jz      @nilHot
        mov     rdx, rsi
        mov     eax, edi
         db      $3e    // PAD: DS prefix, no uop
        and     eax, 4095
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        cmp     byte ptr [rdi], '-'
        je      @negative
        lea     r8, [rip + NumberSimdData]
@unsigned:                               // eax = P mod 4096 (after the sign), r8 = the table, r11 = AllowDouble | sign
        cmp     eax, 4079
        ja      @bytes                   // the window would cross the page: byte loops from the start
        movdqu  xmm0, [rdi]
        movdqa  xmm1, xmm0
        paddb   xmm1, [rip + NumberSimdData + NUMBER_BIAS]
        pcmpgtb xmm1, [rip + NumberSimdData + NUMBER_THRESHOLD]// digit lanes
        pmovmskb r10d, xmm1
        not     r10d                     // non-digit lanes; bits 16..31 are set
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     eax, r10d                // p = leading digit count, 0..16
        lea     r9d, [rax - 1]
        cmp     r9d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        movzx   r9d, word ptr [rdi]      // the first two bytes: "0" followed by a digit is not JSON
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     r9d, $3030
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        ror     r9d, 8                   // low byte 0 (a leading "0") and high byte 0..9 (a digit) <=> the value is 0..9
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        cmp     r9d, 9
        jbe     @nilHot                  // a leading zero
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movzx   r9d, byte ptr [rdi + rax]// the byte after the integer digits
        cmp     r9d, '.'
        je      @fraction
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @integerWideRows         // 9 .. 15 digits
        lea     r10d, [rax + rax]        // row p (16 bytes each, scaled by 8 below)
        pshufb  xmm0, [r8 + r10 * 8 + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is M
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
         db      $3e    // PAD: DS prefix, no uop
        packssdw xmm0, xmm0
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movd    r8d, xmm0                // M
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        add     rax, rdi                 // the cursor: the terminator
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerShortExponent    // e/E: an exponent
        test    r11b, 2
        jnz     @negateShort
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], varInteger
        ret
        // ---- p integer digits (1..15), terminator at [rdi + p]: <= 8 digits, dword 0 = M; cursor rax ----
@negateShort:
        neg     r8
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], varInteger
        ret
@nilHot:                                 // not a number (a copy next to the hot paths: short jumps)
        xor     eax, eax
        ret
@integerShortExponent:                   // rax = the e, r8 = M
        cvtsi2sd xmm0, r8
        lea     rdi, [rax + 1]
        xor     r9d, r9d
        jmp     @fastExponent
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: M = H * 10^8 + L; the type by value --------
@integerWideRows:
        lea     r10d, [rax + rax]        // row p (16 bytes each, scaled by 8 below)
        pshufb  xmm0, [r8 + r10 * 8 + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     r10d, r8d
        shr     r8, 32
        imul    r10, r10, 100000000
        add     r8, r10
        add     rax, rdi                 // the cursor: the terminator
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerExponent
        mov     r10d, varInteger
        mov     r9d, varInt64
        cmp     r8, 999999999
        cmova   r10d, r9d                // ten or more significant digits: Int64
        test    r11b, 2
        jnz     @negateWide
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        ret
@negateWide:
        neg     r8
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        ret
         db      $0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@integerExponent:                        // rax = the e, r8 = M
        cvtsi2sd xmm0, r8
        lea     rdi, [rax + 1]
        xor     r9d, r9d
        jmp     @fastExponent
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $90    // gap after ret/jmp, never executed
        // ---- p integer digits, the dot, fraction digits: eax = p, r10d = non-digit lanes -------
@fraction:                               // bit p of r10d is the dot
        btr     r10d, eax                // clear the dot
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     r10d, r10d               // n = first non-digit after the fraction digits, p+1 .. 16
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        lea     r9d, [rax + rax]
        pshufb  xmm0, [r8 + r9 * 8 + NUMBER_CTRLDOT]// the dot removed: all digits left-aligned, lane 15 zero
        lea     r9d, [r10 + r10 - 2]     // row n - 1
        cmp     r10d, 9
        ja      @fractionWideRows
        pshufb  xmm0, [r8 + r9 * 8 + NUMBER_CTRLSHORT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        sub     r10d, eax                // n - p = f + 1
        add     rdi, rax
        cmp     r10d, 1                  // no digit after the dot
        je      @nilHot
        add     rdi, r10                 // the cursor: the byte after the fraction digits
        movzx   eax, byte ptr [rdi]
        cmp     eax, '.'
        je      @nilHot
        or      eax, 32
        cmp     eax, 'e'
        je      @fractionShortExponent
        cvtdq2pd xmm0, xmm0
        divsd   xmm0, qword ptr [r8 + r10 * 8 + NUMBER_POWERS - 8]// / 10^f: M exact, 10^f exact, one rounding
@storeDouble:                            // xmm0 = the value, rdi = the cursor; nil unless doubles are allowed; JSON zero is +0
        test    r11b, 1
        jz      @nilHot
        movsd   qword ptr [rdx + 8], xmm0
        test    r11b, 2
        jnz     @negateDouble
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
@negateDouble:                           // the sign flipped unless the value is zero: JSON zero is +0 in every rounding mode
        pxor    xmm1, xmm1
        cmpneqsd xmm1, xmm0              // all ones unless the value is zero
        andpd   xmm1, [rip + NumberSimdData + NUMBER_SIGN]// the sign bit, or nothing for a zero
        xorpd   xmm0, xmm1               // JSON zero stays +0 in every rounding mode
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
@fractionShortExponent:                  // scale = -f = 1 - (f + 1)
        cvtdq2pd xmm0, xmm0
        mov     r9d, 1
        sub     r9, r10
        inc     rdi
        jmp     @fastExponent
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $90    // gap after ret/jmp, never executed
@fractionWideRows:                       // 9 .. 15 digits; n = 16 means the fraction runs past the window
        cmp     r10d, 16
        je      @windowFraction
        pshufb  xmm0, [r8 + r9 * 8 + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        sub     r10d, eax                // n - p = f + 1
        add     rdi, rax
        cmp     r10d, 1                  // no digit after the dot
        je      @nilHot
        add     rdi, r10                 // the cursor: the byte after the fraction digits
        movzx   eax, byte ptr [rdi]
        cmp     eax, '.'
        je      @nilHot
        or      eax, 32
        cmp     eax, 'e'
        je      @fractionExponent
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        divsd   xmm0, qword ptr [r8 + r10 * 8 + NUMBER_POWERS - 8]// / 10^f: M exact, 10^f exact, one rounding
        test    r11b, 1
        jz      @nilHot
         db      $3e    // PAD: DS prefix, no uop
        movsd   qword ptr [rdx + 8], xmm0
        test    r11b, 2
        jnz     @negateDouble
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@fractionExponent:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        mov     r9d, 1
        sub     r9, r10
        inc     rdi
        // ---- the inline exponent: xmm0 = the digits as an exact double, rdi = after the e, r9 = -f ---
@fastExponent:                           // up to 3 exponent digits (rdi + r8 = the current byte); the rest goes generic
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
@feFirst:                                // the first exponent byte must be a digit
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @nilHot
        inc     r8d
        movzx   r10d, byte ptr [rdi + r8]
        sub     r10d, 48
        cmp     r10d, 9
        ja      @feEnd
        lea     eax, [rax + rax * 4]
        lea     eax, [r10 + rax * 2]
        inc     r8d
        movzx   r10d, byte ptr [rdi + r8]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        ja      @feEnd
        lea     eax, [rax + rax * 4]
         db      $3e    // PAD: DS prefix, no uop
        lea     eax, [r10 + rax * 2]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        inc     r8d
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movzx   r10d, byte ptr [rdi + r8]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     r10d, 48
         db      $3e    // PAD: DS prefix, no uop
        cmp     r10d, 9
        jbe     @feGeneric
@feEnd:                                  // eax = exponent (0..999), r10d = terminator - 48, rdi + r8 = the terminator
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        add     rdi, r8
         db      $3e    // PAD: DS prefix, no uop
        cmp     r10d, -2                 // a dot after the exponent
        je      @nilHot
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movsxd  rax, eax
        test    r11b, 4
        jz      @fePlus
        neg     rax
@fePlus:
        add     r9, rax
        jnz     @feDouble                // a nonzero scale: a double; scale 0: an integer of the digits
        cvttsd2si r8, xmm0               // M (exact: below 10^15)
        mov     r10d, varInteger
        mov     r9d, varInt64
        cmp     r8, 999999999
        cmova   r10d, r9d                // ten or more significant digits: Int64
        jmp     @typed
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@feDouble:                               // a nonzero scale: a double
        test    r11b, 1
        jz      @nilHot
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        ja      @feWide
        lea     rax, [rip + POW10]
        test    r9, r9
        js      @feDivide
         db      $3e    // PAD: DS prefix, no uop
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
@feStore:
        test    r11b, 2
        jnz     @feNegate
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
@feNegate:
        pxor    xmm1, xmm1
        cmpneqsd xmm1, xmm0              // all ones unless the value is zero
        andpd   xmm1, [rip + NumberSimdData + NUMBER_SIGN]// the sign bit, or nothing for a zero
        xorpd   xmm0, xmm1               // JSON zero stays +0 in every rounding mode
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
@feDivide:                               // scale -22 .. -1
        neg     r9
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
        jmp     @feStore
@feGeneric:                              // 4+ exponent digits: the byte-loop exponent from the e (M as integer)
        cvttsd2si r8, xmm0               // M (exact: below 10^15)
        dec     rdi
        jmp     @exponent
@feWide:                                 // |scale| > 22: the generic converter (canonical zeros, Pascal arithmetic, range)
        cvttsd2si r8, xmm0               // M (exact: below 10^15)
        jmp     @double
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:
        inc     rdi
        or      r11d, 2
        mov     eax, edi
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        jmp     @unsigned
         db      $66,$90    // gap after ret/jmp, never executed
        // ---- 15 digits with the dot at p, more fraction digits follow (eax = p) ----------------
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
        lea     r9, [rax - 15]           // scale = -(15 - p), 64-bit (a negative value)
        add     rdi, 15                  // the last window byte: the loops advance before they load
        cmp     eax, 15
        je      @dot15                   // the dot is the last window byte: a digit must follow it
        test    r8, r8
        jz      @fractionZeros           // a zero window mantissa: the continuation zeros are leading zeros
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @fractionNext
@dot15:                                  // scale 0, rdi = the dot
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @dot
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit (nil), 16 = full
        test    eax, eax
        jz      @nil                     // nil
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     byte ptr [rdi], '0'      // 16 digits starting with a zero: not JSON
        je      @nil
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     eax, r8d
        shr     r8, 32
        imul    rax, rax, 100000000
        add     r8, rax
        add     rdi, 15                  // the last window byte: the loops advance before they load
        xor     r9d, r9d
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @integerNext
         db      $90    // gap after ret/jmp, never executed
        // ---- byte loops: the start without a window (page end), and every continuation ----------
@bytes:                                  // rdi = first byte after an optional consumed -
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        ja      @nil                     // not a number
        xor     r9d, r9d
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        mov     r8d, eax
        test    eax, eax
        jnz     @integerNext
        movzx   eax, byte ptr [rdi + 1]
        sub     eax, 48
        cmp     al, 9
        jbe     @nil
        jmp     @integerNext
@integerDrop:                            // an integer digit beyond the 19 kept: it raises the scale; the number is a double
        or      r11d, 8
@integerDropLoop:
        inc     r9
        cmp     r9, 64
        ja      @nil                     // not a number
        inc     rdi
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerDropLoop
        jmp     @integerDone
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$90    // gap after ret/jmp, never executed
@integerLoop:                            // rdi = the current digit (eax)
        cmp     r8, r10
        jae     @integerDrop             // 19 significant digits kept: this one raises the scale
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
@integerNext:
        add     rdi, 1
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerLoop
@integerDone:                            // al = byte - 48; rdi = the terminator
        cmp     al, 254                  // '.'
        jne     @afterDigits
@dot:                                    // rdi = the dot: a digit must follow
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        add     rdi, 1
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movzx   eax, byte ptr [rdi]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @nil                     // not a number
        test    r8, r8
        jnz     @fractionLoop
        test    eax, eax
        jnz     @fractionLoop
         db      $3e    // PAD: DS prefix, no uop
        dec     r9
@fractionZeros:                          // leading zeros of the fraction while M = 0: at most 64 (r9 counts them down)
        add     rdi, 1
        movzx   eax, byte ptr [rdi]
        sub     al, 48
        jnz     @fractionZerosEnd
        dec     r9
        cmp     r9, -64
        jge     @fractionZeros
        jmp     @nil
@fractionZerosEnd:                       // the digit limit again (the window entry did not set it)
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        cmp     al, 9
        jbe     @fractionLoop
        jmp     @afterDigits
        // ---- not a number: nil, Value untouched --------------------------------------------------
@nil:
        xor     eax, eax
        ret
@fractionDrop:                           // fraction digits beyond the 19 kept: skipped (the scale count of this one undone); the number is a double
        inc     r9
        or      r11d, 8
        mov     r10d, 64
@fractionDropLoop:                       // at most 64 dropped fraction digits (r10 counts down)
        add     rdi, 1
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        ja      @afterDigits
        dec     r10
        jnz     @fractionDropLoop
        jmp     @nil
         db      $66,$90    // gap after ret/jmp, never executed
@fractionLoop:                           // rdi = the current fraction digit (eax); r9 = -f (counted before the drop test, undone there)
        dec     r9
        cmp     r8, r10
        jae     @fractionDrop            // 19 significant digits kept: this one is dropped
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
@fractionNext:
        add     rdi, 1
@fractionLoad:
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        jbe     @fractionLoop
        // ---- after the digits: rdi = the terminator, al = terminator - 48 ------------------------
@afterDigits:                            // a dot after the number is invalid
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     r10d, eax
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        cmp     al, 254                  // '.'
        je      @nil
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        or      r10b, 32
        cmp     r10b, 53                 // 'e' or 'E' (bit 5 set by the OR)
        jne     @convert
        // ---- exponent: rdi = the e, r8 = M, r9 = scale ----------------------------------------------
@exponent:
        add     rdi, 1
        movzx   eax, byte ptr [rdi]
        cmp     eax, 43
        je      @exponentSign
        cmp     eax, 45
        jne     @exponentFirst
        or      r11d, 4
@exponentSign:
        add     rdi, 1
        movzx   eax, byte ptr [rdi]
@exponentFirst:                          // at least one exponent digit
        sub     eax, 48
        cmp     eax, 9
        ja      @nilExp
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        xor     r10d, r10d
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        test    eax, eax
        jnz     @exponentLoop
@exponentZeros:                          // leading zeros of the exponent: at most 64 (r10 counts, then 0 again)
        add     rdi, 1
        inc     r10
        cmp     r10, 64
        ja      @nilExp
        movzx   eax, byte ptr [rdi]
        sub     al, 48
        jz      @exponentZeros
        xor     r10d, r10d
        cmp     al, 9
        jbe     @exponentLoop
        jmp     @exponentEnd
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@exponentLimit:
        mov     rax, r10
        shl     rax, 1
        jnz     @nilExp
        test    r11b, 4
        jz      @nilExp
        jmp     @exponentNext
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
@scaleOverflow:                          // the exponent fits; a zero mantissa survives combined-scale overflow
        test    r8, r8
        jz      @double
@nilExp:                                 // not a number (a copy next to the exponent loop: short jumps)
        xor     eax, eax
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@exponentLoop:                           // r10 = the exponent value so far
        imul    r10, r10, 10
        jo      @nilExp                  // an exponent beyond 64 bits, or a scale beyond 64 bits: not a number
        add     r10, rax
        jo      @exponentLimit
@exponentNext:
        add     rdi, 1
        movzx   eax, byte ptr [rdi]
        sub     eax, 48
        cmp     eax, 9
        jbe     @exponentLoop
@exponentEnd:                            // r10 = the exponent value; a dot after the exponent is invalid
        cmp     al, 254                  // '.'
        je      @nilExp
        mov     rax, r10
        neg     rax
         db      $3e    // PAD: DS prefix, no uop
        test    r11b, 4
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        cmovnz  r10, rax                 // a negative exponent
        add     r9, r10
        jo      @scaleOverflow
        // ---- convert: r8 = M, r9 = scale, r11 flags, rdi = the cursor; the type by value -------------
@convert:                                // an integer only with scale 0 and no dropped digit
        test    r9, r9
        jnz     @double
        mov     r10d, varInteger
        test    r11b, 8
        jnz     @double
        cmp     r8, 999999999
        jbe     @typed
        mov     eax, r11d
        and     eax, 2
        shr     eax, 1
        mov     r10, 9223372036854775807
        add     r10, rax
        cmp     r8, r10
        ja      @double
         db      $3e    // PAD: DS prefix, no uop
        mov     r10d, varInt64
@typed:                                  // r10d = varInteger or varInt64, r8 = the magnitude
        test    r11b, 2
        jnz     @negateTyped
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        mov     rax, rdi
        ret
@negateTyped:
        neg     r8
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        mov     rax, rdi
        ret
@zero:
        pxor    xmm0, xmm0
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
@double:                                 // a double: nil unless allowed; a zero mantissa is +0 whatever the scale
        test    r11b, 1
        jz      @nil                     // nil
        test    r8, r8
        jz      @zero
        mov     rax, r8
        test    r9, r9
        jg      @inexact                 // a positive scale: strip the zeros first, then choose (spellings agree)
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @inexact
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        ja      @inexact
@exact:                                  // one exact operation: the nearest double
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
        test    r9, r9
        jns     @exactMultiply
        neg     r9
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
@done:                                   // xmm0 = the value: store, the cursor, JSON zero is +0
        test    r11b, 2
        jnz     @negateDone
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
@exactMultiply:                          // scale 0 .. 22 (10^0 = 1 exactly)
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
        jmp     @done
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
        // ---- inexact: strip the trailing zeros ("1.50" = "1.5", "2745024e26" = "27450240000e22") --
@inexact:
        mov     rax, r8
        imul    rax, qword ptr [rip + NumberSimdData + NUMBER_INV5]
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, qword ptr [rip + NumberSimdData + NUMBER_STRIPLIMIT]
        ja      @stripped                // not divisible by 10 (the common M): nothing to strip
        mov     r10, rax
@stripLoop:                              // M has trailing zeros: divide them out (M * inverse(5) ror 1 = M / 10 exactly); the mov before it keeps the loop off the jump pair
        mov     r8, rax
        inc     r9
        mov     rax, r8
        imul    rax, qword ptr [rip + NumberSimdData + NUMBER_INV5]
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, qword ptr [rip + NumberSimdData + NUMBER_STRIPLIMIT]
        jbe     @stripLoop
@stripped:                               // canonical (M, scale): the same for every spelling of the value
        mov     rax, r8
         db      $3e    // PAD: DS prefix, no uop
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @wideMantissa
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        jbe     @exact
        test    r9, r9
        jns     @recover                 // a positive scale: the 23..37 recovery, else the Pascal product
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
         db      $3e    // PAD: DS prefix, no uop
        lea     r10, [r9 + 31]
        cmp     r10, 62
        ja      @far
        mulsd   xmm0, qword ptr [rax + r10 * 8]// * POW10[scale], |scale| <= 31
        jmp     @done
@recover:                                // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        lea     rax, [r9 - 23]
        cmp     rax, 14                  // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        ja      @pascal
        cvtsi2sd xmm0, r8
        lea     r10, [rip + POW10]
        movsd   xmm1, qword ptr [r10 + rax * 8 + 32 * 8]
        mulsd   xmm1, xmm0
        comisd  xmm1, qword ptr [rip + NumberSimdData + NUMBER_EXACTINTEGER]
        ja      @pascalScale             // not an exact integer: the Pascal product
        mulsd   xmm1, qword ptr [r10 + 53 * 8]// * 10^22, exact: one rounding in all
        movapd  xmm0, xmm1
        jmp     @done
@wideMantissa:                           // M >= 2^53: scale 1 .. 3 as one integer product when it fits 64 bits
        lea     rax, [r9 - 1]
        cmp     rax, 2
        ja      @pascal
        lea     rax, [rip + POW10]
        cvttsd2si r10, qword ptr [rax + r9 * 8 + 31 * 8]// 10^scale as an integer
        movq    xmm2, rdx                // MUL/DIV need rdx: the Value pointer waits in xmm2
        mov     rax, r8
        mul     r10
        mov     r10, rdx
        movq    rdx, xmm2
        test    r10, r10
        jnz     @pascal                  // the product does not fit: the Pascal path
        mov     r8, rax
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        xor     r9d, r9d
@pascal:                                 // the original Pascal arithmetic: M as a double, then the powers
        test    r8, r8
        js      @pascalBig
        cvtsi2sd xmm0, r8
@pascalScale:
        lea     rax, [rip + POW10]
        lea     r10, [r9 + 31]
        cmp     r10, 62
        ja      @far
        mulsd   xmm0, qword ptr [rax + r10 * 8]// * POW10[scale], |scale| <= 31
        jmp     @done
         db      $90    // gap after ret/jmp, never executed
@pascalBig:                              // M >= 2^63: halve with a sticky bit, convert, double
        mov     rax, r8
        shr     rax, 1
        mov     r10d, r8d
        and     r10d, 1
        or      rax, r10
        cvtsi2sd xmm0, rax
        addsd   xmm0, xmm0
        jmp     @pascalScale
@far:                                    // |scale| > 31: the two POW10 constants of HugePower10Pos/Neg first
        lea     r10, [r9 - 32]
        cmp     r10, 256                 // scale 32 .. 288
        ja      @farEdge
        mov     r10d, r9d
        shr     r10d, 5
        mov     r8d, r9d
        and     r8d, 31
        movsd   xmm1, qword ptr [rax + r10 * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r8 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm0, xmm1
        jmp     @done
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$90    // gap after ret/jmp, never executed
@farEdge:
        test    r9, r9
        jns     @huge
@wideNegative:
        mov     r10d, r9d
        neg     r10d
        cmp     r9, -307
        jl      @tiny
        mov     r8d, r10d
        shr     r10d, 5
        and     r8d, 31
        movsd   xmm1, qword ptr [rax + r10 * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r8 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, xmm1
        jmp     @done
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@tiny:                                   // scale <= -308: the decimal cutoff decides underflow exactly
        cmp     r9, -342
        jl      @nil                     // below the smallest subnormal: not a number
        lea     r10d, [r9 + 342]
        cmp     r9, -324
        jg      @tinyScale
        cvttsd2si r10, qword ptr [rax + r10 * 8 + 31 * 8]
        movq    xmm2, rdx                // MUL/DIV need rdx: the Value pointer waits in xmm2
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_UNDERFLOW]
        xor     edx, edx
        div     r10                      // floor(cutoff midpoint / 10^n)
         db      $3e    // PAD: DS prefix, no uop
        movq    rdx, xmm2
        cmp     r8, rax
        jbe     @nil
        lea     rax, [rip + POW10]
@tinyScale:                              // -324 < scale <= -308, or a nonzero result proved above
        lea     r8d, [r9 + 160]
        neg     r8d
        mov     r10d, r8d
        shr     r8d, 5
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + r8 * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, qword ptr [rax + 81 * 8]// * 10^-160 (POW10[50]): a normal intermediate
        mulsd   xmm0, xmm1
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        maxsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MINBITS]// the cutoff proved a nonzero result
        jmp     @done
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$90    // gap after ret/jmp, never executed
@huge:                                   // scale 289 .. 308: the decimal cutoff decides overflow exactly
        cmp     r9, 308
        ja      @nil                     // not a number
        lea     r10d, [r9 - 289]
        je      @lastPower
        cvttsd2si r10, qword ptr [rax + r10 * 8 + 31 * 8]
        movq    xmm2, rdx                // MUL/DIV need rdx: the Value pointer waits in xmm2
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_OVERFLOW]
        xor     edx, edx
        div     r10                      // floor(cutoff midpoint / 10^n)
        movq    rdx, xmm2
        cmp     r8, rax
        ja      @nil                     // not a number
        lea     rax, [rip + POW10]
        jmp     @hugeScale
         db      $0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
@lastPower:                              // 10^308: only M = 1 stays finite
        cmp     r8, 1
        ja      @nil                     // not a number
@hugeScale:
        mov     r10d, r9d
        shr     r10d, 5
        mov     r8d, r9d
        and     r8d, 31
        movsd   xmm1, qword ptr [rax + r10 * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r8 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm1, qword ptr [rip + NumberSimdData + NUMBER_INVSCALE]// 10^scale * 2^-512
        mulsd   xmm0, xmm1
        minsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MAXSCALED]// the cutoff proved a finite result
        mulsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_SCALE]
        jmp     @done
@negateDone:
        pxor    xmm1, xmm1
        cmpneqsd xmm1, xmm0              // all ones unless the value is zero
        andpd   xmm1, [rip + NumberSimdData + NUMBER_SIGN]// the sign bit, or nothing for a zero
        xorpd   xmm0, xmm1               // JSON zero stays +0 in every rounding mode
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rdi
        ret
{$else}
        movzx   r11d, r8b
        test    rcx, rcx
        jz      @nilHot
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     eax, ecx
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        and     eax, 4095
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        cmp     byte ptr [rcx], '-'
        je      @negative
        lea     r8, [rip + NumberSimdData]
@unsigned:                               // eax = P mod 4096 (after the sign), r8 = the table, r11 = AllowDouble | sign
        cmp     eax, 4079
        ja      @bytes                   // the window would cross the page: byte loops from the start
        movdqu  xmm0, [rcx]
        movdqa  xmm1, xmm0
        paddb   xmm1, [rip + NumberSimdData + NUMBER_BIAS]
        pcmpgtb xmm1, [rip + NumberSimdData + NUMBER_THRESHOLD]// digit lanes
        pmovmskb r10d, xmm1
        not     r10d                     // non-digit lanes; bits 16..31 are set
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     eax, r10d                // p = leading digit count, 0..16
        lea     r9d, [rax - 1]
        cmp     r9d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        movzx   r9d, word ptr [rcx]      // the first two bytes: "0" followed by a digit is not JSON
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r9d, $3030
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        ror     r9d, 8                   // low byte 0 (a leading "0") and high byte 0..9 (a digit) <=> the value is 0..9
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     r9d, 9
        jbe     @nilHot                  // a leading zero
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movzx   r9d, byte ptr [rcx + rax]// the byte after the integer digits
        cmp     r9d, '.'
        je      @fraction
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @integerWideRows         // 9 .. 15 digits
        lea     r10d, [rax + rax]        // row p (16 bytes each, scaled by 8 below)
        pshufb  xmm0, [r8 + r10 * 8 + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is M
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        packssdw xmm0, xmm0
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movd    r8d, xmm0                // M
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        add     rax, rcx                 // the cursor: the terminator
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerShortExponent    // e/E: an exponent
        test    r11b, 2
        jnz     @negateShort
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], varInteger
        ret
        // ---- p integer digits (1..15), terminator at [rcx + p]: <= 8 digits, dword 0 = M; cursor rax ----
@negateShort:
        neg     r8
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], varInteger
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@nilHot:                                 // not a number (a copy next to the hot paths: short jumps)
        xor     eax, eax
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@integerShortExponent:                   // rax = the e, r8 = M
        cvtsi2sd xmm0, r8
        lea     rcx, [rax + 1]
        xor     r9d, r9d
        jmp     @fastExponent
        {$ifndef FPC} db      $0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: M = H * 10^8 + L; the type by value --------
@integerWideRows:
        lea     r10d, [rax + rax]        // row p (16 bytes each, scaled by 8 below)
        pshufb  xmm0, [r8 + r10 * 8 + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     r10d, r8d
        shr     r8, 32
        imul    r10, r10, 100000000
        add     r8, r10
        add     rax, rcx                 // the cursor: the terminator
        or      r9d, 32
        cmp     r9d, 'e'
        je      @integerExponent
        mov     r10d, varInteger
        mov     r9d, varInt64
        cmp     r8, 999999999
        cmova   r10d, r9d                // ten or more significant digits: Int64
        test    r11b, 2
        jnz     @negateWide
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@negateWide:
        neg     r8
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@integerExponent:                        // rax = the e, r8 = M
        cvtsi2sd xmm0, r8
        lea     rcx, [rax + 1]
        xor     r9d, r9d
        jmp     @fastExponent
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- p integer digits, the dot, fraction digits: eax = p, r10d = non-digit lanes -------
@fraction:                               // bit p of r10d is the dot
        btr     r10d, eax                // clear the dot
        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)
        bsf     r10d, r10d               // n = first non-digit after the fraction digits, p+1 .. 16
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        lea     r9d, [rax + rax]
        pshufb  xmm0, [r8 + r9 * 8 + NUMBER_CTRLDOT]// the dot removed: all digits left-aligned, lane 15 zero
        lea     r9d, [r10 + r10 - 2]     // row n - 1
        cmp     r10d, 9
        ja      @fractionWideRows
        pshufb  xmm0, [r8 + r9 * 8 + NUMBER_CTRLSHORT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        sub     r10d, eax                // n - p = f + 1
        add     rcx, rax
        cmp     r10d, 1                  // no digit after the dot
        je      @nilHot
        add     rcx, r10                 // the cursor: the byte after the fraction digits
        movzx   eax, byte ptr [rcx]
        cmp     eax, '.'
        je      @nilHot
        or      eax, 32
        cmp     eax, 'e'
        je      @fractionShortExponent
        cvtdq2pd xmm0, xmm0
        divsd   xmm0, qword ptr [r8 + r10 * 8 + NUMBER_POWERS - 8]// / 10^f: M exact, 10^f exact, one rounding
@storeDouble:                            // xmm0 = the value, rcx = the cursor; nil unless doubles are allowed; JSON zero is +0
        test    r11b, 1
        jz      @nilHot
        movsd   qword ptr [rdx + 8], xmm0
        test    r11b, 2
        jnz     @negateDouble
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@negateDouble:                           // the sign flipped unless the value is zero: JSON zero is +0 in every rounding mode
        pxor    xmm1, xmm1
        cmpneqsd xmm1, xmm0              // all ones unless the value is zero
        andpd   xmm1, [rip + NumberSimdData + NUMBER_SIGN]// the sign bit, or nothing for a zero
        xorpd   xmm0, xmm1               // JSON zero stays +0 in every rounding mode
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
@fractionShortExponent:                  // scale = -f = 1 - (f + 1)
        cvtdq2pd xmm0, xmm0
        mov     r9d, 1
        sub     r9, r10
        inc     rcx
        jmp     @fastExponent
        {$ifndef FPC} db      $0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
@fractionWideRows:                       // 9 .. 15 digits; n = 16 means the fraction runs past the window
        cmp     r10d, 16
        je      @windowFraction
        pshufb  xmm0, [r8 + r9 * 8 + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        sub     r10d, eax                // n - p = f + 1
        add     rcx, rax
        cmp     r10d, 1                  // no digit after the dot
        je      @nilHot
        add     rcx, r10                 // the cursor: the byte after the fraction digits
        movzx   eax, byte ptr [rcx]
        cmp     eax, '.'
        je      @nilHot
        or      eax, 32
        cmp     eax, 'e'
        je      @fractionExponent
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        divsd   xmm0, qword ptr [r8 + r10 * 8 + NUMBER_POWERS - 8]// / 10^f: M exact, 10^f exact, one rounding
        test    r11b, 1
        jz      @nilHot
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movsd   qword ptr [rdx + 8], xmm0
        test    r11b, 2
        jnz     @negateDouble
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@fractionExponent:
        cvtdq2pd xmm0, xmm0
        movhlps xmm1, xmm0
        mulsd   xmm0, qword ptr [r8 + NUMBER_E8]
        addsd   xmm0, xmm1
        mov     r9d, 1
        sub     r9, r10
        inc     rcx
        // ---- the inline exponent: xmm0 = the digits as an exact double, rcx = after the e, r9 = -f ---
@fastExponent:                           // up to 3 exponent digits (rcx + r8 = the current byte); the rest goes generic
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
@feFirst:                                // the first exponent byte must be a digit
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @nilHot
        inc     r8d
        movzx   r10d, byte ptr [rcx + r8]
        sub     r10d, 48
        cmp     r10d, 9
        ja      @feEnd
        lea     eax, [rax + rax * 4]
        lea     eax, [r10 + rax * 2]
        inc     r8d
        movzx   r10d, byte ptr [rcx + r8]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        ja      @feEnd
        lea     eax, [rax + rax * 4]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     eax, [r10 + rax * 2]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        inc     r8d
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movzx   r10d, byte ptr [rcx + r8]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r10d, 48
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     r10d, 9
        jbe     @feGeneric
@feEnd:                                  // eax = exponent (0..999), r10d = terminator - 48, rcx + r8 = the terminator
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        add     rcx, r8
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     r10d, -2                 // a dot after the exponent
        je      @nilHot
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movsxd  rax, eax
        test    r11b, 4
        jz      @fePlus
        neg     rax
@fePlus:
        add     r9, rax
        jnz     @feDouble                // a nonzero scale: a double; scale 0: an integer of the digits
        cvttsd2si r8, xmm0               // M (exact: below 10^15)
        mov     r10d, varInteger
        mov     r9d, varInt64
        cmp     r8, 999999999
        cmova   r10d, r9d                // ten or more significant digits: Int64
        jmp     @typed
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@feDouble:                               // a nonzero scale: a double
        test    r11b, 1
        jz      @nilHot
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        ja      @feWide
        lea     rax, [rip + POW10]
        test    r9, r9
        js      @feDivide
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
@feStore:
        test    r11b, 2
        jnz     @feNegate
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
@feNegate:
        pxor    xmm1, xmm1
        cmpneqsd xmm1, xmm0              // all ones unless the value is zero
        andpd   xmm1, [rip + NumberSimdData + NUMBER_SIGN]// the sign bit, or nothing for a zero
        xorpd   xmm0, xmm1               // JSON zero stays +0 in every rounding mode
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
@feDivide:                               // scale -22 .. -1
        neg     r9
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
        jmp     @feStore
@feGeneric:                              // 4+ exponent digits: the byte-loop exponent from the e (M as integer)
        cvttsd2si r8, xmm0               // M (exact: below 10^15)
        dec     rcx
        jmp     @exponent
@feWide:                                 // |scale| > 22: the generic converter (canonical zeros, Pascal arithmetic, range)
        cvttsd2si r8, xmm0               // M (exact: below 10^15)
        jmp     @double
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:
        inc     rcx
        or      r11d, 2
        mov     eax, ecx
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        jmp     @unsigned
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
        // ---- 15 digits with the dot at p, more fraction digits follow (eax = p) ----------------
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
        lea     r9, [rax - 15]           // scale = -(15 - p), 64-bit (a negative value)
        add     rcx, 15                  // the last window byte: the loops advance before they load
        cmp     eax, 15
        je      @dot15                   // the dot is the last window byte: a digit must follow it
        test    r8, r8
        jz      @fractionZeros           // a zero window mantissa: the continuation zeros are leading zeros
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @fractionNext
@dot15:                                  // scale 0, rcx = the dot
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @dot
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit (nil), 16 = full
        test    eax, eax
        jz      @nil                     // nil
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     byte ptr [rcx], '0'      // 16 digits starting with a zero: not JSON
        je      @nil
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    r8, xmm0
        mov     eax, r8d
        shr     r8, 32
        imul    rax, rax, 100000000
        add     r8, rax
        add     rcx, 15                  // the last window byte: the loops advance before they load
        xor     r9d, r9d
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        jmp     @integerNext
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
        // ---- byte loops: the start without a window (page end), and every continuation ----------
@bytes:                                  // rcx = first byte after an optional consumed -
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        ja      @nil                     // not a number
        xor     r9d, r9d
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        mov     r8d, eax
        test    eax, eax
        jnz     @integerNext
        movzx   eax, byte ptr [rcx + 1]
        sub     eax, 48
        cmp     al, 9
        jbe     @nil
        jmp     @integerNext
@integerDrop:                            // an integer digit beyond the 19 kept: it raises the scale; the number is a double
        or      r11d, 8
@integerDropLoop:
        inc     r9
        cmp     r9, 64
        ja      @nil                     // not a number
        inc     rcx
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerDropLoop
        jmp     @integerDone
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@integerLoop:                            // rcx = the current digit (eax)
        cmp     r8, r10
        jae     @integerDrop             // 19 significant digits kept: this one raises the scale
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
@integerNext:
        add     rcx, 1
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        jbe     @integerLoop
@integerDone:                            // al = byte - 48; rcx = the terminator
        cmp     al, 254                  // '.'
        jne     @afterDigits
@dot:                                    // rcx = the dot: a digit must follow
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        add     rcx, 1
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movzx   eax, byte ptr [rcx]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     eax, 48
        cmp     eax, 9
        ja      @nil                     // not a number
        test    r8, r8
        jnz     @fractionLoop
        test    eax, eax
        jnz     @fractionLoop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        dec     r9
@fractionZeros:                          // leading zeros of the fraction while M = 0: at most 64 (r9 counts them down)
        add     rcx, 1
        movzx   eax, byte ptr [rcx]
        sub     al, 48
        jnz     @fractionZerosEnd
        dec     r9
        cmp     r9, -64
        jge     @fractionZeros
        jmp     @nil
@fractionZerosEnd:                       // the digit limit again (the window entry did not set it)
        mov     r10, 1000000000000000000 // 10^18: 19 significant digits kept
        cmp     al, 9
        jbe     @fractionLoop
        jmp     @afterDigits
        // ---- not a number: nil, Value untouched --------------------------------------------------
@nil:
        xor     eax, eax
        ret
@fractionDrop:                           // fraction digits beyond the 19 kept: skipped (the scale count of this one undone); the number is a double
        inc     r9
        or      r11d, 8
        mov     r10d, 64
@fractionDropLoop:                       // at most 64 dropped fraction digits (r10 counts down)
        add     rcx, 1
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        ja      @afterDigits
        dec     r10
        jnz     @fractionDropLoop
        jmp     @nil
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@fractionLoop:                           // rcx = the current fraction digit (eax); r9 = -f (counted before the drop test, undone there)
        dec     r9
        cmp     r8, r10
        jae     @fractionDrop            // 19 significant digits kept: this one is dropped
        lea     r8, [r8 + r8 * 4]
        lea     r8, [rax + r8 * 2]
@fractionNext:
        add     rcx, 1
@fractionLoad:
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        jbe     @fractionLoop
        // ---- after the digits: rcx = the terminator, al = terminator - 48 ------------------------
@afterDigits:                            // a dot after the number is invalid
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     r10d, eax
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     al, 254                  // '.'
        je      @nil
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        or      r10b, 32
        cmp     r10b, 53                 // 'e' or 'E' (bit 5 set by the OR)
        jne     @convert
        // ---- exponent: rcx = the e, r8 = M, r9 = scale ----------------------------------------------
@exponent:
        add     rcx, 1
        movzx   eax, byte ptr [rcx]
        cmp     eax, 43
        je      @exponentSign
        cmp     eax, 45
        jne     @exponentFirst
        or      r11d, 4
@exponentSign:
        add     rcx, 1
        movzx   eax, byte ptr [rcx]
@exponentFirst:                          // at least one exponent digit
        sub     eax, 48
        cmp     eax, 9
        ja      @nilExp
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        xor     r10d, r10d
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    eax, eax
        jnz     @exponentLoop
@exponentZeros:                          // leading zeros of the exponent: at most 64 (r10 counts, then 0 again)
        add     rcx, 1
        inc     r10
        cmp     r10, 64
        ja      @nilExp
        movzx   eax, byte ptr [rcx]
        sub     al, 48
        jz      @exponentZeros
        xor     r10d, r10d
        cmp     al, 9
        jbe     @exponentLoop
        jmp     @exponentEnd
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@exponentLimit:
        mov     rax, r10
        shl     rax, 1
        jnz     @nilExp
        test    r11b, 4
        jz      @nilExp
        jmp     @exponentNext
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
@scaleOverflow:                          // the exponent fits; a zero mantissa survives combined-scale overflow
        test    r8, r8
        jz      @double
@nilExp:                                 // not a number (a copy next to the exponent loop: short jumps)
        xor     eax, eax
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@exponentLoop:                           // r10 = the exponent value so far
        imul    r10, r10, 10
        jo      @nilExp                  // an exponent beyond 64 bits, or a scale beyond 64 bits: not a number
        add     r10, rax
        jo      @exponentLimit
@exponentNext:
        add     rcx, 1
        movzx   eax, byte ptr [rcx]
        sub     eax, 48
        cmp     eax, 9
        jbe     @exponentLoop
@exponentEnd:                            // r10 = the exponent value; a dot after the exponent is invalid
        cmp     al, 254                  // '.'
        je      @nilExp
        mov     rax, r10
        neg     rax
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    r11b, 4
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmovnz  r10, rax                 // a negative exponent
        add     r9, r10
        jo      @scaleOverflow
        // ---- convert: r8 = M, r9 = scale, r11 flags, rcx = the cursor; the type by value -------------
@convert:                                // an integer only with scale 0 and no dropped digit
        test    r9, r9
        jnz     @double
        mov     r10d, varInteger
        test    r11b, 8
        jnz     @double
        cmp     r8, 999999999
        jbe     @typed
        mov     eax, r11d
        and     eax, 2
        shr     eax, 1
        mov     r10, 9223372036854775807
        add     r10, rax
        cmp     r8, r10
        ja      @double
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     r10d, varInt64
@typed:                                  // r10d = varInteger or varInt64, r8 = the magnitude
        test    r11b, 2
        jnz     @negateTyped
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        mov     rax, rcx
        ret
@negateTyped:
        neg     r8
        mov     qword ptr [rdx + 8], r8
        mov     dword ptr [rdx], r10d
        mov     rax, rcx
        ret
@zero:
        pxor    xmm0, xmm0
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
@double:                                 // a double: nil unless allowed; a zero mantissa is +0 whatever the scale
        test    r11b, 1
        jz      @nil                     // nil
        test    r8, r8
        jz      @zero
        mov     rax, r8
        test    r9, r9
        jg      @inexact                 // a positive scale: strip the zeros first, then choose (spellings agree)
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @inexact
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        ja      @inexact
@exact:                                  // one exact operation: the nearest double
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
        test    r9, r9
        jns     @exactMultiply
        neg     r9
        divsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]
@done:                                   // xmm0 = the value: store, the cursor, JSON zero is +0
        test    r11b, 2
        jnz     @negateDone
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
@exactMultiply:                          // scale 0 .. 22 (10^0 = 1 exactly)
        mulsd   xmm0, qword ptr [rax + r9 * 8 + 31 * 8]// * 10^scale (10^0 = 1 exactly)
        jmp     @done
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- inexact: strip the trailing zeros ("1.50" = "1.5", "2745024e26" = "27450240000e22") --
@inexact:
        mov     rax, r8
        imul    rax, qword ptr [rip + NumberSimdData + NUMBER_INV5]
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, qword ptr [rip + NumberSimdData + NUMBER_STRIPLIMIT]
        ja      @stripped                // not divisible by 10 (the common M): nothing to strip
        mov     r10, rax
@stripLoop:                              // M has trailing zeros: divide them out (M * inverse(5) ror 1 = M / 10 exactly); the mov before it keeps the loop off the jump pair
        mov     r8, rax
        inc     r9
        mov     rax, r8
        imul    rax, qword ptr [rip + NumberSimdData + NUMBER_INV5]
        ror     rax, 1                   // M * inverse(5) ror 1 = M / 10 when M is divisible by 10
        cmp     rax, qword ptr [rip + NumberSimdData + NUMBER_STRIPLIMIT]
        jbe     @stripLoop
@stripped:                               // canonical (M, scale): the same for every spelling of the value
        mov     rax, r8
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        shr     rax, 53                  // M >= 2^53: not exact
        jnz     @wideMantissa
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     rax, [r9 + 22]
        cmp     rax, 44                  // |scale| <= 22
        jbe     @exact
        test    r9, r9
        jns     @recover                 // a positive scale: the 23..37 recovery, else the Pascal product
        cvtsi2sd xmm0, r8
        lea     rax, [rip + POW10]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     r10, [r9 + 31]
        cmp     r10, 62
        ja      @far
        mulsd   xmm0, qword ptr [rax + r10 * 8]// * POW10[scale], |scale| <= 31
        jmp     @done
@recover:                                // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        lea     rax, [r9 - 23]
        cmp     rax, 14                  // scale 23 .. 37: exact while M * 10^(scale - 22) stays an integer below 2^53
        ja      @pascal
        cvtsi2sd xmm0, r8
        lea     r10, [rip + POW10]
        movsd   xmm1, qword ptr [r10 + rax * 8 + 32 * 8]
        mulsd   xmm1, xmm0
        comisd  xmm1, qword ptr [rip + NumberSimdData + NUMBER_EXACTINTEGER]
        ja      @pascalScale             // not an exact integer: the Pascal product
        mulsd   xmm1, qword ptr [r10 + 53 * 8]// * 10^22, exact: one rounding in all
        movapd  xmm0, xmm1
        jmp     @done
@wideMantissa:                           // M >= 2^53: scale 1 .. 3 as one integer product when it fits 64 bits
        lea     rax, [r9 - 1]
        cmp     rax, 2
        ja      @pascal
        lea     rax, [rip + POW10]
        cvttsd2si r10, qword ptr [rax + r9 * 8 + 31 * 8]// 10^scale as an integer
        movq    xmm2, rdx                // MUL/DIV need rdx: the Value pointer waits in xmm2
        mov     rax, r8
        mul     r10
        mov     r10, rdx
        movq    rdx, xmm2
        test    r10, r10
        jnz     @pascal                  // the product does not fit: the Pascal path
        mov     r8, rax
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        xor     r9d, r9d
@pascal:                                 // the original Pascal arithmetic: M as a double, then the powers
        test    r8, r8
        js      @pascalBig
        cvtsi2sd xmm0, r8
@pascalScale:
        lea     rax, [rip + POW10]
        lea     r10, [r9 + 31]
        cmp     r10, 62
        ja      @far
        mulsd   xmm0, qword ptr [rax + r10 * 8]// * POW10[scale], |scale| <= 31
        jmp     @done
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@pascalBig:                              // M >= 2^63: halve with a sticky bit, convert, double
        mov     rax, r8
        shr     rax, 1
        mov     r10d, r8d
        and     r10d, 1
        or      rax, r10
        cvtsi2sd xmm0, rax
        addsd   xmm0, xmm0
        jmp     @pascalScale
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
@far:                                    // |scale| > 31: the two POW10 constants of HugePower10Pos/Neg first
        lea     r10, [r9 - 32]
        cmp     r10, 256                 // scale 32 .. 288
        ja      @farEdge
        mov     r10d, r9d
        shr     r10d, 5
        mov     r8d, r9d
        and     r8d, 31
        movsd   xmm1, qword ptr [rax + r10 * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r8 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm0, xmm1
        jmp     @done
@farEdge:
        test    r9, r9
        jns     @huge
@wideNegative:
        mov     r10d, r9d
        neg     r10d
        cmp     r9, -307
        jl      @tiny
        mov     r8d, r10d
        shr     r10d, 5
        and     r8d, 31
        movsd   xmm1, qword ptr [rax + r10 * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r8 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, xmm1
        jmp     @done
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
@tiny:                                   // scale <= -308: the decimal cutoff decides underflow exactly
        cmp     r9, -342
        jl      @nil                     // below the smallest subnormal: not a number
        lea     r10d, [r9 + 342]
        cmp     r9, -324
        jg      @tinyScale
        cvttsd2si r10, qword ptr [rax + r10 * 8 + 31 * 8]
        movq    xmm2, rdx                // MUL/DIV need rdx: the Value pointer waits in xmm2
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_UNDERFLOW]
        xor     edx, edx
        div     r10                      // floor(cutoff midpoint / 10^n)
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movq    rdx, xmm2
        cmp     r8, rax
        jbe     @nil
        lea     rax, [rip + POW10]
@tinyScale:                              // -324 < scale <= -308, or a nonzero result proved above
        lea     r8d, [r9 + 160]
        neg     r8d
        mov     r10d, r8d
        shr     r8d, 5
        and     r10d, 31
        movsd   xmm1, qword ptr [rax + r8 * 8 + 76 * 8]// POW10[45 + hi] = 10^-(32 * hi)
        divsd   xmm1, qword ptr [rax + r10 * 8 + 31 * 8]// / 10^lo (HugePower10Neg)
        mulsd   xmm0, qword ptr [rax + 81 * 8]// * 10^-160 (POW10[50]): a normal intermediate
        mulsd   xmm0, xmm1
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        maxsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MINBITS]// the cutoff proved a nonzero result
        jmp     @done
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@huge:                                   // scale 289 .. 308: the decimal cutoff decides overflow exactly
        cmp     r9, 308
        ja      @nil                     // not a number
        lea     r10d, [r9 - 289]
        je      @lastPower
        cvttsd2si r10, qword ptr [rax + r10 * 8 + 31 * 8]
        movq    xmm2, rdx                // MUL/DIV need rdx: the Value pointer waits in xmm2
        mov     rax, qword ptr [rip + NumberSimdData + NUMBER_OVERFLOW]
        xor     edx, edx
        div     r10                      // floor(cutoff midpoint / 10^n)
        movq    rdx, xmm2
        cmp     r8, rax
        ja      @nil                     // not a number
        lea     rax, [rip + POW10]
        jmp     @hugeScale
        {$ifndef FPC} db      $0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
@lastPower:                              // 10^308: only M = 1 stays finite
        cmp     r8, 1
        ja      @nil                     // not a number
@hugeScale:
        mov     r10d, r9d
        shr     r10d, 5
        mov     r8d, r9d
        and     r8d, 31
        movsd   xmm1, qword ptr [rax + r10 * 8 + 65 * 8]// POW10[34 + hi] = 10^(32 * hi)
        mulsd   xmm1, qword ptr [rax + r8 * 8 + 31 * 8]// * 10^lo (HugePower10Pos)
        mulsd   xmm1, qword ptr [rip + NumberSimdData + NUMBER_INVSCALE]// 10^scale * 2^-512
        mulsd   xmm0, xmm1
        minsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_MAXSCALED]// the cutoff proved a finite result
        mulsd   xmm0, qword ptr [rip + NumberSimdData + NUMBER_SCALE]
        jmp     @done
@negateDone:
        pxor    xmm1, xmm1
        cmpneqsd xmm1, xmm0              // all ones unless the value is zero
        andpd   xmm1, [rip + NumberSimdData + NUMBER_SIGN]// the sign bit, or nothing for a zero
        xorpd   xmm0, xmm1               // JSON zero stays +0 in every rounding mode
        movsd   qword ptr [rdx + 8], xmm0
        mov     dword ptr [rdx], varDouble
        mov     rax, rcx
        ret
{$endif SYSVABI}
end;
