// Three complete native-integer parsers on the shared SSSE3 window.
// - GetInteger(P, PEnd): exclusive bound, no limit on work beyond that bound;
//   digits at/beyond PEnd never affect the result. Arithmetic wraps modulo 2^64.
// - GetInteger(P): first non-digit ends parsing; arithmetic wraps modulo 2^64.
//   Spaces, spaces after a sign and digits have a budget of 65 bytes each,
//   including bytes already consumed by SIMD (64 after the phase's first byte).
// - GetInteger(P, err): leading zeroes are free, at most 19 significant digits,
//   signed Int64 bounds and a NUL terminator are checked. Failure returns err=1
//   and the unnegated magnitude read so far. Spaces/sign spaces/leading zeroes
//   have the same 65-byte budget; crossing those caps returns zero with err=1.
// All three accept bytes <= space before a sign (except NUL), one sign, then
// spaces. The 16-byte load stays inside a 4 KiB page but may read after the end
// delimiter or PEnd. The bounded SIMD digit count excludes those extra bytes;
// its byte loop reads strictly before PEnd. NumberNoSimd forces byte parsing.
// Win64: rcx=P, rdx=PEnd/@err. SysV uses rdi for P and moves rsi to rdx when needed.
// rax=result, r8=table/budget, r9=end/budget/10^18, r10=digit, r11=sign.
// Generated from number-layout/routine_int*.py; see mormot.core.base.asmx64.layout.md.

function GetInteger(P, PEnd: PUtf8Char): PtrInt;
{$ifdef FPC} nostackframe; assembler; asm {$else} asm .noframe {$endif}
{$ifdef SYSVABI}
        mov     rdx, rsi
        mov     r9, rdx
        test    rdi, rdi
        jz      @zero                    // nil / NUL
        mov     eax, edi
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        and     eax, 4095
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     r9, rdi
        jle     @zero                    // P >= PEnd
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        xor     r11d, r11d
        cmp     byte ptr [rdi], '-'
        je      @negative
        lea     r8, [rip + NumberSimdData]
@unsigned:                               // eax = P mod 4096 (after the sign), r9 = PEnd - P > 0, r11 = the sign, r8 = the table
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
         db      $3e    // PAD: DS prefix, no uop
        cmp     rax, r9
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        cmova   rax, r9                  // p = min(p, PEnd - P): digits at or beyond PEnd do not count
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        lea     r10d, [rax - 1]
        cmp     r10d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @wideRows                // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is the value
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        movd    eax, xmm0                // the value
        test    r11b, 1
        jnz     @negate
        ret
        // ---- p digits (1..15, within the bound): <= 8 digits, dword 0 = the value ----------------
@negate:
        neg     rax
        ret
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------
@wideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        test    r11b, 1
        jnz     @negate
        ret
         db      $90    // gap after ret/jmp, never executed
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:                               // the Pascal allows spaces after the sign: the byte path handles them (p = 0)
        inc     rdi
        or      r11d, 1
        mov     eax, edi
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        dec     r9
        jnz     @unsigned
        xor     eax, eax
        ret
         db      $66,$0f,$1f,$44,$00,$00    // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        add     rdi, 16
        jmp     @next
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$90    // gap after ret/jmp, never executed
        // ---- digits byte by byte up to PEnd: rax = the value so far (wrapping like the Pascal) ---
@digitLoop:                              // r10 = the digit (IMUL keeps the bound check inside one block)
        imul    rax, rax, 10
        add     rax, r10
        inc     rdi
@next:                                   // rdi = the next byte, or PEnd
        cmp     rdi, rdx
        je      @signed                  // PEnd reached
        movzx   r10d, byte ptr [rdi]
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @digitLoop
@signed:                                 // the sign without a branch (the loop end leaves no room for a jump pair)
        mov     r10, rax
        neg     r10
        test    r11b, 1
        cmovnz  rax, r10
        ret
        // ---- nil, an empty range, no digit: 0 ------------------------------------------------------
@zero:
        xor     eax, eax
        ret
        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --
