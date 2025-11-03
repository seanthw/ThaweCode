/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <ncurses.h>
#include <unistd.h>
#include "syntax.h"
#include "config.h"

/*** defines ***/

#define THAWECODE_VERSION "0.5.0"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};


/*** data ***/

struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback) (char *, int));
void initColors();
int getWindowSize(int *rows, int *cols);
void editorApplyHardWrap();

/*** terminal ***/

void die(const char *s) {
  endwin();   // Cleanly exit ncurses mode

  perror(s);
  exit(1);
}

void initColors() {
  init_pair(COLOR_PAIR_COMMENT, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_PAIR_KEYWORD1, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_PAIR_KEYWORD2, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_PAIR_STRING, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(COLOR_PAIR_NUMBER, COLOR_RED, COLOR_BLACK);
  init_pair(COLOR_PAIR_MATCH, COLOR_BLUE, COLOR_BLACK);
  init_pair(COLOR_PAIR_NORMAL, COLOR_WHITE, COLOR_BLACK);
  init_pair(COLOR_PAIR_GUTTER, COLOR_WHITE, COLOR_BLACK);
}

int editorReadKey() {
  int key = getch();
  switch (key)  {
    case KEY_RESIZE:
      getWindowSize(&E.screenrows, &E.screencols);
      E.screenrows -= 2;
      return key;
    case KEY_UP: return ARROW_UP;
    case KEY_DOWN: return ARROW_DOWN;
    case KEY_LEFT: return ARROW_LEFT;
    case KEY_RIGHT: return ARROW_RIGHT;
    case KEY_PPAGE: return PAGE_UP;
    case KEY_NPAGE: return PAGE_DOWN;
    case KEY_HOME: return HOME_KEY;
    case KEY_END: return END_KEY;
    case KEY_DC: return DEL_KEY;
    case KEY_ENTER: return '\n';
    case 127:
    case KEY_BACKSPACE: return BACKSPACE;
    case 27: return '\x1b'; // Escape key
    default: return key;
  }
}

int getWindowSize(int *rows, int *cols) {
  getmaxyx(stdscr, *rows, *cols);
  return 0;
}

/*** syntax highliting ***/

int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) {
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);

  if (E.syntax == NULL) return;

  char **keywords = E.syntax->keywords;

  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while( i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }

    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i],mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i +=mcs_len;
        in_comment = 1;
        continue;
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
        (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    if (prev_sep) {
      int j;
      for(j = 0; keywords[j]; j++) {
        int klen = strlen(keywords[j]);
        int kw2 = keywords[j][klen - 1] == '|';
        if (kw2) klen--;

        if (!strncmp(&row->render[i], keywords[j], klen) &&
           is_separator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
          i += klen;
          break;
        }
      }
      if (keywords[j] != NULL) {
        prev_sep = 0;
        continue;
      }
    }

    prev_sep = is_separator(c);
    i++;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows) {
    editorUpdateSyntax(&E.row[row->idx + 1]);
  }
}

int editorSyntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return COLOR_PAIR_COMMENT;
    case HL_KEYWORD1: return COLOR_PAIR_KEYWORD1;
    case HL_KEYWORD2: return COLOR_PAIR_KEYWORD2;
    case HL_STRING: return COLOR_PAIR_STRING;
    case HL_NUMBER: return COLOR_PAIR_NUMBER;
    case HL_MATCH: return COLOR_PAIR_MATCH;
    case HL_GUTTER: return COLOR_PAIR_GUTTER;
    default: return COLOR_PAIR_NORMAL;
  }
}

void editorSelectSyntaxHighlight() {
  E.syntax = NULL;
  if (E.filename == NULL) return;

  char *ext = strrchr(E.filename, '.');

  for (unsigned int j = 0; HLDB[j].filetype; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;

        int filerow;
        for (filerow = 0; filerow < E.numrows; filerow++) {
          editorUpdateSyntax(&E.row[filerow]);
        }
        return;
      }
      i++;
    }
  }
}

