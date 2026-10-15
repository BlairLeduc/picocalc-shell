# PicoCalc shell — user guide

The shell is a small UNIX-like command line for the ClockworkPi PicoCalc
with an RP2350 board. From it you manage the files on the SD card, edit
text files in vi, and run programs by name. This guide covers every
command and the command line itself. Section 9 covers the DOS flavour,
a second build with DOS's commands. To install the shell, see the
[README](../README.md).

## Contents

1. [Getting started](#1-getting-started)
2. [Keys](#2-keys)
3. [The command line](#3-the-command-line)
4. [Commands](#4-commands)
5. [Running programs](#5-running-programs)
6. [vi](#6-vi)
7. [The card](#7-the-card)
8. [Messages and limits](#8-messages-and-limits)
9. [The DOS flavour](#9-the-dos-flavour)

## 1. Getting started

Hold **Up** (or F1 or F5) while switching the PicoCalc on, and the shell
starts. Powering on with no key held starts the last program you ran,
as the loader always has.

The prompt is the current directory followed by `>`:

```
/home>ls
notes  todo.txt
/home>cat todo.txt
buy batteries
/home>cd /oric
/oric>
```

The shell starts in `/home` if the card has one, and otherwise in `/`.

`help` lists the commands, and `help name` explains one:

```
/>help cp
usage: cp [-r] from... to
copy files, -r directories
```

## 2. Keys

### At the prompt

| Key | Does |
|---|---|
| Left, Right | move along the line (also Ctrl-B, Ctrl-F) |
| Home, End | the start and end of the line (also Ctrl-A, Ctrl-E). Home is Shift+Tab, End is Shift+Del |
| Backspace, Del | delete before or under the cursor |
| Ctrl-U, Ctrl-K | delete to the start, or to the end, of the line |
| Up, Down | earlier and later lines from the history (also Ctrl-P, Ctrl-N) |
| Tab | complete a command or a path (below) |
| Ctrl-C | drop the line |
| Enter | run the line |

### While a command runs

**Esc**, **Break** (Shift+Esc) or **Ctrl-C** stops a long command, such
as a large `cp`, and drops the rest of the line. The shell prints `^C`.

### Tab completion

As the first word, Tab completes a command: a builtin, or a program in
`$PATH`, without its `.uf2`. Anywhere else it completes a path.

- One match goes in whole, followed by `/` for a directory or a space
  for anything else.
- With several matches, Tab puts in what they share, and a second Tab
  lists them.
- Names go in as the card spells them, with `\` before a space or any
  other character the shell treats as special.

### The PicoCalc's own keys

Alt+`,`, Alt+`.`, Alt+Space and Alt+B belong to the keyboard itself: it
uses them for the backlights and the battery, and the shell never sees
them. The `backlight` and `battery` commands (section 4) do the same
from the command line.

## 3. The command line

The shell is deliberately small. It has UNIX spelling but no `if`, `for`
or functions.

### Words and quoting

A line is split into words at spaces. To keep a space, or any special
character, in a word:

- `'single quotes'` keep everything as typed;
- `"double quotes"` keep everything but `$`, so variables still work;
- `\` keeps the next character: `my\ file.txt`.

`#` starts a comment, to the end of the line.

### Several commands

| Write | Runs |
|---|---|
| `a ; b` | `a`, then `b` |
| `a && b` | `b` only if `a` worked |
| `a \|\| b` | `b` only if `a` failed |
| `a \| b` | `a`, with its output given to `b` as input |

### Redirection

| Write | Does |
|---|---|
| `cmd > file` | the output into `file`, replacing it |
| `cmd >> file` | the output added to the end of `file` |
| `cmd < file` | `file` as the command's input |

A redirected file is written safely: if the command fails or is stopped,
the old file is left as it was. Error messages always go to the screen,
never into the file.

### Pipes

`a | b` runs `a` to the end, then gives its output to `b`. A pipe can
have several stages:

```
/>ls /pico2-apps | grep oric
pico-oric.uf2
/>cat log.txt | grep -n error | more
```

Up to 16 KB is held in memory. Larger output goes through a temporary
file in `/.shell/`, which is removed afterwards, so a pipe is limited only
by space on the card.

`more`, `grep`, `head`, `tail`, `wc` and `cat` read a pipe or a `<` file
when they are given no file names.

### Globs

`*` matches any characters, `?` matches one character, and `[abc]` or
`[a-z]` matches one of a set (`[!a-z]` matches one not in it):

```
/>rm *.bak
/>ls /oric/keymaps/[a-m]*
```

- Letters match without regard to case, as FAT names do.
- A pattern that matches nothing stays as typed, so `ls *.xyz` says
  `no such file`.
- Hidden names match only a pattern that starts with `.`.
- Only the last part of a path is expanded: `*/x` stays as typed.

### Variables

```
/>NAME=world
/>echo hello $NAME
hello world
/>echo ${NAME}s
worlds
```

- `$NAME` or `${NAME}` is a variable's value, and `$?` is the last
  command's status: 0 if it worked.
- `NAME=value` on its own sets a variable. `NAME=value cmd` is not
  supported.
- An unset variable is empty.
- Variables last until the shell is left. There is room for 1 KB of them.

Two variables matter to the shell:

| Variable | Default | Used for |
|---|---|---|
| `HOME` | `/home` | `cd` with no directory, and where the shell starts |
| `PATH` | `/pico2-apps` | where programs are looked for, directories separated by `:` |

### Statuses

Every command gives a status: 0 when it worked, and otherwise a number
that says how it failed.

- 1 is an ordinary failure.
- `grep` gives 1 for "nothing found" and 2 for an error.
- 127 means the command was not found.
- 130 means it was stopped with Esc, Break or Ctrl-C.

`&&`, `||` and `$?` use the status.

### History

The shell keeps about 2 KB of the lines you have typed. Up and Down bring
them back, and `history` lists them. A blank line, or one the same as the
one before, is not kept.

The history is saved to the card (`/.shell/history`) when you leave the
shell: for a program, `menu`, `usbdisk`, `reboot`, `poweroff` or
`bootsel`. Lines typed since the last save are lost if the power is
switched off.

### Scripts

`sh file` runs a file of commands, one line at a time. A mistake is
reported with the file's name and line number, and the script goes on.

`/etc/profile`, if the card has one, runs every time the shell starts.
It is the place for your own settings:

```
# /etc/profile
phosphor amber
font 5x10
PATH=/pico2-apps:/games
cd /home
```

Scripts can run other scripts, up to three deep.

## 4. Commands

Every command is built in, so it runs at once. Any other name runs a
program (section 5).

### Finding your way

| Command | Does |
|---|---|
| `pwd` | prints the current directory |
| `cd [dir]` | changes directory; to `$HOME` with none. `..` is the directory above |
| `ls [-l] [-a] [path...]` | lists a directory, sorted. `-l` adds the size, and `d` for a directory. `-a` includes hidden names |

```
/>ls -l /home
-rw       24 a.txt
drw          notes
```

### Files and directories

| Command | Does |
|---|---|
| `cat file...` | shows files one after another, or joins them: `cat a b > c` |
| `cp [-r] from... to` | copies a file, or several files into a directory. `-r` copies directories and everything in them |
| `mv from... to` | moves or renames files and directories |
| `rm [-r] path...` | removes files. `-r` removes directories and everything in them. **There is no undo** |
| `mkdir [-p] dir...` | makes directories. `-p` makes the ones above too, and does not mind if it is already there |
| `rmdir dir...` | removes empty directories |
| `touch file...` | makes empty files. A file that exists is left alone |
| `df` | the card's format, size, space used and space free |

Names on the card are case-insensitive but keep their case: `cat
TODO.TXT` reads `todo.txt`, and `ls` shows it as `todo.txt`. The
PicoCalc has no clock, so every file gets the same fixed date.

### Reading text

| Command | Does |
|---|---|
| `more file...` | shows text a screen at a time |
| `head [-n N] [file...]` | the first N lines, 10 if not given |
| `tail [-n N] [file...]` | the last N lines, 10 if not given |
| `grep [-v] [-n] [-c] [-i] pattern [file...]` | the lines that match a pattern |
| `wc [-l] [-w] [-c] [file...]` | counts lines, words and bytes |

Each of these reads the files named, or its input from `<` or a pipe.

**more** stops after each screenful at `--More--`. Then:

| Key | Does |
|---|---|
| Space | shows the next screenful |
| Enter or Down | shows one line more |
| q, Esc, Break or Ctrl-C | stops |

When its output goes to a file or a pipe, `more` just copies.

**head** and **tail** take `-n 5`, `-n5` or `-5`. With several files,
each one starts with `==> name <==`. `tail` keeps the last 4 KB of what
it reads. If the lines you ask for are longer than that, it shows the
whole lines that fit and says so.

**grep** uses vi's patterns, which always ignore case:

| Pattern | Matches |
|---|---|
| `.` | any character |
| `*` | any number of the one before |
| `^` and `$` | the start and the end of the line |
| `[abc]`, `[a-z]` | one character of a set |
| `\<` and `\>` | the start and the end of a word |

- `-v` gives the lines that do not match.
- `-n` adds line numbers.
- `-c` gives only how many lines matched.
- `-i` is accepted, and changes nothing.

With several files, each line starts with its file's name. The status is
0 if something matched, 1 if nothing did, and 2 on an error.

```
/>grep -n '^speed' /oric/pico-oric.cfg
4:speed=fast
```

**wc** shows lines, words and bytes, or just the ones asked for with
`-l`, `-w` or `-c`. Several files get a total line too.

```
/>wc notes.txt
     12      85     502 notes.txt
/>ls | wc -l
7
```

### Editing

| Command | Does |
|---|---|
| `vi [-R] file` | edits a text file. `-R` opens it read-only |
| `view file` | the same as `vi -R` |

See section 6.

### The screen

| Command | Does |
|---|---|
| `clear` | clears the screen |
| `font [8x10\|5x10]` | 40 columns (8x10, the default) or 64 columns (5x10). With no font, switches to the other one |
| `phosphor [white\|green\|amber\|default]` | the colour of the text, like an old monitor's phosphor. `phosphor` alone shows the current one |
| `echo [-n] [word...]` | prints its words. `-n` leaves off the line break |

`font` and `phosphor` last until the shell is left. Put them in
`/etc/profile` to have them every time.

### The PicoCalc

| Command | Does |
|---|---|
| `battery` | the battery's charge, and `charging` when the charger is on |
| `backlight [lcd\|kbd] [0-255]` | shows or sets the screen (`lcd`) or keyboard (`kbd`) backlight |
| `info` | the shell's version, the chip, memory, UF2 Loader, the keyboard's firmware, the card and the battery |

`battery`:

- The charge is read from the keyboard's controller, which updates it
  only every 20 seconds or so.
- `charging` proves the PicoCalc is on USB power, but the absence of
  `charging` proves nothing: once the battery is full, the charger stops.

`backlight`:

- `backlight` alone shows both backlights.
- `backlight 200` sets the screen.
- `backlight kbd 0` switches the keyboard's light off.
- The command shows the level the keyboard's controller reports after
  setting it. Some keyboard firmware rounds the level (the screen to steps
  of 16, from 16 to 240, the keyboard to steps of 32); others keep the
  value as given. At 0 the screen may go fully dark: type
  `backlight 128` to bring it back.

```
/>backlight
lcd 128
kbd 64
/>backlight 200
lcd 200
```

### Leaving the shell

| Command | Does |
|---|---|
| `poweroff` | switches the PicoCalc off |
| `reboot` | restarts the shell |
| `bootsel` | restarts the RP2350 in BOOTSEL mode, as a USB drive, to install firmware |
| `menu` | runs UF2 Loader's own menu (`/BOOT2350.ORG`) |
| `usbdisk` | offers the SD card to a computer over USB (below) |

Each of these saves the history first. None can be used with a
redirection or in a pipe.

- **poweroff**: the keyboard's controller switches the power off about
  6 seconds later, on USB power too. If it is still on after 10 seconds,
  the shell says so and carries on.
- **reboot**: comes back to the shell, not to the last program. It needs
  the card in and UF2 Loader in flash, and says so if either is missing.
- **bootsel**: the RP2350's own drive appears on the computer. Switch the
  PicoCalc off and on to come back.

### usbdisk

`usbdisk` makes the SD card a USB drive on a computer connected to the
PicoCalc's USB port. Copy files to and from it as to any drive.

To come back to the shell:

- **Eject** the drive on the computer. The shell comes back by itself.
- **Esc** comes back at once. If the computer was still writing, those
  files may be incomplete, so wait for copies to finish first. An iPad
  has no eject, so there Esc is the only way.

The shell reads the card afresh afterwards. If the current directory has
gone, it moves to `/`.

### Variables and scripts

| Command | Does |
|---|---|
| `set` | lists the variables |
| `export [NAME=value...]` | sets variables, or lists them. The same as `NAME=value` |
| `unset NAME...` | removes variables |
| `sh file` | runs a script |
| `history` | lists the lines typed before |

### For testing

| Command | Does |
|---|---|
| `displaytest` | the display's stress test, with timings. Esc stops it |

## 5. Running programs

Type a program's name to run it. The shell looks for `name.uf2` in each
directory of `$PATH`, which is `/pico2-apps` unless you change it:

```
/>pico-oric
pico-oric: writing 112 of 120 sectors
```

A name with a `/` in it is taken as a path: `/games/chess.uf2`.

### What happens

1. The shell checks that the file is a UF2 for the RP2350 and that it
   fits the flash set aside for programs.
2. It writes the program to flash, but only the parts that differ from
   what is there. Running the same program again takes about a second.
3. It restarts the PicoCalc into the program.

If the power goes while the program is being written, the PicoCalc comes
back to the shell next time.

A program that runs from RAM (a `no_flash` build) is loaded straight
from the card without writing flash.

### Coming back

- **Hold Up, F1 or F5 while switching on.** This works for every program.
- **The program's own exit command**, if it has one, such as an *Exit to
  shell* item in its menu.

### Limits

- **Programs take no arguments yet.** `pico-oric game.tap` is refused,
  and so is running a program with a redirection or in a pipe.
- **Programs that keep files in flash.** Running a program rewrites the
  program area of flash, so a program that keeps its own files there,
  after its code, loses them when another program runs. **Pico Logo**
  does this: keep its files somewhere else first.

## 6. vi

`vi file` edits a text file of up to 64 KB, such as a program's settings
file:

```
/>vi /atom/pico-atom.cfg
```

vi has three modes.

- **Normal mode** is where it starts. Keys are commands.
- **Insert mode** is for typing text. Esc goes back to normal mode.
- **Visual mode** is for selecting text. `v` selects characters, `V`
  selects whole lines.

The bottom row shows the mode, messages, and the cursor's line and column.

### The commands most used

| Keys | Does |
|---|---|
| arrows, `h` `j` `k` `l` | move |
| `w` `b` `e` | next word, previous word, end of word |
| `0` `$` | start and end of the line |
| `gg` `G` | first and last line; `12G` goes to line 12 |
| PgUp, PgDn (Shift+Up, Shift+Down) | a screenful up or down |
| `i` `a` | insert before or after the cursor |
| `I` `A` | insert at the start or end of the line |
| `o` `O` | open a new line below or above |
| `x` | delete a character |
| `dd` `yy` | cut or copy a line; `3dd` cuts three |
| `dw` `cw` | delete or change a word |
| `p` `P` | paste after or before |
| `u` `Ctrl-R` | undo and redo |
| `/text` `?text` | search forward or back; `n` finds the next |
| `:%s/old/new/g` | replace in every line |
| `>>` `<<` | indent or unindent a line by 4 spaces |
| `.` | repeat the last change |

### Saving and leaving

| Keys | Does |
|---|---|
| `:w` | save |
| `:wq` | save and leave |
| `ZZ` or `:x` | save if changed, and leave |
| `:q` | leave, if nothing has changed |
| `:q!` or `ZQ` | leave without saving |
| Break (Shift+Esc) | leave at once, **without saving** |

vi saves safely: it writes `name.new` first, then puts it in place, so a
failed save leaves the old file as it was.

### Good to know

- vi keeps a file's line endings, LF or CR LF, as they were, and whether
  the last line ends in a line break.
- `view`, `vi -R`, and files marked read-only open read-only.
- A file larger than 64 KB is refused, never cut short.
- Long lines do not wrap: the view scrolls sideways to follow the cursor.
- A tab shows as up to 8 spaces. A control character shows as `^X`.

## 7. The card

```
/BOOT2350.UF2         the shell
/BOOT2350.ORG         UF2 Loader's own menu, run with `menu`
/pico2-apps/          programs, run by name
/etc/profile          run each time the shell starts
/home/                where the shell starts ($HOME)
/.shell/              the shell's history, and large pipes while they run
```

- **Hidden names.** `ls` and globs leave out names that start with `.`
  (such as macOS's `._` files) and names FAT marks hidden or system, such
  as Windows's `System Volume Information`. `ls -a` shows them.
- **Safe writes.** Every command that writes a file (`cp`, `>`, `>>`,
  vi's `:w`) writes `name.new` and then renames it into place. If you
  ever find a `name.new` left on the card, the power went at just the
  wrong moment. It holds the complete new version.
- **Taking the card out** at the prompt is fine. The shell reads the new
  card at the next command.

## 8. Messages and limits

Errors name the command, then the file, then the reason:

```
/>cat nope.txt
cat: nope.txt: no such file or directory
/>pico-ace
pico-ace: not found
```

| Message | Means |
|---|---|
| `name: not found` | no builtin, and no `name.uf2` in `$PATH` |
| `not for this device` | the UF2 is for another chip, such as an RP2040 |
| `not on this machine` | the command needs the PicoCalc's hardware, as in the shell built for a computer |
| `not with a redirection or a pipe` | the command leaves the shell, so it must run on its own |
| `cannot run programs: ...` | the shell was not started by UF2 Loader, or the loader is a version it does not know |

Limits:

| What | Limit |
|---|---|
| A typed line | 255 characters |
| Words on a line, after globs | 128 |
| Variables | 1 KB in all |
| History | about 2 KB of lines |
| A file in vi | 64 KB |
| Scripts within scripts | 3 deep |
| A path | 255 characters |

## 9. The DOS flavour

The shell also comes in a second flavour, for anyone who knows DOS
better than UNIX. It manages the same card and runs the same programs,
but its commands are DOS's: `DIR`, `COPY`, `DEL`, `CD`. It has its own
`BOOT2350.UF2`. To use it, copy that file to the card's root in place of
the UNIX one; to go back, copy the UNIX one there again. Neither flavour
installs the other. In place of vi it has EDIT, an editor like the
one DOS had (below).

### The prompt and paths

The card is drive `C:`, and the prompt is the drive and directory
followed by `>`:

```
C:\HOME>DIR /B
TODO.TXT
NOTES
C:\HOME>CD ..
C:\>
```

- Paths use `\`: `C:\ORIC\KEYMAPS`. `/` works as well, except at the
  start of a word for a command that takes switches, where `/W` is a
  switch, as in DOS.
- Commands and switches can be typed in either case: `dir /w` is `DIR /W`.
- `CD..`, `CD\` and `DIR/W` work with no space, as in DOS.
- Put a name with spaces in quotes: `TYPE "my notes.txt"`.
- The shell starts in `C:\HOME` if the card has one, and otherwise in
  `C:\`.

### Wildcards

`*` stands for any characters and `?` for one, before or after the
name's last dot: `*.TXT` is every name ending `.TXT`, `*.*` is every
name, `*.` is every name with no extension, and `REPORT?.TXT` matches
`REPORT.TXT` and `REPORT1.TXT`. Wildcards leave out hidden and system
files; give the name in full to reach one. `DIR NAME` lists `NAME.*`.

### Commands

| Command | Does |
|---|---|
| `DIR [path] [/W] [/P] [/S] [/A] [/B]` | lists files: `/W` wide, `/P` a screen at a time, `/S` in every directory below, `/A` hidden files too, `/B` names only |
| `CD [path]`, `CHDIR` | changes directory; with none, shows the current one |
| `MD path`, `MKDIR` | makes a directory |
| `RD path`, `RMDIR` | removes an empty directory |
| `COPY source [destination]` | copies files: `COPY A.TXT B.TXT`, `COPY *.TXT \BACKUP`, `COPY *.TXT *.BAK` |
| `XCOPY source [destination] [/S]` | copies files and, with `/S`, every directory below |
| `DEL path`, `ERASE` | deletes files. `DEL *.*` asks first; `/P` asks for each. **There is no undo** |
| `DELTREE [/Y] path...` | deletes a directory and everything in it, after asking (not with `/Y`) |
| `REN path name`, `RENAME` | renames files: `REN *.TXT *.OLD` |
| `MOVE source... destination` | moves files into a directory, or renames a file or directory |
| `TYPE file` | shows a text file |
| `MORE < file`, `command \| MORE` | shows text a screen at a time |
| `FIND [/V] [/C] [/N] [/I] "text" [file...]` | lines that contain the text: `/V` those that do not, `/C` how many, `/N` with numbers, `/I` in either case |
| `ATTRIB [+R\|-R] [+A\|-A] [+S\|-S] [+H\|-H] [path]` | shows or changes a file's attributes: read-only, archive, system, hidden |
| `SET [name=[value]]` | lists the variables, sets one, or with nothing after `=`, clears it. Use one as `%NAME%` |
| `PATH [dir;dir...]` | shows or sets where programs are found; `PATH ;` clears it |
| `PROMPT [text]` | sets the prompt: `$P` the path, `$G` `>`, `$_` a new line, `$N` the drive. `PROMPT` alone puts back `$P$G` |
| `ECHO [ON\|OFF\|message]` | prints a message; `ECHO.` prints a blank line; `ECHO OFF` hides the prompt |
| `PAUSE` | waits for a key |
| `EDIT [file]` | edits a text file (below) |
| `CALL`, `FOR`, `GOTO`, `IF`, `REM`, `SHIFT` | for batch files (below); `FOR` and `IF` work at the prompt too |
| `CLS` | clears the screen |
| `VER`, `VOL`, `MEM` | the shell's version; the card's label and serial number; the memory in use and free |
| `HELP [command]` | lists the commands, or explains one |
| `EXIT` | UF2 Loader's own menu, as `menu` in the UNIX flavour |

The PicoCalc's own commands keep their names in this flavour: `BATTERY`,
`BACKLIGHT`, `PHOSPHOR`, `FONT`, `INFO`, `USBDISK`, `POWEROFF`, `REBOOT`
and `BOOTSEL` (section 4). There is no `DATE` or `TIME`: the PicoCalc has
no clock.

Output can go to a file with `>` or `>>`, or be thrown away with `> NUL`.
A command can read a file with `<`, and `|` joins commands:
`DIR /B | FIND "TXT"`.

### Programs

Type a program's name to run it, as in the UNIX flavour: `PICO-ATOM`
runs `PICO-ATOM.UF2`. The shell looks in the current directory first,
then in each directory of `PATH`, which starts as `C:\PICO2-APPS`. A
batch file of the same name, `PICO-ATOM.BAT`, anywhere it looks, runs
in its place.

### Batch files

A batch file is a text file of commands, one to a line, whose name ends
`.BAT`. Type its name, with or without `.BAT`, to run it; the shell
looks for it where it looks for programs.

```
@ECHO OFF
REM BACKUP.BAT: copy the text files given, or say how
IF "%1"=="" GOTO USAGE
:NEXT
IF "%1"=="" GOTO END
COPY %1 \BACKUP
SHIFT
GOTO NEXT
:USAGE
ECHO Usage: BACKUP file...
:END
```

| In a batch file | Does |
|---|---|
| `%0` to `%9` | the name it was run by, and the words after it. Words are split at blanks, commas and semicolons; quotes are kept |
| `SHIFT` | moves the words down one: `%2` becomes `%1`, and a word past `%9` comes in |
| `:LABEL` and `GOTO LABEL` | carries on from the line after the label. `Label not found` ends the batch file |
| `IF [NOT] EXIST name command` | runs the command if the file or directory is there; with a wildcard, if any file matches |
| `IF [NOT] ERRORLEVEL n command` | runs it if the last command's status is `n` or more. Every command sets it: 0 if it worked, 1 or more if not |
| `IF [NOT] a==b command` | runs it if the two are the same, in the same case. Put both in quotes, `"%1"==""`, in case one is empty |
| `FOR %%X IN (set) DO command` | runs the command once for each word of the set, with `%%X` replaced by it; a wildcard in the set is each file it matches. At the prompt, write `%X` |
| `CALL name [words]` | runs another batch file and comes back. Without `CALL`, the other one takes this one's place, and this one does not go on |
| `REM text` | a comment |
| `ECHO OFF`, `@` | `ECHO OFF` stops the shell showing each line before running it; `@` at a line's start hides that line. At the end of the batch file, ECHO is as it was at the prompt |
| `PAUSE` | waits for a key |

Break, Esc or Ctrl-C stops a batch file, even one that loops for ever.
`%NAME%` is a variable, as at the prompt. Lines can be up to 255
characters long, and batch files can `CALL` one another three deep. A
batch file's output cannot be redirected or piped as `NAME > FILE`, but
`CALL NAME > FILE` works.

`C:\AUTOEXEC.BAT`, if the card has one, runs each time the shell starts,
before the first prompt: it is the place for `PATH`, `PROMPT` and `SET`.
A program run from it would run at every start, so keep that to a
program you can leave; if the shell can no longer be reached, take the
card out and edit the file on a computer.

### EDIT

`EDIT NAME.TXT` opens a file to edit, or a new one by that name; `EDIT`
alone starts an untitled one. EDIT switches to the 64-column font while
it runs and back when it ends. The top row is the menu bar, the next the
file's name, and the bottom row says `F1=Help` and where the cursor is.

**Menus.** Tap Alt on its own to open the File menu, or hold Alt and
press a menu's letter: Alt+F, Alt+E, Alt+S, Alt+O, Alt+H. In a menu,
Left and Right go to the next menu, Up and Down choose, and Enter or an
item's highlighted letter carries it out. Esc closes the menu.

| Menu | Items |
|---|---|
| File | New, Open, Save, Save As, Exit |
| Edit | Cut, Copy, Paste, Clear |
| Search | Find, Repeat Last Find, Change (every match, at once) |
| Options | Display: white on blue, or your phosphor on black |
| Help | Keys: this table, on the screen |

**Keys.** The PicoCalc's keyboard cannot send Shift with the arrows, so
Alt does what Shift did in DOS's EDIT:

| Key | Does |
|---|---|
| Arrows | move |
| Ctrl+Left, Ctrl+Right | a word left or right |
| Shift+Tab, Shift+Del | Home and End: the start and end of the line |
| Ctrl+Shift+Tab, Ctrl+Shift+Del | the start and end of the file |
| Shift+Up, Shift+Down | a page up or down |
| Alt with the arrows | select |
| Ctrl+X, Ctrl+C, Ctrl+V | cut, copy, paste |
| Del | clears the selection, or the letter under the cursor |
| Ctrl+Y | cuts the line |
| Alt+I | Insert: switches between inserting and typing over (`OVR`) |
| F1 | the keys |
| F3 | finds the same text again |
| Esc | closes a menu or a box |
| Brk (Shift+Esc) | File, Exit |

Typing over a selection replaces it. The clipboard holds 8 KB. Find
and Change match the text as typed, in either case. A box that asks for
text offers what was typed last: type to replace it, or Backspace to
change it.

**Saving.** Save writes the file through `NAME.TXT.new`, as vi does, so a
cut in the power leaves the old file or the new one. Exit and New ask
before they lose a change. EDIT keeps the file's line endings, `CR LF`
or `LF`; a new file gets `LF`. A file larger than 64 KB is not opened.
A read-only file opens, but must be saved under another name.

### Keys at the prompt

The keys of section 2 work here too, and as in DOS:

| Key | Does |
|---|---|
| F3 | types the rest of the last line from where the cursor is |
| Esc | clears the line |
| Tab | completes a command or a path, with `\` after a directory |

### Messages

Errors are DOS's: `Bad command or file name`, `File not found - X.TXT`,
`Access denied`, `Invalid directory`, `Required parameter missing`,
`Invalid switch - /X`.