@bytes:                                  // rdi = the first byte (after a consumed -: spaces only)
        movzx   r10d, byte ptr [rdi]
        test    r11b, 1
        jnz     @afterSign
        cmp     r10d, 32
        ja      @signByte
        xor     eax, eax
@spaces:                                 // bytes <= 32 except NUL
        test    r10d, r10d
        jz      @zero                    // nil / NUL
        add     rdi, 1
        cmp     rdi, rdx
        je      @zero
        movzx   r10d, byte ptr [rdi]
        cmp     r10d, 32
        jbe     @spaces
@signByte:
        cmp     r10d, 45
        jne     @plusByte
        or      r11d, 1
        jmp     @signSpaces
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $66,$90    // gap after ret/jmp, never executed
@plusByte:
        cmp     r10d, 43
        jne     @firstDigit
@signSpaces:                             // spaces after the sign
        add     rdi, 1
        cmp     rdi, rdx
        je      @zero
        movzx   r10d, byte ptr [rdi]
@afterSign:
        cmp     r10d, 32
        je      @signSpaces
@firstDigit:
        xor     eax, eax
         db      $3e    // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        ja      @zero
        jmp     @digitLoop
{$else}
        mov     r9, rdx
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    rcx, rcx
        jz      @zero                    // nil / NUL
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
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
        sub     r9, rcx
        jle     @zero                    // P >= PEnd
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        xor     r11d, r11d
        cmp     byte ptr [rcx], '-'
        je      @negative
        lea     r8, [rip + NumberSimdData]
@unsigned:                               // eax = P mod 4096 (after the sign), r9 = PEnd - P > 0, r11 = the sign, r8 = the table
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
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     rax, r9
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmova   rax, r9                  // p = min(p, PEnd - P): digits at or beyond PEnd do not count
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        lea     r10d, [rax - 1]
        cmp     r10d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @wideRows                // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is the value
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movd    eax, xmm0                // the value
        test    r11b, 1
        jnz     @negate
        ret
        // ---- p digits (1..15, within the bound): <= 8 digits, dword 0 = the value ----------------
@negate:
        neg     rax
        ret
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------
@wideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        test    r11b, 1
        jnz     @negate
        ret
        {$ifndef FPC} db      $90 {$endif}   // gap after ret/jmp, never executed
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:                               // the Pascal allows spaces after the sign: the byte path handles them (p = 0)
        inc     rcx
        or      r11d, 1
        mov     eax, ecx
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        dec     r9
        jnz     @unsigned
        xor     eax, eax
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$44,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        add     rcx, 16
        jmp     @next
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
        // ---- digits byte by byte up to PEnd: rax = the value so far (wrapping like the Pascal) ---
@digitLoop:                              // r10 = the digit (IMUL keeps the bound check inside one block)
        imul    rax, rax, 10
        add     rax, r10
        inc     rcx
@next:                                   // rcx = the next byte, or PEnd
        cmp     rcx, rdx
        je      @signed                  // PEnd reached
        movzx   r10d, byte ptr [rcx]
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @digitLoop
@signed:                                 // the sign without a branch (the loop end leaves no room for a jump pair)
        mov     r10, rax
        neg     r10
        test    r11b, 1
        cmovnz  rax, r10
        ret
        // ---- nil, an empty range, no digit: 0 ------------------------------------------------------
@zero:
        xor     eax, eax
        ret
        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --
@bytes:                                  // rcx = the first byte (after a consumed -: spaces only)
        movzx   r10d, byte ptr [rcx]
        test    r11b, 1
        jnz     @afterSign
        cmp     r10d, 32
        ja      @signByte
        xor     eax, eax
@spaces:                                 // bytes <= 32 except NUL
        test    r10d, r10d
        jz      @zero                    // nil / NUL
        add     rcx, 1
        cmp     rcx, rdx
        je      @zero
        movzx   r10d, byte ptr [rcx]
        cmp     r10d, 32
        jbe     @spaces
@signByte:
        cmp     r10d, 45
        jne     @plusByte
        or      r11d, 1
        jmp     @signSpaces
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $66,$90 {$endif}   // gap after ret/jmp, never executed
@plusByte:
        cmp     r10d, 43
        jne     @firstDigit
