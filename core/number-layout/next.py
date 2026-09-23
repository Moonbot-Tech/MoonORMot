"""GetNextExtended / GetNextInt64: GetExtended / GetInteger(P, err) that leave P on the byte after the number.

python next.py           writes ../mormot.core.base.asmx64.number.next.inc
python next.py --check   fails if that include no longer matches the current string/integer includes

Both are derived from the Win64 bodies of mormot.core.base.asmx64.number.string.inc and .integer.inc.
Only the cursor, the accepted ending and the JSON grammar gates are added: the numeric instructions stay
the originals' (asserted below). Rerun after any change of those two routines; tests/numbers/verify.py
also compares both entries with the originals. FPC uses the same Win64 body on SysV (ms_abi_default):
@P and Ending stay in the shadow slots [rsp + 8] / [rsp + 16].
Ending: 0 = the NUL-ended text of the original; 34 = the same text ended by a closing quote; 256 = an
unquoted JSON number (JSON grammar), err = 0 before a JSON delimiter (NUL, spaces, ',', ']', '}').
"""
import re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
STRING = HERE.parent / 'mormot.core.base.asmx64.number.string.inc'
INTEGER = HERE.parent / 'mormot.core.base.asmx64.number.integer.inc'
OUTPUT = HERE.parent / 'mormot.core.base.asmx64.number.next.inc'


class Text:
    """The Win64 body under edit: every edit asserts the text it expects."""
    def __init__(self, body):
        self.body = body

    def change(self, old, new, count=1):
        assert self.body.count(old) == count, (old, self.body.count(old), count)
        self.body = self.body.replace(old, new)

    def at(self, label, extra):
        pattern = r'^' + re.escape(label) + r':[^\n]*\n'
        self.body, count = re.subn(pattern, lambda m: m[0] + extra, self.body, flags=re.M)
        assert count == 1, label


def win64(source, header):
    text = source.read_text(encoding='utf-8').split(header, 1)[1]
    body = text.split('{$else}\n', 1)[1].split('{$endif SYSVABI}', 1)[0]
    return '\n'.join(line for line in body.splitlines() if '{$ifndef FPC}' not in line)


def publish(reg='rax'):
    return f'        mov     {reg}, [rsp + 8]\n        mov     [{reg}], rcx\n'


def numeric_ops(text):
    return [line.split('//')[0].strip() for line in text.splitlines()
            if 'xmm' in line.split('//')[0] and 'xmm5' not in line.split('//')[0]]


