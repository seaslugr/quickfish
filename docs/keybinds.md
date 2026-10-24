# Quickfish Keybinds and Commands

Everything in quickfish is a command. Keybinds are shortcuts for command lines, and you can type the same commands in the shell pane or in quickshell (`:`). Type `help` in the shell for the full list with aliases, or press `F1` for the keybind overlay.

The keybind table lives in `src/keymap.c` and the command table in `src/command.c`.

## Navigation
- `j` / Down, `k` / Up: `down`, `up` (a count works: `5j`)
- `Ctrl+D` / `Ctrl+U`: `page-down`, `page-up`
- `h` / Left: `parent`
- `l` / Right / Enter: `open`, enters a directory or opens a file
- `g`: `goto`, prompts for a path or name. `<num>g` jumps the cursor to that index
- `..`: `back`, the previous directory

## Files
- `r`: `rename`
- `s`: `select cur`, toggles selection
- `v`: `visual`, sweep select (`Esc` leaves it)
- `d`: `trash`, asks first
- `D`: `delete`, permanent, always asks first
- `m`: `mark cur`, toggles a move mark
- `M`: `mark sel|all`, marks the selection, or everything when nothing is selected
- `p`: `paste`, moves marked items here
- `P`: `paste cur`, moves marked items into the directory under the cursor
- `Ctrl+C`: `clear-marks`
- `u` / `U`: `undo`, `redo`. A multi-file trash, delete or paste undoes in one step
- `:`: `quickshell`, the inline command prompt

## Panes
- `Ctrl+P`: `preview`, toggles the preview pane
- `p` in the preview pane: `preview off`
- `j` / `k` / `Ctrl+D` / `Ctrl+U` in the preview pane: `scroll`
- `K` / Shift+Up / `Esc` in the shell: `focus files`
- `J` / Shift+Down: `focus shell`
- `L` / Shift+Right: `focus next`
- `H` / Shift+Left: `focus prev`
- `F1`: `help`
- `Ctrl+L`: `clear`, clears the shell output
- `Ctrl+Z`: `suspend` (resume with `fg`)

While the shell pane is focused, letters type normally. Use the Shift+arrow keys or `Esc` to switch panes.

## Targets
File commands (`trash`, `delete`, `mark`, `select`, `stat`, `open`, `rename`, `paste`) take targets:

- an index: `trash 3 7`
- a name: `trash notes.txt`, quoted when it has spaces: `trash 'my file.txt'`. Quoted words are always names, so `'3'` is a file called 3
- `sel` (the selection), `cur` (the cursor), `all`
- `a|b`: the first one that isn't empty, so `sel|cur` means the selection, or the cursor when nothing is selected

With no targets, a command acts on `sel|cur`, the same as its keybind.

## Shell
- A bare index opens that entry: `3`
- `cd <path|index>`, `cd -`, `cd ~user`
- `r <target> <new name>` renames, `r <target>` opens the rename prompt
- `rm <targets>` is `delete`, `s <targets>` is `stat`, `q` is `quit`
- Anything that isn't a quickfish command runs in `$SHELL`, with `sel` and `cur` expanded to quoted names: `tar czf out.tgz sel`
- `!` runs the rest in `$SHELL` even when a quickfish command has that name: `!stat -c %s file`, `!rm -rf build`
- An empty line opens `$SHELL` in the current directory

## Debug
- `$jumplist`, `$move`, `$undo`, `$redo`: print internal state