/*** row operations ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (E.tab_stop - 1) - (rx % E.tab_stop);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (E.tab_stop - 1) - (cur_rx % E.tab_stop);
    cur_rx++;

    if (cur_rx > rx) return cx;
  }
  return cx;
}

char *editorGetIdent(erow *row) {
  if (row == NULL) return NULL;

  int ident_len = 0;
  while (ident_len < row->size && isspace(row->chars[ident_len])) {
    ident_len++;
  }

  if (ident_len == 0) return NULL;

  char *ident = malloc(ident_len + 1);
  if (ident == NULL) return NULL;

  memcpy(ident, row->chars, ident_len);
  ident[ident_len] = '\0';
  return ident;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(E.tab_stop - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % E.tab_stop != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
  for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

  E.row[at].idx = at;

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  E.row[at].hl = NULL;
  E.row[at].hl_open_comment = 0;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at, int count) {
  if (at < 0 || at >= row->size) return;
  if (at + count > row->size) count = row->size - at;

  memmove(&row->chars[at], &row->chars[at + count], row->size - at - count);
  row->size -= count;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
  editorApplyHardWrap();
}

void editorInsertNewline() {
  char *ident = NULL;
  if (E.cy < E.numrows) {
    ident = editorGetIdent(&E.row[E.cy]);
  }
  int ident_len = (ident) ? strlen(ident) : 0;
  
  if (E.cx == 0) {
    editorInsertRow(E.cy, ident ? ident : "", ident_len);
  } else {
    erow *row = &E.row[E.cy];
    size_t len_after_cursor = row->size - E.cx;
    char *new_content = malloc(ident_len + len_after_cursor + 1);
    if (ident) {
      memcpy(new_content, ident, ident_len);
    }
    memcpy(new_content + ident_len, &row->chars[E.cx], len_after_cursor);
    new_content[ident_len + len_after_cursor] = '\0';

    editorInsertRow(E.cy + 1, new_content, ident_len + len_after_cursor);
    free(new_content);

    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = ident_len;

  if (ident) {
    free(ident);
  }
}

void editorApplyHardWrap() {
  if (!E.hard_wrap) return;

  erow *row = &E.row[E.cy];
  int wrap_width = E.screencols - 5;

  if (row->rsize <= wrap_width) return;

  int wrap_char_idx = editorRowRxToCx(row, wrap_width);

  int break_char_idx = -1;
  for (int i = wrap_char_idx; i >= 0; i--) {
    if (row->chars[i] == ' ') {
      break_char_idx = i;
      break;
    }
  }

  if (break_char_idx == -1) return;

  int content_start_idx = break_char_idx;
  while (content_start_idx < row->size && isspace(row->chars[content_start_idx])) {
    content_start_idx++;
  }

  if (content_start_idx >= row->size) return;

  char *content_to_move = &row->chars[content_start_idx];
  int len_to_move = row->size - content_start_idx;

  editorInsertRow(E.cy + 1, content_to_move, len_to_move);

  row = &E.row[E.cy]; // Re-fetch the pointer after realloc

  row->size = break_char_idx;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);

  if (E.cx > break_char_idx) {
    E.cy++;
    E.cx = E.cx - content_start_idx;
  }
}

void editorDelChar() {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    // Soft Tab Deletion Logic
    if (E.soft_tabs && (E.cx % E.tab_stop == 0)) {
      // Check if there are enough characters to even be a soft tab
      if (E.cx >= E.tab_stop) {
        int is_soft_tab = 1;
        // Check if the preceding characters are all spaces
        for (int i = 1; i <= E.tab_stop; i++) {
          if (row->chars[E.cx - i] != ' ') {
            is_soft_tab = 0;
            break;
          }
        }

        if (is_soft_tab) {
          editorRowDelChar(row, E.cx - E.tab_stop, E.tab_stop);
          E.cx -= E.tab_stop;
          return;
        }
      }
    }

    editorRowDelChar(row, E.cx - 1, 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

void editorCopy() {
  if (!E.selection_active) return;

  free(E.clipboard);
  E.clipboard = NULL;

  int start_row, start_col, end_row, end_col;
  if (E.cy < E.mark_cy || (E.cy == E.mark_cy && E.cx < E.mark_cx)) {
    start_row = E.cy;
    start_col = E.cx;
    end_row   = E.mark_cy;
    end_col   = E.mark_cx;
  } else {
    start_row = E.mark_cy;
    start_col   = E.mark_cx;
    end_row   = E.cy;
    end_col   = E.cx;
  }

  int total_len = 0;

  for (int i = start_row; i <= end_row; i++) {
    erow *row = &E.row[i];
    int row_start = (i == start_row) ? start_col : 0;
    int row_end = (i == end_row) ? end_col : row->size;
    total_len += (row_end - row_start);
    if (i < end_row) total_len++;
  }

  if (total_len == 0) return;

  E.clipboard = malloc(total_len + 1);
  if (!E.clipboard) return;

  char *p = E.clipboard;

  for (int i = start_row; i <= end_row; i++) {
    erow *row = &E.row[i];
    int row_start = (i == start_row) ? start_col : 0;
    int row_end = (i == end_row) ? end_col : row->size;
    int row_len = row_end - row_start;

    memcpy(p, &row->chars[row_start], row_len);
    p += row_len;

    if (i < end_row) {
      *p = '\n';
      p++;
    }
  }
  *p = '\0'; // Null-terminate the string

  editorSetStatusMessage("%d bytes copied to clipboard.", total_len);
}

void editorDeleteSelection() {
  if (!E.selection_active) return;

  // Determine start and end points
  int start_row, start_col, end_row, end_col;
  if (E.cy < E.mark_cy || (E.cy == E.mark_cy && E.cx < E.mark_cx)) {
    start_row = E.cy; start_col = E.cx;
    end_row = E.mark_cy; end_col = E.mark_cx;
  } else {
    start_row = E.mark_cy; start_col = E.mark_cx;
    end_row = E.cy; end_col = E.cx;
  }

  E.cy = start_row;
  E.cx = start_col;

  if (start_row == end_row) {
    // Single-line deletion
    editorRowDelChar(&E.row[start_row], start_col, end_col - start_col);
  } else {
    // Multi-line deletion
    erow *first_row = &E.row[start_row];
    erow *last_row = &E.row[end_row];

    editorRowDelChar(first_row, start_col, first_row->size - start_col);

    editorRowDelChar(last_row, 0, end_col);

    editorRowAppendString(first_row, last_row->chars, last_row->size);

    for (int i = end_row; i > start_row; i--) {
      editorDelRow(i);
    }
  }

  E.selection_active = 0;
  E.dirty++;
}

void editorPaste() {
  if (E.clipboard == NULL) return;

  for (int i = 0; E.clipboard[i] != '\0'; i++) {
    if (E.clipboard[i] == '\n') {
      editorInsertNewline();
    } else {
      editorInsertChar(E.clipboard[i]);
    }
  }
}

void editorCut() {
  if (!E.selection_active) return;
  editorCopy();
  editorDeleteSelection();
}

/*** file i/o ***/

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  editorSelectSyntaxHighlight();

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen -1] == '\n' ||
                           line[linelen -1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave() {
  if (E.filename == NULL) {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (E.filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    };
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));

}