@signSpaces:                             // spaces after the sign
        add     rcx, 1
        cmp     rcx, rdx
        je      @zero
        movzx   r10d, byte ptr [rcx]
@afterSign:
        cmp     r10d, 32
        je      @signSpaces
@firstDigit:
        xor     eax, eax
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        ja      @zero
        jmp     @digitLoop
{$endif SYSVABI}
end;

function GetInteger(P: PUtf8Char): PtrInt;
{$ifdef FPC} nostackframe; assembler; asm {$else} asm .noframe {$endif}
{$ifdef SYSVABI}
        xor     r11d, r11d
         db      $3e    // PAD: DS prefix, no uop
        test    rdi, rdi
        jz      @zero                    // nil / NUL / the budget is spent
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     eax, edi
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        and     eax, 4095
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        cmp     byte ptr [rdi], '-'
        je      @negative
        lea     r8, [rip + NumberSimdData]
@unsigned:                               // eax = P mod 4096 (after the sign), r11 = the sign, r8 = the table
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
        lea     r10d, [rax - 1]
        cmp     r10d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @wideRows                // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is the value
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movd    eax, xmm0                // the value
        test    r11b, 1
        jnz     @negate
        ret
        // ---- p digits (1..15): <= 8 digits, dword 0 = the value ----------------------------------------
@negate:
        neg     rax
        ret
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------
@wideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        test    r11b, 1
        jnz     @negate
        ret
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:                               // the Pascal allows spaces after the sign: the byte path handles them (p = 0)
        inc     rdi
        or      r11d, 1
        mov     eax, edi
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        jmp     @unsigned
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        add     rdi, 16
        mov     r9d, 49
        jmp     @next
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
        // ---- digits byte by byte: rax = the value so far (wrapping like the Pascal), at most 64 here --
@digitLoop:                              // r10 = the digit, r9 = the budget
        imul    rax, rax, 10
        add     rax, r10
        inc     rdi
        dec     r9
        jz      @signed                  // the digit budget is spent: the value so far
@next:
        movzx   r10d, byte ptr [rdi]
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @digitLoop
@signed:                                 // the sign without a branch (the loop end leaves no room for a jump pair)
        mov     r10, rax
        neg     r10
        test    r11b, 1
        cmovnz  rax, r10
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
        // ---- nil, NUL, no digit, a phase beyond its 64-byte budget: 0 ---------------------------------
@zero:
        xor     eax, eax
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --
@bytes:                                  // rdi = the first byte (after a consumed -: spaces only); each phase at most 64 bytes
        movzx   r10d, byte ptr [rdi]
        mov     r9d, 65                  // the budget of the next phase: 64 bytes beyond its first
        test    r11b, 1
        jnz     @afterSign
        cmp     r10d, 32
        ja      @signByte
        xor     eax, eax
@spaces:                                 // bytes <= 32 except NUL
        test    r10d, r10d
        jz      @zero                    // nil / NUL / the budget is spent
        add     rdi, 1
        dec     r9
        jz      @zero                    // nil / NUL / the budget is spent
        movzx   r10d, byte ptr [rdi]
        cmp     r10d, 32
        jbe     @spaces
         db      $3e    // PAD: DS prefix, no uop
        mov     r9d, 65                  // the budget of the next phase: 64 bytes beyond its first
@signByte:
        cmp     r10d, 45
        jne     @plusByte
        or      r11d, 1
        jmp     @signSkip
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
@plusByte:
        cmp     r10d, 43
        jne     @firstDigit
@signSkip:                               // past the sign (not charged to the budget)
        add     rdi, 1
        movzx   r10d, byte ptr [rdi]
@afterSign:
        cmp     r10d, 32
        jne     @firstDigit
@signSpaces:                             // spaces after the sign: at most 64
        add     rdi, 1
        dec     r9
        jz      @zero                    // nil / NUL / the budget is spent
        movzx   r10d, byte ptr [rdi]
        cmp     r10d, 32
        je      @signSpaces