def extended():
    t = Text(win64(STRING, 'function GetExtended(P: PUtf8Char; out err: integer): TSynExtended;'))
    before = t.body
    t.change('        xor     r11d, r11d', '        mov     r11d, r8d\n        and     r11d, 256')
    t.at('@unsigned', '''        test    r11d, 256
        jz      @jsonStartChecked
        movzx   r8d, byte ptr [rcx]
        sub     r8d, 48
        cmp     r8d, 9
        ja      @nil
        test    r8d, r8d
        jnz     @jsonStartChecked
        movzx   r8d, byte ptr [rcx + 1]
        sub     r8d, 48
        cmp     r8d, 9
        jbe     @nil
@jsonStartChecked:
''')
    # Integer RCX becomes the already discovered terminator; dot branch keeps start.
    t.change("        je      @fraction\n", "        je      @fraction\n        lea     rcx, [rcx + rax]\n")
    # Fraction RCX likewise moves once n is known (the full-window branch stays upstream).
    t.change('        movzx   r9d, byte ptr [rcx + r10]\n',
             '        movzx   r9d, byte ptr [rcx + r10]\n        lea     rcx, [rcx + r10]\n', 2)
    t.change('        test    r9d, r9d\n', '        cmp     r9d, dword ptr [rsp + 16]\n', 4)
    for label in ('@integerShortTail', '@integerWide', '@fractionShortValue', '@fractionValue'):
        t.at(label, publish())
    # The digit count no longer participates in locating the exponent.
    t.change('        shr     eax, 4\n        lea     rcx, [rcx + rax + 1]\n', '        inc     rcx\n', 2)
    t.change('        shr     eax, 4\n        shr     r10d, 3\n        lea     rcx, [rcx + r10]\n        lea     rcx, [rcx + rax + 1]\n',
             '        shr     r10d, 3\n        inc     rcx\n', 2)
    # Preserve the actual delimiter byte while testing e/E.
    t.change("        or      r9d, 32\n        cmp     r9d, 'e'\n",
             "        mov     eax, r9d\n        or      eax, 32\n        cmp     eax, 'e'\n", 4)
    for label, exponent in (('@integerShortEnded', '@integerShortExponent'), ('@integerEnded', '@integerExponent'),
                            ('@fractionShortEnded', '@fractionShortExponent'), ('@fractionEnded', '@fractionExponent')):
        anchor = f'        je      {exponent}\n        mov     dword ptr [rdx], 1\n'
        t.change(anchor, anchor + f'''        test    r11d, 256
        jz      {label}Publish
        lea     rax, [rip + JsonEndErrors]
        movzx   eax, byte ptr [rax + r9]
        mov     dword ptr [rdx], eax
{label}Publish:
''' + publish())
    # Raw JSON requires a digit after '.', while ToDouble's string grammar stays intact.
    t.change('        bsf     r10d, r10d               // n = first non-digit after the fraction digits, p+1 .. 16\n',
             '''        bsf     r10d, r10d               // n = first non-digit after the fraction digits, p+1 .. 16
        test    r11d, 256
        jz      @jsonFractionChecked
        lea     r9d, [rax + 1]
        cmp     r10d, r9d
        jne     @jsonFractionChecked
        cmp     r10d, 16
        jne     @nil
        movzx   r9d, byte ptr [rcx + 16]
        sub     r9d, 48
        cmp     r9d, 9
        ja      @nil
@jsonFractionChecked:
''')
    t.change('        ja      @afterDigits\n', '        ja      @fractionNoDigit\n')
    # Unrolled exponent paths report the endpoint they already read.
    t.change('        ja      @feEnd\n', '        ja      @feEnd1\n', 2)
    # The second occurrence is the two-digit end.
    first = t.body.index('        ja      @feEnd1\n')
    second = t.body.index('        ja      @feEnd1\n', first + 1)
    t.body = t.body[:second] + t.body[second:].replace('@feEnd1', '@feEnd2', 1)
    t.change('        jbe     @feGeneric\n', '        jbe     @feGeneric\n        lea     rcx, [rcx + r8 + 3]\n')
    t.at('@feEnd', publish('r8'))
    t.change('        xor     r8d, r8d\n        cmp     r10d, -48\n        setne   r8b\n', '''        lea     r8d, [r10 + 48]
        cmp     r8d, dword ptr [rsp + 16]
        setne   r8b
        test    r11d, 256
        jz      @feStatus
        lea     eax, [r10 + 48]
        lea     r8, [rip + JsonEndErrors]
        movzx   r8d, byte ptr [r8 + rax]
@feStatus:
''')
    # Generic conversion must receive a cursor saved before RCX becomes arithmetic scratch.
    t.change('        jne     @terminator\n', '        jne     @afterDigitsEnd\n')
    t.at('@exponentEnd', '        movq    xmm5, rax\n' + publish() + '        movq    rax, xmm5\n')
    t.change('        ja      @exponentNone\n', '        ja      @exponentNoDigit\n')
    t.change('        cmp     al, 208                  // NUL\n        setne   r10b\n', '''        add     al, 48
        movzx   eax, al
        cmp     eax, dword ptr [rsp + 16]
        setne   r10b
        test    r11d, 256
        jz      @convert
        lea     r10, [rip + JsonEndErrors]
        movzx   r10d, byte ptr [r10 + rax]
''')
    t.body += '''
@feEnd1:
        lea     rcx, [rcx + r8 + 1]
        jmp     @feEnd
@feEnd2:
        lea     rcx, [rcx + r8 + 2]
        jmp     @feEnd
@afterDigitsEnd:
        dec     rcx
''' + publish('r10') + '''        jmp     @terminator
@exponentNoDigit:
''' + publish('r10') + '''        jmp     @exponentNone
@fractionNoDigit:
        test    r11d, 256
        jnz     @nil
        jmp     @afterDigits
'''
    # Numeric SIMD/FP instructions, their operands and sequence are identical.
    assert numeric_ops(before) == numeric_ops(t.body)
    # Continue a long exponent with its three digits already accumulated. Keep
    # upstream's zero-run count, signed-overflow checks and numeric conversion.
    t.change('        ja      @feGeneric\n', '        ja      @feNoDigit\n')
    t.change('        jbe     @feGeneric\n', '        jbe     @feContinue\n')
    t.body = re.sub(r'^@feGeneric:[^\n]*\n', '@feContinue:\n        lea     rcx, [rcx + r8 + 3]\n', t.body, flags=re.M)
    t.change('        cvttsd2si r8, xmm0\n        jmp     @exponent\n', '''        cvttsd2si r8, xmm0
        xchg    eax, r10d
        test    r10d, r10d
        jnz     @exponentLoop
        test    eax, eax
        jnz     @exponentLoop
        mov     r10d, 3
        jmp     @exponentZeros
''')
    t.body += '''@feNoDigit:
        lea     rcx, [rcx + r8]
        cvttsd2si r8, xmm0
        jmp     @exponentNoDigit
'''
    assert numeric_ops(t.body) == numeric_ops(before) + ['cvttsd2si r8, xmm0']
    # Keep raw-JSON grammar gates out of the hot quoted path.
    for checked, cold in (('@jsonStartChecked', '@rawStart'), ('@jsonFractionChecked', '@rawFraction')):
        gate = '        test    r11d, 256\n        jz      ' + checked + '\n'
        begin = t.body.index(gate)
        end = t.body.index(checked + ':\n', begin)
        checks = t.body[begin + len(gate):end]
        t.body = t.body[:begin] + '        test    r11d, 256\n        jnz     ' + cold + '\n' + t.body[end:]
        t.body += cold + ':\n' + checks + '        jmp     ' + checked + '\n'
    return t.body


