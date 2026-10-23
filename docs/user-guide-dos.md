# PicoCalc shell, DOS flavour — user guide

The shell is a small command line for the ClockworkPi PicoCalc with an
RP2350 board. This is its DOS flavour, for anyone who knows DOS better
than UNIX: the card is drive `C:`, the commands are DOS's (`DIR`, `COPY`,
`DEL`, `CD`), batch files run as they did, and EDIT edits text files.
From it you manage the files on the SD card, edit text files, and run
programs by name.

The shell also comes in a UNIX flavour, with `ls`, `cp`, `rm` and vi; it
has [its own guide](user-guide-unix.md). Each flavour is its own
`BOOT2350.UF2`. To install one, see the [README](../README.md).

## Contents

1. [Getting started](#1-getting-started)
2. [Keys](#2-keys)
3. [The command line](#3-the-command-line)
4. [Commands](#4-commands)
5. [Running programs](#5-running-programs)
6. [Batch files](#6-batch-files)
7. [EDIT](#7-edit)
8. [The card](#8-the-card)
9. [Messages and limits](#9-messages-and-limits)

## 1. Getting started

Hold **Up** (or F1 or F5) while switching the PicoCalc on, and the shell
starts. Powering on with no key held starts the last program you ran,
as the loader always has.

The card is drive `C:`, and the prompt is the drive and directory
followed by `>`:

```
C:\HOME>DIR
 Volume in drive C has no label
 Volume Serial Number is 5C23-0000
 Directory of C:\HOME

NOTES          <DIR>
TODO     TXT                          6
    1 file(s)              6 bytes
                  66,040,832 bytes free
C:\HOME>CD ..
C:\>
```

The shell starts in `C:\HOME` if the card has one, and otherwise in
`C:\`. The screen shows 40 columns in IBM's PC font, as DOS did.

`HELP` lists the commands, and `HELP name` explains one:

```
C:\>HELP COPY
usage: COPY source [destination]
Copy one or more files
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
| F3 | types the rest of the last line from where the cursor is |
| Tab | completes a command or a path, with `\` after a directory |
| Esc | clears the line |
| Enter | runs the line |

The shell keeps about 2 KB of the lines you have typed. It saves them to
the card (`C:\.shell\history`) when you leave the shell: for a program,
`EXIT`, `USBDISK`, `REBOOT`, `POWEROFF` or `BOOTSEL`. Lines typed since
the last save are lost if the power is switched off.

### While a command runs

**Esc**, **Break** (Shift+Esc) or **Ctrl-C** stops a long command, such
as a large `COPY`, and drops the rest of the line. It stops a batch file
too, even one that loops for ever.

### The PicoCalc's own keys

Alt+`,`, Alt+`.`, Alt+Space and Alt+B belong to the keyboard itself: it
uses them for the backlights and the battery, and the shell never sees
them. The `BACKLIGHT` and `BATTERY` commands (section 4) do the same
from the command line.

## 3. The command line

### Paths

- Paths use `\`: `C:\ORIC\KEYMAPS`. `/` works as well, except at the
  start of a word for a command that takes switches, where `/W` is a
  switch, as in DOS.
- A path may start with `C:` or not: `C:\HOME` and `\HOME` are the same.
- Commands and switches can be typed in either case: `dir /w` is `DIR /W`.
- `CD..`, `CD\` and `DIR/W` work with no space, as in DOS.
- Put a name with spaces in quotes: `TYPE "my notes.txt"`.
- Names on the card are case-insensitive, and are shown as the card
  spells them.
- A file written or made gets the date and time, in local time, as a PC
  does. With the clock not set (see `DATE`), every file gets the same
  fixed date, 1 January 2026.

### Wildcards

`*` stands for any characters and `?` for one, before or after the
name's last dot:

- `*.TXT` is every name ending `.TXT`;
- `*.*` is every name;
- `*.` is every name with no extension;
- `REPORT?.TXT` matches `REPORT.TXT` and `REPORT1.TXT`.

Wildcards leave out hidden and system files; give the name in full to
reach one. `DIR NAME` lists `NAME.*`.

### Redirection and pipes

| Write | Does |
|---|---|
| `command > file` | the output into `file`, replacing it |
| `command >> file` | the output added to the end of `file` |
| `command > NUL` | the output thrown away |
| `command < file` | `file` as the command's input |
| `a \| b` | `a`, with its output given to `b` as input: `DIR /B \| FIND "TXT"` |

A redirected file is written safely: if the command fails or is stopped,
the old file is left as it was. Error messages always go to the screen,
never into the file.

A pipe runs `a` to the end, then gives its output to `b`. Up to 16 KB is
held in memory; larger output goes through a temporary file in
`C:\.shell`, which is removed afterwards.

### Variables

```
C:\>SET NAME=world
C:\>ECHO hello %NAME%
hello world
```

- `SET` alone lists the variables. `SET NAME=` clears one.
- `%NAME%` is a variable's value. An unset variable is empty.
- Variables last until the shell is left. There is room for 1 KB of them.
- `PATH` and `PROMPT` are variables too, with commands of their own
  (section 4).
- `TZ` is the time zone, as a POSIX string: `SET TZ=EST5EDT`. Without
  it, the time is UTC. See `DATE` and `TIME` in section 4.

### ERRORLEVEL

Every command sets the error level: 0 if it worked, 1 or more if not. A
batch file tests it with `IF ERRORLEVEL n` (section 6).

## 4. Commands

Every command is built in, so it runs at once. Any other name runs a
batch file or a program (section 5).

### Directories

| Command | Does |
|---|---|
| `DIR [path] [/W] [/P] [/S] [/A] [/B]` | lists files with their size, date and time: `/W` wide, names only, `/P` a screen at a time, `/S` in every directory below, `/A` hidden files too, `/B` names only |
| `CD [path]`, `CHDIR` | changes directory; with none, shows the current one |
| `MD path`, `MKDIR` | makes a directory |
| `RD path`, `RMDIR` | removes an empty directory |
| `DELTREE [/Y] path...` | deletes a directory and everything in it, after asking (not with `/Y`). **There is no undo** |

```
C:\HOME>DIR
 ...
NOTES        <DIR>      10-22-26  2:30p
README   TXT      1,024 10-20-26  9:12a
my long file name.txt
                     24 10-22-26  2:31p
```

A name that is not 8.3, or a file of 1 GB or more, puts its size, date
and time on the next row, so every row fits the 40 columns.

### Files

| Command | Does |
|---|---|
| `COPY source [destination]` | copies files: `COPY A.TXT B.TXT`, `COPY *.TXT \BACKUP`, `COPY *.TXT *.BAK` |
| `XCOPY source [destination] [/S]` | copies files and, with `/S`, every directory below |

`COPY` and `XCOPY` give each copy the date and time of the file it was
copied from, as DOS did.
| `DEL path`, `ERASE` | deletes files. `DEL *.*` asks first; `/P` asks for each. **There is no undo** |
| `REN path name`, `RENAME` | renames files: `REN *.TXT *.OLD` |
| `MOVE source... destination` | moves files into a directory, or renames a file or directory |
| `ATTRIB [+R\|-R] [+A\|-A] [+S\|-S] [+H\|-H] [path]` | shows or changes a file's attributes: read-only, archive, system, hidden |
| `VOL` | the card's label and serial number |

### Reading text

| Command | Does |
|---|---|
| `TYPE file` | shows a text file |
| `MORE < file`, `command \| MORE` | shows text a screen at a time |
| `FIND [/V] [/C] [/N] [/I] "text" [file...]` | lines that contain the text: `/V` those that do not, `/C` how many, `/N` with numbers, `/I` in either case |
| `EDIT [file]` | edits a text file (section 7) |

### The command line

| Command | Does |
|---|---|
| `SET [name=[value]]` | lists the variables, sets one, or with nothing after `=`, clears it |
| `PATH [dir;dir...]` | shows or sets where programs are found; `PATH ;` clears it |
| `PROMPT [text]` | sets the prompt: `$P` the path, `$G` `>`, `$_` a new line, `$N` the drive, `$D` the date, `$T` the time. `PROMPT` alone puts back `$P$G` |
| `ECHO [ON\|OFF\|message]` | prints a message; `ECHO.` prints a blank line; `ECHO OFF` hides the prompt |
| `PAUSE` | waits for a key |
| `CHOICE [/C:keys] [/N] [/S] [/T:c,nn] [text]` | waits for one of the keys; ERRORLEVEL is which (section 6) |
| `CALL`, `FOR`, `GOTO`, `IF`, `REM`, `SHIFT` | for batch files (section 6); `FOR` and `IF` work at the prompt too |
| `CLS` | clears the screen |
| `HELP [command]` | lists the commands, or explains one |

### The PicoCalc

| Command | Does |
|---|---|
| `PHOSPHOR [white\|green\|amber\|default]` | the colour of the text, like an old monitor's phosphor. `PHOSPHOR` alone shows the current one |
| `BATTERY` | the battery's charge, and `charging` when the charger is on |
| `BACKLIGHT [lcd\|kbd] [0-255]` | shows or sets the screen (`lcd`) or keyboard (`kbd`) backlight |
| `VER` | the shell's version |
| `MEM` | the memory in use and free, and the PSRAM: 8 MB on the Pimoroni Pico Plus 2 W, 0 on a board without it |
| `INFO` | the shell's version, the chip, memory and PSRAM (0 MB on a board without it), UF2 Loader, the clock, the keyboard's firmware, the card and the battery |
| `DATE [/T \| date]` | shows the date and asks for a new one; Enter keeps it. `DATE 10-22-26` sets it, `DATE /T` only shows it |
| `TIME [/T \| time]` | the same for the time: `TIME 14:30`, `TIME 2:30p`, `TIME 9:05:30` |

`PHOSPHOR` lasts until the shell is left. Put it in `AUTOEXEC.BAT` to
have it every time.

`DATE` and `TIME`:

- The clock keeps going while a program runs, and through `REBOOT`.
  Switching the PicoCalc off loses it, unless a DS3231 clock chip is
  fitted, on GP4 (SDA) and GP5 (SCL): setting the time sets the chip
  too, and the shell reads it back at power-on. If the chip lost its
  time (it is new, or its battery is flat), the shell says
  `Warning: the DS3231 lost its time` at power-on, but not again after a
  program, until the clock is set. The chip holds 2000 to 2199.
- The date is typed in the country's order, `mm-dd-yy` in the United
  States (see `CONFIG.SYS`, section 6), with `-`, `/` or `.` between. The
  year is 1980 to 2099, in two digits or four.
- The time is `hh:mm`, with seconds and hundredths if you like, and `a`
  or `p` for a 12-hour clock.
- With the clock not set, they say `Current date is not set`, and
  `DATE /T` and `TIME /T` set ERRORLEVEL 1, so `AUTOEXEC.BAT` can ask
  for the date only when it is needed (section 6).
- The time is local time, by the variable `TZ`: `SET TZ=EST5EDT` in
  `AUTOEXEC.BAT`. `TZ` names the standard zone and how many hours it is
  behind UTC, then the summer zone, and optionally when summer time
  starts and ends (`EST5EDT,M3.2.0,M11.1.0`: the second Sunday of March
  and the first of November; these are the dates used when none are
  given). Others: `CST6CDT`, `PST8PDT`, `GMT0BST,M3.5.0/1,M10.5.0`,
  `CET-1CEST,M3.5.0,M10.5.0/3`. Without `TZ`, the time is UTC.

```
C:\>DATE
Current date is Thu 10-22-2026
Enter new date (mm-dd-yy):
C:\>TIME /T
Current time is  2:30:05.00p
```

`BATTERY`:

- The charge is read from the keyboard's controller, which updates it
  only every 20 seconds or so.
- `charging` proves the PicoCalc is on USB power, but the absence of
  `charging` proves nothing: once the battery is full, the charger stops.

`BACKLIGHT`:

- `BACKLIGHT` alone shows both backlights.
- `BACKLIGHT 200` sets the screen.
- `BACKLIGHT kbd 0` switches the keyboard's light off.
- The command shows the level the keyboard's controller reports after
  setting it. Some keyboard firmware rounds the level (the screen to steps
  of 16, from 16 to 240, the keyboard to steps of 32); others keep the
  value as given. At 0 the screen may go fully dark: type
  `BACKLIGHT 128` to bring it back.

### Leaving the shell

| Command | Does |
|---|---|
| `POWEROFF` | switches the PicoCalc off |
| `REBOOT` | restarts the shell |
| `BOOTSEL` | restarts the RP2350 in BOOTSEL mode, as a USB drive, to install firmware |
| `EXIT` | runs UF2 Loader's own menu (`C:\BOOT2350.ORG`) |
| `USBDISK` | offers the SD card to a computer over USB (below) |

Each of these saves the history first. None can be used with a
redirection or in a pipe.

- **POWEROFF**: the keyboard's controller switches the power off about
  6 seconds later, on USB power too. If it is still on after 10 seconds,
  the shell says so and carries on.
- **REBOOT**: comes back to the shell, not to the last program. It needs
  the card in and UF2 Loader in flash, and says so if either is missing.
- **BOOTSEL**: the RP2350's own drive appears on the computer. Switch the
  PicoCalc off and on to come back.

### USBDISK

`USBDISK` makes the SD card a USB drive on a computer connected to the
PicoCalc's USB port. Copy files to and from it as to any drive.

To come back to the shell:

- **Eject** the drive on the computer. The shell comes back by itself.
- **Esc** comes back at once. If the computer was still writing, those
  files may be incomplete, so wait for copies to finish first. An iPad
  has no eject, so there Esc is the only way.

The shell reads the card afresh afterwards. If the current directory has
gone, it moves to `C:\`.

## 5. Running programs

Type a program's name to run it: `PICO-ATOM` runs `PICO-ATOM.UF2`. The
shell looks in the current directory first, then in each directory of
`PATH`, which starts as `C:\PICO2-APPS`. A batch file of the same name,
`PICO-ATOM.BAT`, anywhere it looks, runs in its place.

```
C:\>PICO-ORIC
writing 112 of 112 sectors
```

The count goes up as each sector is written. Only the sectors that
differ from what flash holds are written, so a program run before
writes few or none.

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

- **Programs take no arguments yet.** `PICO-ORIC GAME.TAP` is refused,
  and so is running a program with a redirection or in a pipe.
- **Programs that keep files in flash.** Running a program rewrites the
  program area of flash, so a program that keeps its own files there,
  after its code, loses them when another program runs. **Pico Logo**
  does this: keep its files somewhere else first.

## 6. Batch files

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
| `CHOICE` | waits for one of a set of keys, and sets ERRORLEVEL to which (below) |

`%NAME%` is a variable, as at the prompt. Lines can be up to 255
characters long, and batch files can `CALL` one another three deep. A
batch file's output cannot be redirected or piped as `NAME > FILE`, but
`CALL NAME > FILE` works.

### Menus

`CHOICE` waits for one of a set of keys, and sets ERRORLEVEL to the
key's place in the set: 1 for the first, 2 for the second, and so on.
With `IF ERRORLEVEL` and `GOTO`, that makes a menu:

```
@ECHO OFF
:MENU
ECHO 1  Oric
ECHO 2  Atom
ECHO Q  Quit
CHOICE /C:12Q /T:Q,10 Which
IF ERRORLEVEL 3 GOTO END
IF ERRORLEVEL 2 PICO-ATOM
IF ERRORLEVEL 1 PICO-ORIC
:END
```

This shows `Which[1,2,Q]?` and waits. `IF ERRORLEVEL n` is true for `n`
or more, so test the highest first.

| Switch | Does |
|---|---|
| `/C:keys` | the keys to choose from; `YN` if there is no `/C` |
| `/N` | does not show the keys after the text |
| `/S` | `a` and `A` are different keys; otherwise case does not matter |
| `/T:c,nn` | chooses `c` if nothing is pressed for `nn` seconds (0 to 99) |

Put the text last, after the switches. A key that is not in the set
beeps. Break, Esc or Ctrl-C stops the batch file. A mistake in the
switches sets ERRORLEVEL to 255.

### AUTOEXEC.BAT

`C:\AUTOEXEC.BAT`, if the card has one, runs each time the shell starts,
before the first prompt. It is the place for your own settings:

```
@ECHO OFF
PATH C:\PICO2-APPS;C:\GAMES
PROMPT $P$G
PHOSPHOR amber
CD \HOME
```

A program run from it would run at every start, so keep that to a
program you can leave. If the shell can no longer be reached, take the
card out and edit the file on a computer.

To ask for the date and time at power-on, only when the clock is not
set:

```
@ECHO OFF
SET TZ=EST5EDT
DATE /T
IF NOT ERRORLEVEL 1 GOTO SET
DATE
TIME
:SET
```

### CONFIG.SYS

`C:\CONFIG.SYS`, if the card has one, is read before `AUTOEXEC.BAT`. Its
one working line is `COUNTRY=`, which sets how dates, times and numbers
are written, as in DOS:

```
COUNTRY=044
```

| Code | Country | Date | Time | Numbers |
|---|---|---|---|---|
| 001 | United States (the default) | `10-22-2026` | ` 2:30:05.00p` | `1,234` |
| 002 | Canadian-French | `2026-10-22` | `14:30:05,00` | `1 234` |
| 033 | France | `22.10.2026` | `14:30:05,00` | `1 234` |
| 044 | United Kingdom | `22/10/2026` | `14:30:05.00` | `1,234` |
| 049 | Germany | `22.10.2026` | `14:30:05,00` | `1.234` |
| 081 | Japan | `2026-10-22` | `14:30:05.00` | `1,234` |

`COUNTRY=044,850,C:\DOS\COUNTRY.SYS`, as DOS wrote it, works: only the
code is read. It affects `DATE`, `TIME`, `PROMPT`'s `$D` and `$T`, and
`DIR`. `FILES=`, `BUFFERS=`, `DEVICE=`, `DEVICEHIGH=`, `DOS=`,
`LASTDRIVE=`, `BREAK=`, `SHELL=`, `INSTALL=`, `FCBS=` and `STACKS=` mean
nothing here and are passed over, so a `CONFIG.SYS` from an old PC works.
`REM` and `;` start a comment. Any other line gets `Unrecognized command
in CONFIG.SYS` and its line number.

## 7. EDIT

`EDIT NAME.TXT` opens a file to edit, or a new one by that name; `EDIT`
alone starts an untitled one. EDIT shows 40 columns, like the rest of
the shell. A line wider than the screen scrolls sideways as the cursor
moves along it. The top row is the menu bar, the next the file's name,
and the bottom row says `F1=Help` and where the cursor is.

### Menus

Tap Alt on its own to open the File menu, or hold Alt and press a menu's
letter: Alt+F, Alt+E, Alt+S, Alt+O, Alt+H. In a menu, Left and Right go
to the next menu, Up and Down choose, and Enter or an item's highlighted
letter carries it out, with or without Alt, so Alt+F then Alt+X exits.
Alt with a letter no item has opens that menu instead. Esc closes the
menu.

| Menu | Items |
|---|---|
| File | New, Open, Save, Save As, Exit |
| Edit | Cut, Copy, Paste, Clear |
| Search | Find, Repeat Last Find, Change (every match, at once) |
| Options | Display: white on blue, or your phosphor on black |
| Help | Keys: this table, on the screen |

### Keys

The PicoCalc's keyboard cannot send Shift with the arrows, so Alt does
what Shift did in DOS's EDIT:

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

Answer a question by pressing a button's red letter (`Y`, `N`, `C`), or
Enter for the first button; Esc cancels. Typing over a selection
replaces it. The clipboard holds 8 KB. Find and Change match the text as
typed, in either case. A box that asks for text offers what was typed
last: type to replace it, or Backspace to change it.

### Saving

Save writes the file through `NAME.TXT.new`, so a cut in the power
leaves the old file or the new one. Exit and New ask before they lose a
change. EDIT keeps the file's line endings, `CR LF` or `LF`; a new file
gets `LF`. A file larger than 64 KB is not opened. A read-only file
opens, but must be saved under another name.

## 8. The card

```
C:\BOOT2350.UF2       the shell
C:\BOOT2350.ORG       UF2 Loader's own menu, run with EXIT
C:\PICO2-APPS\        programs, run by name
C:\AUTOEXEC.BAT       run each time the shell starts
C:\HOME\              where the shell starts
C:\.shell\            the shell's history, and large pipes while they run
```

- **Hidden files.** `DIR` and wildcards leave out names that start with
  `.` (such as macOS's `._` files) and files marked hidden or system,
  such as Windows's `System Volume Information`. `DIR /A` shows them, and
  `ATTRIB` changes the marks.
- **Safe writes.** Every command that writes a file (`COPY`, `>`, `>>`,
  EDIT's Save) writes `NAME.new` and then renames it into place. If you
  ever find a `NAME.new` left on the card, the power went at just the
  wrong moment. It holds the complete new version.
- **Taking the card out** at the prompt is fine. The shell reads the new
  card at the next command.

## 9. Messages and limits

Errors are DOS's:

```
C:\>TYPE NONE.TXT
File not found - NONE.TXT
C:\>NOPE
Bad command or file name
```

| Message | Means |
|---|---|
| `Bad command or file name` | no command, batch file or program by that name |
| `File not found` | no file matches the name or wildcard |
| `Access denied` | the file is read-only, or a file of that name is already there |
| `Invalid directory`, `Invalid path` | no directory by that name |
| `Required parameter missing` | the command needs a name |
| `Invalid switch - /X` | the command has no `/X` |
| `not for this device` | the UF2 is for another chip, such as an RP2040 |
| `not with a redirection or a pipe` | the command leaves the shell, so it must run on its own |
| `cannot run programs: ...` | the shell was not started by UF2 Loader, or the loader is a version it does not know |

Limits:

| What | Limit |
|---|---|
| A typed line | 255 characters |
| Variables | 1 KB in all |
| History | about 2 KB of lines |
| A file in EDIT | 64 KB |
| The clipboard in EDIT | 8 KB |
| Batch files within batch files | 3 deep |
| A path | 255 characters |