@firstDigit:
        xor     eax, eax
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     r9d, 65                  // the budget of the next phase: 64 bytes beyond its first
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        ja      @zero
        jmp     @digitLoop
{$else}
        xor     r11d, r11d
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    rcx, rcx
        jz      @zero                    // nil / NUL / the budget is spent
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
@unsigned:                               // eax = P mod 4096 (after the sign), r11 = the sign, r8 = the table
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
        lea     r10d, [rax - 1]
        cmp     r10d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @wideRows                // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is the value
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movd    eax, xmm0                // the value
        test    r11b, 1
        jnz     @negate
        ret
        // ---- p digits (1..15): <= 8 digits, dword 0 = the value ----------------------------------------
@negate:
        neg     rax
        ret
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------
@wideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        test    r11b, 1
        jnz     @negate
        ret
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:                               // the Pascal allows spaces after the sign: the byte path handles them (p = 0)
        inc     rcx
        or      r11d, 1
        mov     eax, ecx
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        jmp     @unsigned
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        add     rcx, 16
        mov     r9d, 49
        jmp     @next
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- digits byte by byte: rax = the value so far (wrapping like the Pascal), at most 64 here --
@digitLoop:                              // r10 = the digit, r9 = the budget
        imul    rax, rax, 10
        add     rax, r10
        inc     rcx
        dec     r9
        jz      @signed                  // the digit budget is spent: the value so far
@next:
        movzx   r10d, byte ptr [rcx]
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @digitLoop
@signed:                                 // the sign without a branch (the loop end leaves no room for a jump pair)
        mov     r10, rax
        neg     r10
        test    r11b, 1
        cmovnz  rax, r10
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- nil, NUL, no digit, a phase beyond its 64-byte budget: 0 ---------------------------------
@zero:
        xor     eax, eax
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --
@bytes:                                  // rcx = the first byte (after a consumed -: spaces only); each phase at most 64 bytes
        movzx   r10d, byte ptr [rcx]
        mov     r9d, 65                  // the budget of the next phase: 64 bytes beyond its first
        test    r11b, 1
        jnz     @afterSign
        cmp     r10d, 32
        ja      @signByte
        xor     eax, eax
@spaces:                                 // bytes <= 32 except NUL
        test    r10d, r10d
        jz      @zero                    // nil / NUL / the budget is spent
        add     rcx, 1
        dec     r9
        jz      @zero                    // nil / NUL / the budget is spent
        movzx   r10d, byte ptr [rcx]
        cmp     r10d, 32
        jbe     @spaces
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     r9d, 65                  // the budget of the next phase: 64 bytes beyond its first
@signByte:
        cmp     r10d, 45
        jne     @plusByte
        or      r11d, 1
        jmp     @signSkip
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
@plusByte:
        cmp     r10d, 43
        jne     @firstDigit
@signSkip:                               // past the sign (not charged to the budget)
        add     rcx, 1
        movzx   r10d, byte ptr [rcx]
@afterSign:
        cmp     r10d, 32
        jne     @firstDigit
@signSpaces:                             // spaces after the sign: at most 64
        add     rcx, 1
        dec     r9
        jz      @zero                    // nil / NUL / the budget is spent
        movzx   r10d, byte ptr [rcx]
        cmp     r10d, 32
        je      @signSpaces
@firstDigit:
        xor     eax, eax
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     r9d, 65                  // the budget of the next phase: 64 bytes beyond its first
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        sub     r10d, 48
        cmp     r10d, 9
        ja      @zero
        jmp     @digitLoop
{$endif SYSVABI}
end;

function GetInteger(P: PUtf8Char; var err: integer): PtrInt;
{$ifdef FPC} nostackframe; assembler; asm {$else} asm .noframe {$endif}
{$ifdef SYSVABI}
        mov     rdx, rsi
        xor     r11d, r11d
        test    rdi, rdi
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
        mov     eax, edi
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        and     eax, 4095
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        cmp     byte ptr [rdi], '-'
        je      @negative
        lea     r8, [rip + NumberSimdData]