def arithmetic(text):
    # Include every RAX arithmetic operation plus all SIMD, multiplier, bound,
    # zero-run and sign instructions. Cursor/status/control edits are excluded.
    ops = []
    for line in text.splitlines():
        line = line.split('//')[0].strip()
        if ('xmm' in line or re.match(r'(imul|add|sub|neg|shr|shl|cmp|xor|cmovnz)\s+(rax|eax)\b', line)
                or re.match(r'(imul|add|shr)\s+r10,', line)
                or '1000000000000000000' in line or '9223372036854775807' in line
                or re.match(r'(mov|dec)\s+r8[d]?, (50|65)$', line) or line == 'dec     r8'):
            ops.append(line)
    return ops


def int64():
    t = Text(win64(INTEGER, 'function GetInteger(P: PUtf8Char; var err: integer): PtrInt;') + '\n')
    before = t.body
    # The integer return occupies RAX; R8 is disposable at every existing return.
    t.change('        ret\n', '        mov     r8, [rsp + 8]\n        mov     [r8], rcx\n        ret\n', t.body.count('        ret\n'))
    anchor = '        movzx   r9d, byte ptr [rcx + rax]// the byte after the digits: NUL for err = 0\n'
    t.change(anchor, anchor + '        lea     rcx, [rcx + rax]\n')
    t.change('        test    r9d, r9d\n        jnz     @invalid',
             '        cmp     r9d, dword ptr [rsp + 16]\n        jnz     @shortDelimiter', 2)
    t.change('        cmp     r10d, -48\n        jne     @invalid\n',
             '        add     r10d, 48\n        cmp     r10d, dword ptr [rsp + 16]\n        jne     @longDelimiter\n@range:\n')
    # Raw JSON starts with a digit (after its optional minus) and disallows 01.
    # Numeric strings (quoted and NUL) keep the original ToInt64 grammar.
    anchor = '@unsigned:                               // eax = P mod 4096 (after the sign), r11 = the sign, r8 = the table\n'
    t.change(anchor, anchor + '        cmp     dword ptr [rsp + 16], 256\n        je      @rawStart\n@startChecked:\n')
    # A literal control byte is illegal inside JSON quotes. Mode 0 remains the
    # exact ToInt64 contract; escaped controls are decoded and passed to ToInt64.
    t.change('@spaces:                                 // bytes <= 32 except NUL\n',
             '@spaces:                                 // bytes <= 32 except NUL\n'
             '        cmp     r10d, 32\n'
             '        je      @spaceAllowed\n'
             '        cmp     dword ptr [rsp + 16], 34\n'
             '        je      @zeroInvalid\n'
             '@spaceAllowed:\n')
    t.body += '''@rawStart:
        movzx   r10d, byte ptr [rcx]
        sub     r10d, 48
        cmp     r10d, 9
        ja      @zeroInvalid
        test    r10d, r10d
        jnz     @startChecked
        movzx   r10d, byte ptr [rcx + 1]
        sub     r10d, 48
        cmp     r10d, 9
        jbe     @zeroInvalid
        jmp     @startChecked
@shortDelimiter:
        cmp     dword ptr [rsp + 16], 256
        jne     @invalid
        lea     r8, [rip + JsonEndErrors]
        cmp     byte ptr [r8 + r9], 0
        jne     @invalid
        test    r11b, 1
        jnz     @negate
        mov     r8, [rsp + 8]
        mov     [r8], rcx
        ret
@longDelimiter:
        cmp     dword ptr [rsp + 16], 256
        jne     @invalid
        lea     r8, [rip + JsonEndErrors]
        cmp     byte ptr [r8 + r10], 0
        jne     @invalid
        jmp     @range
'''
    assert arithmetic(before) == arithmetic(t.body)
    return t.body


