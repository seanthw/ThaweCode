## Usage

### Compiling

To compile the editor, simply run `make`:

```sh
make
```

### Running

To open a file, provide its name as an argument:

```sh
./kilo <filename>
```

If the file doesn't exist, it will be created. If you run `./kilo` without a filename, you will start with an empty, unnamed buffer.

### Keybindings

*   **`Ctrl-S`**: Save the current file.
*   **`Ctrl-Q`**: Quit the editor.
    *   If the file has unsaved changes, you will be warned and must press `Ctrl-Q` three more times to force quit.
*   **`Ctrl-F`**: Search for text within the file.
    *   Use `Enter` or arrow keys to navigate between matches.
    *   Press `Esc` to cancel the search.
*   **Arrow Keys**: Move the cursor.
*   **`Home` / `End`**: Move cursor to the start/end of the current line.
*   **`Page Up` / `Page Down`**: Move the cursor up or down by one screen length.
*   **`Backspace` / `Delete`**: Delete characters.