@unsigned:                               // eax = P mod 4096 (after the sign), r11 = the sign, r8 = the table
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
        lea     r10d, [rax - 1]
        cmp     r10d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        movzx   r9d, byte ptr [rdi + rax]// the byte after the digits: NUL for err = 0
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @wideRows                // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is the magnitude
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
         db      $3e    // PAD: DS prefix, no uop
        movd    eax, xmm0                // the magnitude
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        mov     dword ptr [rdx], 0
        test    r9d, r9d
        jnz     @invalid                 // not NUL: err = 1, the magnitude
        test    r11b, 1
        jnz     @negate
        ret
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
        // ---- p digits (1..15), terminator at [rdi + p]: <= 8 digits, dword 0 = the magnitude ------
@negate:
        neg     rax
        ret
@invalid:                                // err = 1, rax = the magnitude read so far (unnegated, like the Pascal exit)
        mov     dword ptr [rdx], 1
        ret
         db      $0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------
@wideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        mov     dword ptr [rdx], 0
        test    r9d, r9d
        jnz     @invalid                 // not NUL: err = 1, the magnitude
        test    r11b, 1
        jnz     @negate
        ret
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:                               // the Pascal allows spaces after the sign: the byte path handles them (p = 0)
        inc     rdi
        or      r11d, 1
        mov     eax, edi
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        jmp     @unsigned
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        add     rdi, 15
        mov     r9, 1000000000000000000  // 10^18: 19 significant digits kept
        test    rax, rax
        jnz     @next
        mov     r8d, 50
        jmp     @zeroLoop
@invalidLoop:                            // err = 1, rax = the 19 digits (a copy next to the loop: a short jump keeps it in one window)
        mov     dword ptr [rdx], 1
        ret
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
        // ---- digits byte by byte: rax = the magnitude, 19 significant digits at most ----------------
@digitLoop:                              // r10 = the digit
        cmp     rax, r9
        jae     @invalidLoop             // a 20th significant digit: err = 1, the 19 digits
        imul    rax, rax, 10
        add     rax, r10
@next:
        inc     rdi
        movzx   r10d, byte ptr [rdi]
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @digitLoop
@ended:                                  // r10d = terminator - 48: NUL, then the Int64 range (2^63 allowed when negative)
        mov     dword ptr [rdx], 0
        cmp     r10d, -48
        jne     @invalid
        mov     r8d, r11d
        and     r8d, 1
        mov     r9, 9223372036854775807
        add     r9, r8
        cmp     rax, r9
        ja      @invalid                 // beyond Int64: err = 1, the magnitude
        mov     r10, rax
        neg     r10
        test    r11b, 1
        cmovnz  rax, r10
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
        // ---- leading zeros while the magnitude is 0: free, at most 64 (r8 counts) ------------------
@zeros:
        mov     r8d, 65                  // the budget of the next phase: 64 bytes beyond its first
@zeroLoop:
        add     rdi, 1
        movzx   r10d, byte ptr [rdi]
        sub     r10d, 48
        jnz     @zeroEnd
        dec     r8
        jnz     @zeroLoop
        mov     dword ptr [rdx], 1
        ret
@zeroEnd:                                // r10d = byte - 48, nonzero
        cmp     r10d, 9
        jbe     @digitLoop
        jmp     @ended
         db      $0f,$1f,$00    // gap after ret/jmp, never executed
        // ---- nil, NUL or no digit before any digit, a phase beyond its 64-byte budget: 0 with err = 1 ----
@zeroInvalid:
        xor     eax, eax
        mov     dword ptr [rdx], 1
        ret
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --
@bytes:                                  // rdi = the first byte (after a consumed -: spaces only); each phase at most 64 bytes
        xor     eax, eax
        movzx   r10d, byte ptr [rdi]
        mov     r8d, 65                  // the budget of the next phase: 64 bytes beyond its first
        test    r11b, 1
        jnz     @afterSign
        cmp     r10d, 32
        ja      @signByte
        xor     eax, eax
@spaces:                                 // bytes <= 32 except NUL
        test    r10d, r10d
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
        add     rdi, 1
        dec     r8
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
        movzx   r10d, byte ptr [rdi]
        cmp     r10d, 32
        jbe     @spaces
         db      $3e    // PAD: DS prefix, no uop
        mov     r8d, 65                  // the budget of the next phase: 64 bytes beyond its first