def entry(header, body):
    if not body.endswith('\n'):
        body += '\n'
    return f'''{header}
{{$ifdef FPC}} ms_abi_default; nostackframe; assembler; asm {{$else}} asm .noframe {{$endif}}
        mov     [rsp + 8], rcx
        mov     [rsp + 16], r8d
        mov     rcx, [rcx]
{body}end;
'''


def generate():
    errors = [0 if i in (0, 9, 10, 13, 32, 44, 93, 125) else 1 for i in range(256)]
    table = ',\n    '.join(','.join(map(str, errors[i:i + 32])) for i in range(0, 256, 32))
    return f'''// GetNextExtended / GetNextInt64: generated by number-layout/next.py from the Win64 bodies of
// GetExtended (.string.inc) and GetInteger(P, err) (.integer.inc) - do not edit, rerun the script.

const
  // err after an unquoted JSON number: 0 before a JSON delimiter (NUL, spaces, ',', ']', '}}')
  JsonEndErrors: array[byte] of byte = (
    {table});

''' + entry('function GetNextExtended(var P: PUtf8Char; out err: integer; Ending: integer): double;', extended()) + '\n' + \
        entry('function GetNextInt64(var P: PUtf8Char; out err: integer; Ending: integer): Int64;', int64())


if __name__ == '__main__':
    text = generate()
    if sys.argv[1:] == ['--check']:
        assert OUTPUT.read_text(encoding='utf-8') == text, 'the cursor entries differ from the current string/integer routines: run next.py'
        print('NUMERIC_NEXT_PASS')
    else:
        OUTPUT.write_text(text, encoding='utf-8', newline='\n')
        print('generated', OUTPUT.name)