/*** find ***/

void editorFindCallback(char *query, int key){
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;
  static char *saved_hl = NULL;

  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }

  if (key == '\r' || key == '\x1b' || key == '\n' || key == KEY_ENTER) {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == KEY_RIGHT || key == KEY_DOWN) {
    direction = 1;
  } else if (key == KEY_LEFT || key == KEY_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == - 1) direction = 1;
  int current = last_match;
  int i;
  for (i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    else if (current == E.numrows) current = 0;

    erow *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorFind() {
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                             editorFindCallback);

  if (query) {
    free(query);
  } else {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

/*** output ***/

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.soft_wrap) {
    E.coloff = 0; // No horizontal scrolling with soft warp
    int display_y = 0;
    // Calculate the total number of display lines up to the cursor's line
    for (int i = 0; i < E.cy; i++) {
      display_y += (E.row[i].rsize / (E.screencols - 5)) + 1;
    }
    // Add the display lines within the cursor's line
    display_y += E.rx / (E.screencols - 5);

    if(display_y < E.rowoff) {
      E.rowoff = display_y;
    }
    if (display_y >= E.rowoff + E.screenrows) {
      E.rowoff = display_y - E.screenrows + 1;
    }
  } else {
    if (E.cy < E.rowoff) {
      E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
      E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
      E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols - 5) {
      E.coloff = E.rx - (E.screencols - 5) + 1;
    }
  }
 }

int is_char_in_selection(int filerow, int char_idx) {
  if (!E.selection_active) return 0;

  int start_row, start_col, end_row, end_col;

  if (E.cy < E.mark_cy || (E.cy == E.mark_cy && E.cx < E.mark_cx)) {
    // Cursor is before the mark
    start_row = E.cy;
    start_col = E.cx;
    end_row   = E.mark_cy;
    end_col   = E.mark_cx;
  } else {
    // Mark is before the cursor
    start_row = E.mark_cy;
    start_col = E.mark_cx;
    end_row   = E.cy;
    end_col   = E.cx;
  }

  if (filerow < start_row || filerow > end_row) {
    return 0;
  }
  if (filerow == start_row && char_idx < start_col) {
    return 0;
  }
  if (filerow == end_row && char_idx >= end_col) {
    return 0;
  }

  return 1;
}