@signByte:
        cmp     r10d, 45
        jne     @plusByte
        or      r11d, 1
        jmp     @signSkip
         db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00    // gap after ret/jmp, never executed
         db      $0f,$1f,$40,$00    // gap after ret/jmp, never executed
@plusByte:
        cmp     r10d, 43
        jne     @firstDigit
@signSkip:                               // past the sign (not charged to the budget)
        add     rdi, 1
        movzx   r10d, byte ptr [rdi]
@afterSign:
         db      $3e    // PAD: DS prefix, no uop
         db      $3e    // PAD: DS prefix, no uop
        cmp     r10d, 32
        jne     @firstDigit
@signSpaces:                             // spaces after the sign: at most 64
        add     rdi, 1
        dec     r8
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
        movzx   r10d, byte ptr [rdi]
        cmp     r10d, 32
        je      @signSpaces
@firstDigit:
        mov     r9, 1000000000000000000  // 10^18: 19 significant digits kept
        sub     r10d, 48
        cmp     r10d, 9
        ja      @zeroInvalid
        xor     eax, eax
        test    r10d, r10d
        jz      @zeros
        jmp     @digitLoop
{$else}
        xor     r11d, r11d
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        test    rcx, rcx
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
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
@unsigned:                               // eax = P mod 4096 (after the sign), r11 = the sign, r8 = the table
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
        lea     r10d, [rax - 1]
        cmp     r10d, 14
        ja      @window0or16             // p = 0: no leading digit; p = 16: the window is full
        movzx   r9d, byte ptr [rcx + rax]// the byte after the digits: NUL for err = 0
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        cmp     eax, 8
        ja      @wideRows                // 9 .. 15 digits
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLSHORT]// <= 8 digits right-aligned into lanes 0..7: dword 0 is the magnitude
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        movd    eax, xmm0                // the magnitude
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     dword ptr [rdx], 0
        test    r9d, r9d
        jnz     @invalid                 // not NUL: err = 1, the magnitude
        test    r11b, 1
        jnz     @negate
        ret
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- p digits (1..15), terminator at [rcx + p]: <= 8 digits, dword 0 = the magnitude ------
@negate:
        neg     rax
        ret
@invalid:                                // err = 1, rax = the magnitude read so far (unnegated, like the Pascal exit)
        mov     dword ptr [rdx], 1
        ret
        {$ifndef FPC} db      $0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- 9 .. 15 digits right-aligned into lanes 1..15: H * 10^8 + L ---------------------------
@wideRows:
        shl     eax, 4
        pshufb  xmm0, [r8 + rax + NUMBER_CTRLINT]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        mov     dword ptr [rdx], 0
        test    r9d, r9d
        jnz     @invalid                 // not NUL: err = 1, the magnitude
        test    r11b, 1
        jnz     @negate
        ret
        // ---- the minus sign, out of line: positive numbers fall through at the entry ------------
@negative:                               // the Pascal allows spaces after the sign: the byte path handles them (p = 0)
        inc     rcx
        or      r11d, 1
        mov     eax, ecx
        and     eax, 4095
        or      eax, dword ptr [rip + NumberNoSimd]// 4096 on a CPU without SSSE3: the byte path then (the same contract)
        lea     r8, [rip + NumberSimdData]
        jmp     @unsigned
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- the window is full (16 digits): continue byte by byte from byte 16 -------------
@window0or16:                            // eax = p: 0 = no leading digit, 16 = full
        test    eax, eax
        jz      @bytes
        psubb   xmm0, [r8 + NUMBER_ASCII0]
        pmaddubsw xmm0, [r8 + NUMBER_TEN]
        pmaddwd xmm0, [r8 + NUMBER_HUNDRED]
        packssdw xmm0, xmm0
        pmaddwd xmm0, [r8 + NUMBER_TENTHOUSAND]
        movq    rax, xmm0
        mov     r10d, eax
        shr     rax, 32
        imul    r10, r10, 100000000
        add     rax, r10
        add     rcx, 15
        mov     r9, 1000000000000000000  // 10^18: 19 significant digits kept
        test    rax, rax
        jnz     @next
        mov     r8d, 50
        jmp     @zeroLoop
