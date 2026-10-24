# Todo list (and ideas) Also has some feature ideas and stuff, yada yada.

## TODO
- clipboard support (+ register)
- put expensive tasks on a differant thread
- add the Operation Buffer
- add proper comments and documentation to all files
- hold down "s" to "drag select"
- copy files (only move exists right now)
- toggle for showing hidden files
- write `.trashinfo` files when trashing, so desktop trash apps can see and restore what quickfish trashed (FreeDesktop trash spec)
- scrollback for the shell buffer log (it only shows the newest lines)
- CI that runs `make test` and `make rel`

## Bugs
- opening files: `Exec=` is cut at the first space (breaks `flatpak run ...`, `env ...`), system `.desktop` dirs are searched before `~/.local`, `XDG_DATA_DIRS` is ignored, and `$EDITOR` is almost never used
- opening a file with xdg-open can leave a zombie process (the intermediate child is reaped with `WNOHANG`)
- interactive shell buffer commands (`less`, `vim`, `sudo` password prompts) hang the UI, since output is piped and the terminal is still in curses mode
- Ctrl+C can't interrupt a long-running shell buffer command (we're in raw mode)
- bat previews and `s` on a big directory block the UI (see the threading TODO)
- a failed undo still moves the op to the redo stack, so it's gone from undo
- visual mode with Ctrl+D/U only selects the entry it lands on, not the ones it skipped over
- `s` (stat) shows disk usage for directories but byte size for files
- rename, goto and shell input are ASCII only, and backspace deletes one byte, which breaks names with accents
- file preview lines are cut by bytes, not columns
- Full UTF-8 support

## ideas
- add image preview via some in terminal image renderer (find it out later)
- Bulk rename
- Jumplist/bookmarks
- Git integration
- Exiting the program puts you in the file directory the last where in (might be redundant since we have the Shell Buffer)
- Config file
  - Folders/files that can never be deleted
  - Custom keybinds
  - Workspaces
  - Buffer Layout
- Theme support (like swordfishs)
  - Config file support. Set the theme automaticly in the config file
- Plugin support
- Argument support
  - Default args in the config file?
- Persistent workspaces (eg "--workspace dev" would open `<dir>/build`)
  - Default workspace/quick loaded workspaces in config file?
- A second file buffer to allow multipanel managing
  - Do we set this by default, or in config file?
- Action Pipelines. Build pipelines like "compress > move > rename"
- Built in file conversion TUI. Would use ffmpeg as a backend (optinaly)
- Binary inspector via a hex view
- Context-aware shell, part two. `sel`, `cur` and `all` work as targets and expand in system commands now. Still to do: bulk rename like "r sel.ext png" to change every selected extension, a `cwd` word, and subsections (like "cwd.build/" for `<cwd>/build/`)
- keybinds from a config file (the keymap is already one table of key -> command line, so this is mostly parsing)
- targets outside the current directory (`trash ../foo`), and index ranges (`trash 3-7`)
- Command templates "template compress = tar -czf {dir}.tar.gz {dir}" then use it with "compress sel" in the shell buffer
- Quick Fuzzy search ("/" in file buffer?)
- Quickview trash buffer (view whats been put into the trash by quickfish)
- Undo with a preview inside the file buffer
- Edit file/folder permissions via a simple tui/buffer (include adding +/-x)
- File as objects system. Files can be tagged and sorted and marked as "important" or other tags that effect actions towards them

## Buffer system
**Core Buffers:**
File Buffer: The buffer for files. It's always the default buffer you start in.
Shell Buffer: Can run commands and more complex features that the quickshell cannot easily.
No children yet.
  
**Secondary Buffers:**
Preview Buffer: Child of the File Buffer.
Operation Buffer (TODO): Will display actions like a preview for some things, batch actions, things that cant fit in the File Buffer, but need a TUI. Also a child of the File Buffer
