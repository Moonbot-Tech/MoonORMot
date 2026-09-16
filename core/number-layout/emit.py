"""Render solved instruction sheets; layout.py owns source generation for both ABIs."""
PAD = "        {$ifndef FPC} db      $3e {$endif}   // PAD: DS prefix, no uop\n"
NOPS = {1: '$90', 2: '$66,$90', 3: '$0f,$1f,$00', 4: '$0f,$1f,$40,$00', 5: '$0f,$1f,$44,$00,$00',
        6: '$66,$0f,$1f,$44,$00,$00', 7: '$0f,$1f,$80,$00,$00,$00,$00', 8: '$0f,$1f,$84,$00,$00,$00,$00,$00',
        9: '$66,$0f,$1f,$84,$00,$00,$00,$00,$00'}
def gap(n):
    out = []
    while n > 9: out.append(NOPS[9]); n -= 9
    if n: out.append(NOPS[n])
    return ''.join(f"        {{$ifndef FPC}} db      {enc} {{$endif}}   // gap after ret/jmp, never executed\n" for enc in out)

def render(R, text):
    if text.startswith('tzcnt '):
        return '        db      $f3                      // TZCNT (BSF gives the same for this nonzero operand)\n' + render(R, 'bsf ' + text[6:])
    src = text
    for k, v in R.MEM.items():
        if k in src: src = src.replace(k, v)
    mn, _, ops = src.partition(' ')
    line = f'        {mn:<7s} {ops}'.rstrip()
    if text in R.INS:
        line = f'{line:<41s}// {R.INS[text]}'
    return line + '\n'

def emit(R, laid):
    out = []
    for text, size in laid:
        if text == 'PAD': out.append(PAD); continue
        if text.startswith('NOP'): out.append(gap(int(text[3:]))); continue
        if text.endswith(':'):
            name = text[:-1]
            head, note = R.COMMENT.get(name, (None, ''))
            if head: out.append(head + '\n')
            out.append(f'{text:<41s}// {note}\n' if note else text + '\n')
            continue
        out.append(render(R, text))
    return ''.join(out)