@invalidLoop:                            // err = 1, rax = the 19 digits (a copy next to the loop: a short jump keeps it in one window)
        mov     dword ptr [rdx], 1
        ret
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- digits byte by byte: rax = the magnitude, 19 significant digits at most ----------------
@digitLoop:                              // r10 = the digit
        cmp     rax, r9
        jae     @invalidLoop             // a 20th significant digit: err = 1, the 19 digits
        imul    rax, rax, 10
        add     rax, r10
@next:
        inc     rcx
        movzx   r10d, byte ptr [rcx]
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @digitLoop
@ended:                                  // r10d = terminator - 48: NUL, then the Int64 range (2^63 allowed when negative)
        mov     dword ptr [rdx], 0
        cmp     r10d, -48
        jne     @invalid
        mov     r8d, r11d
        and     r8d, 1
        mov     r9, 9223372036854775807
        add     r9, r8
        cmp     rax, r9
        ja      @invalid                 // beyond Int64: err = 1, the magnitude
        mov     r10, rax
        neg     r10
        test    r11b, 1
        cmovnz  rax, r10
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- leading zeros while the magnitude is 0: free, at most 64 (r8 counts) ------------------
@zeros:
        mov     r8d, 65                  // the budget of the next phase: 64 bytes beyond its first
@zeroLoop:
        add     rcx, 1
        movzx   r10d, byte ptr [rcx]
        sub     r10d, 48
        jnz     @zeroEnd
        dec     r8
        jnz     @zeroLoop
        mov     dword ptr [rdx], 1
        ret
@zeroEnd:                                // r10d = byte - 48, nonzero
        cmp     r10d, 9
        jbe     @digitLoop
        jmp     @ended
        {$ifndef FPC} db      $0f,$1f,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- nil, NUL or no digit before any digit, a phase beyond its 64-byte budget: 0 with err = 1 ----
@zeroInvalid:
        xor     eax, eax
        mov     dword ptr [rdx], 1
        ret
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
        // ---- the Pascal byte rules from the start: bytes <= 32 (not NUL), one sign, spaces, digits --
@bytes:                                  // rcx = the first byte (after a consumed -: spaces only); each phase at most 64 bytes
        xor     eax, eax
        movzx   r10d, byte ptr [rcx]
        mov     r8d, 65                  // the budget of the next phase: 64 bytes beyond its first
        test    r11b, 1
        jnz     @afterSign
        cmp     r10d, 32
        ja      @signByte
        xor     eax, eax
@spaces:                                 // bytes <= 32 except NUL
        test    r10d, r10d
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
        add     rcx, 1
        dec     r8
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
        movzx   r10d, byte ptr [rcx]
        cmp     r10d, 32
        jbe     @spaces
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        mov     r8d, 65                  // the budget of the next phase: 64 bytes beyond its first
@signByte:
        cmp     r10d, 45
        jne     @plusByte
        or      r11d, 1
        jmp     @signSkip
        {$ifndef FPC} db      $66,$0f,$1f,$84,$00,$00,$00,$00,$00 {$endif}   // gap after ret/jmp, never executed
        {$ifndef FPC} db      $0f,$1f,$40,$00 {$endif}   // gap after ret/jmp, never executed
@plusByte:
        cmp     r10d, 43
        jne     @firstDigit
@signSkip:                               // past the sign (not charged to the budget)
        add     rcx, 1
        movzx   r10d, byte ptr [rcx]
@afterSign:
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop
        cmp     r10d, 32
        jne     @firstDigit
@signSpaces:                             // spaces after the sign: at most 64
        add     rcx, 1
        dec     r8
        jz      @zeroInvalid             // NUL before a digit / the budget is spent: err = 1, 0
        movzx   r10d, byte ptr [rcx]
        cmp     r10d, 32
        je      @signSpaces
@firstDigit:
        mov     r9, 1000000000000000000  // 10^18: 19 significant digits kept
        sub     r10d, 48
        cmp     r10d, 9
        ja      @zeroInvalid
        xor     eax, eax
        test    r10d, r10d
        jz      @zeros
        jmp     @digitLoop
{$endif SYSVABI}
end;