void editorDrawRows() {
  int y;
  for (y = 0; y < E.screenrows; y++) {
    if (E.soft_wrap) {
      int target_display_line = E.rowoff + y;

      int filerow_idx = -1;
      int line_offset_in_row = 0;

      // Find which file row and which wrapped line within it corresponds to the target_display_line
      int display_line_counter = 0;
      for (int i = 0; i < E.numrows; i++) {
        int lines_for_this_row = (E.row[i].rsize / (E.screencols - 5)) + 1;
        if (display_line_counter + lines_for_this_row > target_display_line) {
          filerow_idx = i;
          line_offset_in_row = target_display_line - display_line_counter;
          break;
        }
        display_line_counter += lines_for_this_row;
      }

      if (filerow_idx != -1) {
        erow *row = &E.row[filerow_idx];
        int start_char_offset = line_offset_in_row * (E.screencols - 5);
        
        if (start_char_offset >= row->rsize) {
            mvprintw(y, 0, "~");
            continue;
        }

        int len = row->rsize - start_char_offset;
        if (len > (E.screencols - 5)) len = (E.screencols - 5);

        char *c = &row->render[start_char_offset];
        unsigned char *hl = &row->hl[start_char_offset];

        // Gutter: only for the first line of a wrapped row
        if (line_offset_in_row == 0) {
          attron(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));
          mvprintw(y, 0, "%4d ", filerow_idx + 1);
          attroff(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));
        } else {
          attron(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));
          mvprintw(y, 0, "   . "); // Indicate continuation
          attroff(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));
        }

        for (int j = 0; j < len; j++) {
          attron(COLOR_PAIR(editorSyntaxToColor(hl[j])));
          mvprintw(y, j + 5, "%c", c[j]);
          attroff(COLOR_PAIR(editorSyntaxToColor(hl[j])));
        }
      } else {
         mvprintw(y, 0, "~");
      }
    } else { // Original non-wrapped drawing logic
      int filerow = y + E.rowoff;
      if (filerow >= E.numrows) {
        if (E.numrows == 0 && y == E.screenrows / 3) {
          char welcome[80];
          int welcomelen = snprintf(welcome, sizeof(welcome),
                                    "ThaweCode editor -- version %s", THAWECODE_VERSION);
          if (welcomelen > E.screencols) welcomelen = E.screencols;
          int padding = (E.screencols - welcomelen) / 2;
          if (padding) {
            mvprintw(y, 0, "~");
          }
          mvprintw(y, padding, "%s", welcome);
        } else {
          mvprintw(y, 0, "~");
        }
      } else {
        int len = E.row[filerow].rsize - E.coloff;
        if (len < 0) len = 0;
        if (len > E.screencols) len = E.screencols;
        char *c = &E.row[filerow].render[E.coloff];
        unsigned char *hl = &E.row[filerow].hl[E.coloff];

        attron(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));
        mvprintw(y, 0, "%4d ", filerow + 1);
        attroff(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));

        for (int j = 0; j < len; j++) {
          if (is_char_in_selection(filerow, E.coloff + j)) {
            attron(A_REVERSE);
          }

          attron(COLOR_PAIR(editorSyntaxToColor(hl[j])));
          mvprintw(y, j + 5, "%c", c[j]);
          attroff(COLOR_PAIR(editorSyntaxToColor(hl[j])));

          attroff(A_REVERSE);
        }
      }
    }
  }
}

void editorDrawStatusBar() {
  attron(A_REVERSE);
  move(E.screenrows, 0);
  clrtoeol();

  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No name]", E.numrows,
                     E.dirty ? "(modified)" : " ");
  int rlen = snprintf(rstatus, sizeof(rstatus), " %s | %d/%d",
                      E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);

  // Create a buffer for the full line
  char line[E.screencols + 1];
  memset(line, ' ', E.screencols);
  line[E.screencols] = '\0';

  // Copy left status
  if (len > E.screencols) len = E.screencols;
  memcpy(line, status, len);

  // Copy right status if there's room
  if (E.screencols >= rlen) {
      memcpy(line + E.screencols - rlen, rstatus, rlen);
  }

  mvprintw(E.screenrows, 0, "%s", line);

  attroff(A_REVERSE);
}

void editorDrawMessageBar() {
  move(E.screenrows + 1, 0);
  clrtoeol();
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5) {
    char msg[msglen + 1];
    memcpy(msg, E.statusmsg, msglen);
    msg[msglen] = '\0';
    mvprintw(E.screenrows + 1, 0, "%s", msg);
  }
}

