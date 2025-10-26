## Usage

### Compiling

To compile the editor, simply run `make`:

```sh
make
```

### Running

To open a file, provide its name as an argument:

```sh
./ThaweCode <filename>
```

If the file doesn't exist, it will be created. If you run `./ThaweCode` without a filename, you will start with an empty, unnamed buffer.

### Keybindings

*   **`Ctrl-S`**: Save the current file.
*   **`Ctrl-Q`**: Quit the editor.
    *   If the file has unsaved changes, you will be warned and must press `Ctrl-Q` a configurable number of times to quit (default is 3).
*   **`Ctrl-F`**: Search for text within the file.
    *   Use `Enter` or arrow keys to navigate between matches.
    *   Press `Esc` to cancel the search.

#### Text Editing

*   **`Ctrl+Space`**: Toggle selection mode. Press once to set the selection mark, move the cursor to select text, and press again to cancel.
*   **`Ctrl+X`**: Cut the selected text.
*   **`Ctrl+K`**: Copy the selected text to the internal clipboard.
*   **`Ctrl+P`**: Paste the text from the clipboard.

#### Navigation

*   **Arrow Keys**: Move the cursor.
*   **`Home` / `End`**: Move cursor to the start/end of the current line.
*   **`Page Up` / `Page Down`**: Move the cursor up or down by one screen length.
*   **`Backspace` / `Delete`**: Delete characters.

---

## Configuration

You can customize the editor's behavior by creating a configuration file named `.ThaweCoderc` in your home directory.

**Location:** The file must be placed at `~/.ThaweCoderc` (e.g., `/home/youruser/.ThaweCoderc`).

The file uses a simple `key = value` format. Lines beginning with `#` are treated as comments and are ignored.

### Available Options

| Key          | Description                                                  | Default Value |
|--------------|--------------------------------------------------------------|---------------|
| `tab-stop`   | The number of spaces to display for a tab character.         | `8`           |
| `quit-times` | The number of times you must press `Ctrl-Q` to quit when there are unsaved changes. | `3`           |

If the `.ThaweCoderc` file is not found, or if a specific key is not present, the editor will use these default values.

### Example `.ThaweCoderc` File

```
# This is a comment.
# Use 2 spaces for tabs instead of the default 8.
tab-stop = 2

# Only require pressing Ctrl-Q once to quit with unsaved changes.
quit-times = 1
```
