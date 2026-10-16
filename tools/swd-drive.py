#!/usr/bin/env python3
"""swd-drive.py — drive the running shell over SWD, with no UART, so the
release build can be scripted (EL §13.2; hardware-notes.md §2.7).

  tools/swd-drive.py build/pico-release/BOOT2350.elf type 'ls\r'
  tools/swd-drive.py build/pico-release/BOOT2350.elf screen
  tools/swd-drive.py build/pico-release/BOOT2350.elf stats

The ELF must be the image running (tools/swd-ram.sh starts one): its
symbols give the addresses. `type` writes key-press events into kbd.c's
ring and then moves its head, as kbd_poll would; the main loop only moves
the tail, so this is safe while nobody presses a key on the keyboard. CR
and LF type Enter, BS Backspace. `screen` reads the terminal's grid and
prints it as text; `stats` the start times and counters. Every read is
taken twice and kept only when both agree (hardware-notes.md §2.7).
Nothing is halted.
"""
import glob, os, shutil, subprocess, sys

elf, cmd = sys.argv[1], sys.argv[2]
ocd_dir = sorted(glob.glob(os.path.expanduser('~/.pico-sdk/openocd/*/')))[-1]
nm = shutil.which('arm-none-eabi-nm') or sorted(glob.glob(os.path.expanduser(
    '~/.pico-sdk/toolchain/*/bin/arm-none-eabi-nm')))[-1]
syms = {}
for line in subprocess.run([nm, '-S', elf], capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 4: syms[p[3]] = int(p[0], 16)

def ocd(cmds):
    args = [ocd_dir + 'openocd', '-s', ocd_dir + 'scripts', '-f', 'interface/cmsis-dap.cfg',
            '-f', 'target/rp2350.cfg', '-c', 'adapter speed 5000', '-c', 'init']
    for c in cmds: args += ['-c', c]
    args += ['-c', 'exit']
    r = subprocess.run(args, capture_output=True, text=True)
    return [l for l in r.stderr.splitlines() + r.stdout.splitlines() if l.startswith('R:')]

def read(addr, n):  # n 32-bit words, read twice until both agree (HW §2.7)
    for _ in range(5):
        out = ocd([f'echo "R:[read_memory {addr:#x} 32 {n}]"', f'echo "R:[read_memory {addr:#x} 32 {n}]"'])
        a, b = out[0][2:].split(), out[1][2:].split()
        if a == b: return [int(x, 16) for x in a]
    raise SystemExit('reads never agreed')

RING = 64   # SHELL_KEY_RING in src/core/config.h

if cmd == 'type':
    text = sys.argv[3].encode().decode('unicode_escape')
    ring, head = syms['s_ring'], syms['s_head']
    h = read(head, 1)[0]
    cmds = []
    for i, ch in enumerate(text):
        code = {'\r': 0x0A, '\n': 0x0A, '\b': 0x08}.get(ch, ord(ch))
        cmds.append(f'write_memory {ring + 2 * ((h + i) % RING):#x} 16 {{{(1 << 8) | code:#x}}}')
    cmds.append(f'write_memory {head:#x} 32 {{{h + len(text):#x}}}')
    ocd(cmds)
elif cmd == 'screen':
    t = syms['g_term']
    words = read(t, 32 * 64 * 6 // 4)
    raw = b''.join(w.to_bytes(4, 'little') for w in words)
    for r in range(32):
        row = raw[r * 64 * 6:(r + 1) * 64 * 6]
        s = ''.join(chr(row[c * 6]) if 0x20 <= row[c * 6] < 0x7f else '.' for c in range(64))
        print(s.rstrip())
elif cmd == 'stats':
    for name in [n for n in ['s_main_us', 's_lcd_us', 's_prompt_us'] if n in syms]:
        lo, hi = read(syms[name], 2); print(name, lo | hi << 32)
    for name in ['s_rows_drawn', 's_cells_drawn', 's_overflows', 's_errors']:
        print(name, read(syms[name], 1)[0])