void editorRefreshScreen() {
  editorScroll();

  erase(); // Clear screen (ncurses equivalent of \x1b[2J)
  
  editorDrawRows();
  editorDrawStatusBar();
  editorDrawMessageBar();

  int final_cy, final_cx;
  if (E.soft_wrap) {
    int display_y = 0;
    for (int i = 0; i < E.cy; i++) {
      display_y += (E.row[i].rsize / (E.screencols - 5)) + 1;
    }
    display_y += E.rx / (E.screencols - 5);
    final_cy = display_y - E.rowoff;
    final_cx = (E.rx % (E.screencols - 5)) + 5;
  } else {
    final_cy = E.cy - E.rowoff;
    final_cx = E.rx - E.coloff + 5;
  }
  move(final_cy, final_cx);

  refresh(); // Refresh the screen (ncurses equivalent of write())
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
      char *buf = malloc(128);
      size_t bufsize = 128;
      size_t buflen = 0;
      buf[0] = '\0';

      while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = getch(); // Use getch() directly
        if (c == KEY_DC || c == CTRL_KEY('h') || c == KEY_BACKSPACE || c == 127) {
          if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
          editorSetStatusMessage("");
          if (callback) callback(buf, c);
          free(buf);
          return NULL;
        } else if (c == '\n' || c == '\r') {
          if (buflen != 0) {
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            return buf;
          }
        } else if (!iscntrl(c) && c < 128) {
          if (buflen == bufsize - 1) {
            bufsize *= 2;
            buf = realloc(buf, bufsize);
          }
          buf[buflen++] = c;
          buf[buflen] = '\0';
        }
        if (callback) callback(buf, c);
      }
    }

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch(key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row -> size) {
        E.cx++;
      } else if (row && E.cx == row -> size) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if(E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows) {
        E.cy++;
      }
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row -> size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress() {
  static int quit_times = 3; // This will be updated by the first non-quit keypress

  int c = editorReadKey();

  switch (c) {
    case KEY_RESIZE: break;
    case '\n':
      editorInsertNewline();
      break;

    case '\t':
      if (E.soft_tabs) {
        for (int i = 0; i < E.tab_stop; i++) {
          editorInsertChar(' ');
        }
      } else {
        editorInsertChar('\t');
      }
      break;

    case CTRL_KEY(' '): // Ctrl+Space
      if (E.selection_active) {
        E.selection_active = 0;
        editorSetStatusMessage("Selection cancelled.");
      } else {
        E.selection_active = 1;
        E.mark_cx = E.cx;
        E.mark_cy = E.cy;
        editorSetStatusMessage("Selection mark set. Move cursor to select. Ctrl+Space to cancel.");
      }
      break;

    case CTRL_KEY('k'):
      editorCopy();
      E.selection_active = 0;
      break;

    case CTRL_KEY('v'):
      editorPaste();
      break;

    case CTRL_KEY('x'):
      editorCut();
      break;

    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                               "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }

      endwin();
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case HOME_KEY:
      E.cx = 0;
      break;

    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) {
          E.cy = E.rowoff;
        } else if (c == PAGE_DOWN) {
          E.cy = E.rowoff + E.screenrows - 1;
          if (E.cy > E.numrows) E.cy = E.numrows;
        }

        int times = E.screenrows;
        while(times--)
          editorMoveCursor( c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      editorInsertChar(c);
      break;
  }

  // Reset quit_times on any keypress that isn't the quit key
  if (c != CTRL_KEY('q')) {
    quit_times = E.quit_times;
  }
}

/*** init ***/

void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  E.mark_cx = 0;
  E.mark_cy = 0;
  E.selection_active = 0;
  E.clipboard = NULL;

  // --- SET CONFIG DEFAULTS ---
  E.soft_tabs = 0;
  E.tab_stop = 8;
  E.soft_wrap = 0;
  E.hard_wrap = 0;
  E.quit_times = 3;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  initscr();               // Start ncurses mode
  raw();                   // Go into raw mode (character-at-a-time)
  noecho();                // Don't echo characters as they are typed
  keypad(stdscr, TRUE);    // Enable F-keys, arrow keys, etc
  start_color();           // Enable color support
  initColors();

  initEditor();
  load_config();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage(
    "C-s:save | C-q:quit | C-f:find | C-spc:select | C-x:cut | C-k:copy | C-v:paste");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
