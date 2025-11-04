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

#define THAWECODE_VERSION "0.7.0"

#define CTRL_KEY(k) ((k) & 0x1f)

#define CURRENT_BUFFER (E.buffers[E.current_buffer])

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
void editorUndo();
void editorRedo();
void editorAddUndoAction(enum editorActionType type, char *data, size_t len);
void editorSave();
void editorNewBuffer();
void initBuffer(struct Buffer *b);
void editorSwitchBuffer();
void editorShowBufferList();
void editorCloseBuffer();

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

  if (CURRENT_BUFFER->syntax == NULL) return;

  char **keywords = CURRENT_BUFFER->syntax->keywords;

  char *scs = CURRENT_BUFFER->syntax->singleline_comment_start;
  char *mcs = CURRENT_BUFFER->syntax->multiline_comment_start;
  char *mce = CURRENT_BUFFER->syntax->multiline_comment_end;

  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && CURRENT_BUFFER->row[row->idx - 1].hl_open_comment);

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

    if (CURRENT_BUFFER->syntax->flags & HL_HIGHLIGHT_STRINGS) {
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

    if (CURRENT_BUFFER->syntax->flags & HL_HIGHLIGHT_NUMBERS) {
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
  if (changed && row->idx + 1 < CURRENT_BUFFER->numrows) {
    editorUpdateSyntax(&CURRENT_BUFFER->row[row->idx + 1]);
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
  CURRENT_BUFFER->syntax = NULL;
  if (CURRENT_BUFFER->filename == NULL) return;

  char *ext = strrchr(CURRENT_BUFFER->filename, '.');

  for (unsigned int j = 0; HLDB[j].filetype; j++) {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i]) {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(CURRENT_BUFFER->filename, s->filematch[i]))) {
        CURRENT_BUFFER->syntax = s;

        int filerow;
        for (filerow = 0; filerow < CURRENT_BUFFER->numrows; filerow++) {
          editorUpdateSyntax(&CURRENT_BUFFER->row[filerow]);
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
      rx += (CURRENT_BUFFER->tab_stop - 1) - (rx % CURRENT_BUFFER->tab_stop);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (CURRENT_BUFFER->tab_stop - 1) - (cur_rx % CURRENT_BUFFER->tab_stop);
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
  row->render = malloc(row->size + tabs*(CURRENT_BUFFER->tab_stop - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % CURRENT_BUFFER->tab_stop != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > CURRENT_BUFFER->numrows) return;

  CURRENT_BUFFER->row = realloc(CURRENT_BUFFER->row, sizeof(erow) * (CURRENT_BUFFER->numrows + 1));
  memmove(&CURRENT_BUFFER->row[at + 1], &CURRENT_BUFFER->row[at], sizeof(erow) * (CURRENT_BUFFER->numrows - at));
  for (int j = at + 1; j <= CURRENT_BUFFER->numrows; j++) CURRENT_BUFFER->row[j].idx++;

  CURRENT_BUFFER->row[at].idx = at;

  CURRENT_BUFFER->row[at].size = len;
  CURRENT_BUFFER->row[at].chars = malloc(len + 1);
  memcpy(CURRENT_BUFFER->row[at].chars, s, len);
  CURRENT_BUFFER->row[at].chars[len] = '\0';

  CURRENT_BUFFER->row[at].rsize = 0;
  CURRENT_BUFFER->row[at].render = NULL;
  CURRENT_BUFFER->row[at].hl = NULL;
  CURRENT_BUFFER->row[at].hl_open_comment = 0;
  editorUpdateRow(&CURRENT_BUFFER->row[at]);

  CURRENT_BUFFER->numrows++;
  CURRENT_BUFFER->dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void editorDelRow(int at) {
  if (at < 0 || at >= CURRENT_BUFFER->numrows) return;
  editorFreeRow(&CURRENT_BUFFER->row[at]);
  memmove(&CURRENT_BUFFER->row[at], &CURRENT_BUFFER->row[at + 1], sizeof(erow) * (CURRENT_BUFFER->numrows - at - 1));
  for (int j = at; j < CURRENT_BUFFER->numrows - 1; j++) CURRENT_BUFFER->row[j].idx--;
  CURRENT_BUFFER->numrows--;
  CURRENT_BUFFER->dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  CURRENT_BUFFER->dirty++;
}

void editorRowDelChar(erow *row, int at, int count) {
  if (at < 0 || at >= row->size) return;
  if (at + count > row->size) count = row->size - at;

  memmove(&row->chars[at], &row->chars[at + count], row->size - at - count);
  row->size -= count;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  CURRENT_BUFFER->dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  CURRENT_BUFFER->dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c) {
  if (CURRENT_BUFFER->cy == CURRENT_BUFFER->numrows) {
    editorInsertRow(CURRENT_BUFFER->numrows, "", 0);
  }
  editorRowInsertChar(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy], CURRENT_BUFFER->cx, c);
  editorAddUndoAction(ACTION_INSERT, (char *)&c, 1);
  CURRENT_BUFFER->cx++;
  editorApplyHardWrap();
}

void editorInsertNewline() {
  char *ident = NULL;
  if (CURRENT_BUFFER->cy < CURRENT_BUFFER->numrows) {
    ident = editorGetIdent(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy]);
  }
  int ident_len = (ident) ? strlen(ident) : 0;
  
  if (CURRENT_BUFFER->cx == 0) {
    editorInsertRow(CURRENT_BUFFER->cy, ident ? ident : "", ident_len);
  } else {
    erow *row = &CURRENT_BUFFER->row[CURRENT_BUFFER->cy];
    size_t len_after_cursor = row->size - CURRENT_BUFFER->cx;
    char *new_content = malloc(ident_len + len_after_cursor + 1);
    if (ident) {
      memcpy(new_content, ident, ident_len);
    }
    memcpy(new_content + ident_len, &row->chars[CURRENT_BUFFER->cx], len_after_cursor);
    new_content[ident_len + len_after_cursor] = '\0';

    editorInsertRow(CURRENT_BUFFER->cy + 1, new_content, ident_len + len_after_cursor);
    free(new_content);

    row = &CURRENT_BUFFER->row[CURRENT_BUFFER->cy];
    row->size = CURRENT_BUFFER->cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  CURRENT_BUFFER->cy++;
  CURRENT_BUFFER->cx = ident_len;

  if (ident) {
    free(ident);
  }
}

void editorApplyHardWrap() {
  if (!E.hard_wrap) return;

  erow *row = &CURRENT_BUFFER->row[CURRENT_BUFFER->cy];
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

  editorInsertRow(CURRENT_BUFFER->cy + 1, content_to_move, len_to_move);

  row = &CURRENT_BUFFER->row[CURRENT_BUFFER->cy]; // Re-fetch the pointer after realloc

  row->size = break_char_idx;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);

  if (CURRENT_BUFFER->cx > break_char_idx) {
    CURRENT_BUFFER->cy++;
    CURRENT_BUFFER->cx = CURRENT_BUFFER->cx - content_start_idx;
  }
}

void editorDelChar() {
  if (CURRENT_BUFFER->cy == CURRENT_BUFFER->numrows) return;
  if (CURRENT_BUFFER->cx == 0 && CURRENT_BUFFER->cy == 0) return;

  erow *row = &CURRENT_BUFFER->row[CURRENT_BUFFER->cy];
  if (CURRENT_BUFFER->cx > 0) {
    if (CURRENT_BUFFER->soft_tabs && (CURRENT_BUFFER->cx % CURRENT_BUFFER->tab_stop == 0)) {
      if (CURRENT_BUFFER->cx >= CURRENT_BUFFER->tab_stop) {
        int is_soft_tab = 1;
        for (int i = 1; i <= CURRENT_BUFFER->tab_stop; i++) {
          if (row->chars[CURRENT_BUFFER->cx - i] != ' ') {
            is_soft_tab = 0;
            break;
          }
        }

        if (is_soft_tab) {
          char *delete_spaces = malloc(CURRENT_BUFFER->tab_stop);
          memcpy(delete_spaces, &row->chars[CURRENT_BUFFER->cx - CURRENT_BUFFER->tab_stop], CURRENT_BUFFER->tab_stop);
          editorAddUndoAction(ACTION_DELETE, delete_spaces, CURRENT_BUFFER->tab_stop);
          free(delete_spaces);

          editorRowDelChar(row, CURRENT_BUFFER->cx - CURRENT_BUFFER->tab_stop, CURRENT_BUFFER->tab_stop);
          CURRENT_BUFFER->cx -= CURRENT_BUFFER->tab_stop;
          return;
        }
      }
    }
    
    char delete_char = row->chars[CURRENT_BUFFER->cx - 1];
    editorAddUndoAction(ACTION_DELETE, &delete_char, 1);

    editorRowDelChar(row, CURRENT_BUFFER->cx - 1, 1);
    CURRENT_BUFFER->cx--;
  } else {
    char newline_char = '\n';
    editorAddUndoAction(ACTION_DELETE, &newline_char, 1);

    CURRENT_BUFFER->cx = CURRENT_BUFFER->row[CURRENT_BUFFER->cy - 1].size;
    editorRowAppendString(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy - 1], row->chars, row->size);
    editorDelRow(CURRENT_BUFFER->cy);
    CURRENT_BUFFER->cy--;
  }
}

void editorCopy() {
  if (!CURRENT_BUFFER->selection_active) return;

  free(CURRENT_BUFFER->clipboard);
  CURRENT_BUFFER->clipboard = NULL;

  int start_row, start_col, end_row, end_col;
  if (CURRENT_BUFFER->cy < CURRENT_BUFFER->mark_cy || (CURRENT_BUFFER->cy == CURRENT_BUFFER->mark_cy && CURRENT_BUFFER->cx < CURRENT_BUFFER->mark_cx)) {
    start_row = CURRENT_BUFFER->cy;
    start_col = CURRENT_BUFFER->cx;
    end_row   = CURRENT_BUFFER->mark_cy;
    end_col   = CURRENT_BUFFER->mark_cx;
  } else {
    start_row = CURRENT_BUFFER->mark_cy;
    start_col   = CURRENT_BUFFER->mark_cx;
    end_row   = CURRENT_BUFFER->cy;
    end_col   = CURRENT_BUFFER->cx;
  }

  int total_len = 0;

  for (int i = start_row; i <= end_row; i++) {
    erow *row = &CURRENT_BUFFER->row[i];
    int row_start = (i == start_row) ? start_col : 0;
    int row_end = (i == end_row) ? end_col : row->size;
    total_len += (row_end - row_start);
    if (i < end_row) total_len++;
  }

  if (total_len == 0) return;

  CURRENT_BUFFER->clipboard = malloc(total_len + 1);
  if (!CURRENT_BUFFER->clipboard) return;

  char *p = CURRENT_BUFFER->clipboard;

  for (int i = start_row; i <= end_row; i++) {
    erow *row = &CURRENT_BUFFER->row[i];
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
  if (!CURRENT_BUFFER->selection_active) return;

  // Determine start and end points
  int start_row, start_col, end_row, end_col;
  if (CURRENT_BUFFER->cy < CURRENT_BUFFER->mark_cy || (CURRENT_BUFFER->cy == CURRENT_BUFFER->mark_cy && CURRENT_BUFFER->cx < CURRENT_BUFFER->mark_cx)) {
    start_row = CURRENT_BUFFER->cy; start_col = CURRENT_BUFFER->cx;
    end_row = CURRENT_BUFFER->mark_cy; end_col = CURRENT_BUFFER->mark_cx;
  } else {
    start_row = CURRENT_BUFFER->mark_cy; start_col = CURRENT_BUFFER->mark_cx;
    end_row = CURRENT_BUFFER->cy; end_col = CURRENT_BUFFER->cx;
  }

  CURRENT_BUFFER->cy = start_row;
  CURRENT_BUFFER->cx = start_col;

  if (start_row == end_row) {
    // Single-line deletion
    editorRowDelChar(&CURRENT_BUFFER->row[start_row], start_col, end_col - start_col);
  } else {
    // Multi-line deletion
    erow *first_row = &CURRENT_BUFFER->row[start_row];
    erow *last_row = &CURRENT_BUFFER->row[end_row];

    editorRowDelChar(first_row, start_col, first_row->size - start_col);

    editorRowDelChar(last_row, 0, end_col);

    editorRowAppendString(first_row, last_row->chars, last_row->size);

    for (int i = end_row; i > start_row; i--) {
      editorDelRow(i);
    }
  }

  CURRENT_BUFFER->selection_active = 0;
  CURRENT_BUFFER->dirty++;
}

void editorPaste() {
  if (CURRENT_BUFFER->clipboard == NULL) return;

  for (int i = 0; CURRENT_BUFFER->clipboard[i] != '\0'; i++) {
    if (CURRENT_BUFFER->clipboard[i] == '\n') {
      editorInsertNewline();
    } else {
      editorInsertChar(CURRENT_BUFFER->clipboard[i]);
    }
  }
}

void editorCut() {
  if (!CURRENT_BUFFER->selection_active) return;
  editorCopy();
  editorDeleteSelection();
}

void editorAddUndoAction(enum editorActionType type, char *data, size_t len) {
  for (int i = 0; i < CURRENT_BUFFER->redo_pos; i++) {
    free(CURRENT_BUFFER->redo_stack[i].data);
  }
  CURRENT_BUFFER->redo_pos = 0;

  if (CURRENT_BUFFER->undo_pos >= CURRENT_BUFFER->undo_len) {
    CURRENT_BUFFER->undo_len = (CURRENT_BUFFER->undo_len == 0) ? 8 : CURRENT_BUFFER->undo_len * 2;
    CURRENT_BUFFER->undo_stack = realloc(CURRENT_BUFFER->undo_stack, sizeof(editorAction) * CURRENT_BUFFER->undo_len);
  }

  editorAction *action = &CURRENT_BUFFER->undo_stack[CURRENT_BUFFER->undo_pos++];
  action->type = type;
  action->cx = CURRENT_BUFFER->cx;
  action->cy = CURRENT_BUFFER->cy;
  action->len = len;
  action->data = malloc(len);
  memcpy(action->data, data, len);
}

void editorUndo() {
  if (CURRENT_BUFFER->undo_pos == 0) return;

  CURRENT_BUFFER->undo_pos--;
  editorAction *action = &CURRENT_BUFFER->undo_stack[CURRENT_BUFFER->undo_pos];

  if (CURRENT_BUFFER->redo_pos >= CURRENT_BUFFER->redo_len) {
    CURRENT_BUFFER->redo_len = (CURRENT_BUFFER->redo_len == 0) ? 8 : CURRENT_BUFFER->redo_len * 2;
    CURRENT_BUFFER->redo_stack = realloc(CURRENT_BUFFER->redo_stack, sizeof(editorAction) * CURRENT_BUFFER->redo_len);
  }
  memcpy(&CURRENT_BUFFER->redo_stack[CURRENT_BUFFER->redo_pos++], action, sizeof(editorAction));

  CURRENT_BUFFER->cx = action->cx;
  CURRENT_BUFFER->cy = action->cy;

  if (action->type ==ACTION_INSERT) {
    if (action->data[0] == '\n') {
      editorDelRow(CURRENT_BUFFER->cy);
    } else {
      editorRowDelChar(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy], CURRENT_BUFFER->cx - action->len, action->len);
    }
  } else {
    if (action->data[0] == '\n') {
      editorInsertNewline();
    } else {
      for (size_t i = 0; i < action->len; i++) {
        editorRowInsertChar(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy], CURRENT_BUFFER->cx + i, action->data[i]);
      }
    }
  }
}

void editorRedo() {
  if (CURRENT_BUFFER->redo_pos == 0) return;

  CURRENT_BUFFER->redo_pos--;
  editorAction *action = &CURRENT_BUFFER->redo_stack[CURRENT_BUFFER->redo_pos];
  memcpy(&CURRENT_BUFFER->undo_stack[CURRENT_BUFFER->undo_pos++], action, sizeof(editorAction));

  CURRENT_BUFFER->cx = action->cx;
  CURRENT_BUFFER->cy = action->cy;

  if (action->type == ACTION_INSERT) {
    if (action->data[0] == '\n') {
      editorInsertNewline();
    } else {
      for (size_t i = 0; i < action->len; i++) {
        editorRowInsertChar(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy], CURRENT_BUFFER->cx + i, action->data[i]);
      }
    }
  } else {
    if (action->data[0] == '\n') {
      editorDelRow(CURRENT_BUFFER->cy);
    } else {
      editorRowDelChar(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy], CURRENT_BUFFER->cx - action->len, action->len);
    }
  }
}
/*** file i/o ***/

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < CURRENT_BUFFER->numrows; j++)
    totlen += CURRENT_BUFFER->row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < CURRENT_BUFFER->numrows; j++) {
    memcpy(p, CURRENT_BUFFER->row[j].chars, CURRENT_BUFFER->row[j].size);
    p += CURRENT_BUFFER->row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorOpen(char *filename) {
  free(CURRENT_BUFFER->filename);
  CURRENT_BUFFER->filename = strdup(filename);

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
    editorInsertRow(CURRENT_BUFFER->numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  CURRENT_BUFFER->dirty = 0;
}

void editorNewBuffer() {
  // Check if the current buffer is dirty before switching
  if (CURRENT_BUFFER->dirty) {
    editorSetStatusMessage("Current buffer has unsaved changes. Save? (y/n/ESC)");
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == 'y' || c == 'Y') {
      editorSave();
      if (CURRENT_BUFFER->dirty) { // If save failed, don't create new buffer
        editorSetStatusMessage("Save failed. New buffer aborted.");
        return;
      }
    } else if (c == '\x1b') { // User cancelled
      editorSetStatusMessage("New buffer aborted.");
      return;
    }
    // If 'n' or any other key, proceed without saving (discard changes)
  }

  E.num_buffers++;
  E.buffers = realloc(E.buffers, sizeof(struct Buffer *) * E.num_buffers);
  E.buffers[E.num_buffers - 1] = malloc(sizeof(struct Buffer));
  initBuffer(E.buffers[E.num_buffers - 1]);
  E.current_buffer = E.num_buffers - 1;
  editorSetStatusMessage("New buffer created.");
  editorSelectSyntaxHighlight(); // Apply syntax highlighting for the new (empty) buffer
}

void editorSwitchBuffer() {
  if (E.num_buffers <= 1) {
    editorSetStatusMessage("Only one buffer open.");
    return;
  }

  E.current_buffer = (E.current_buffer + 1) % E.num_buffers;
  editorSetStatusMessage("switch to buffer %d: %s", E.current_buffer + 1,
                         CURRENT_BUFFER->filename ? CURRENT_BUFFER->filename : "[No name]");
}

void editorShowBufferList() {
  if (E.num_buffers <= 1) {
    editorSetStatusMessage("Only one buffer open.");
    return;
  }

  int height = E.num_buffers + 2;
  if (height > E.screenrows - 4) height = E.screenrows - 4;
  int width = E.screencols / 2;
  int start_y = (E.screenrows - height) / 2;
  int start_x = (E.screencols - width) / 2;
  int selected_buffer = E.current_buffer;

  while (1) {
    attron(A_REVERSE);
    for (int i = 0; i < height; i++) {
      mvprintw(start_y + i, start_x, "%*s", width, " ");
    }
    mvprintw(start_y, start_x + 1, " Open Buffers ");

    for (int i = 0; i < E.num_buffers; i++) {
      if (i >= height - 2) break;

      char *filename = E.buffers[i]->filename ? E.buffers[i]->filename : "[No name]";
      char buffer_entry[width - 2];
      snprintf(buffer_entry, sizeof(buffer_entry), "%d: %s", i + 1, filename);

      if (i == selected_buffer) {
        mvprintw(start_y + 1 + i, start_x + 1, "%s", buffer_entry);
      } else {
        attroff(A_REVERSE);
        mvprintw(start_y + 1 + i, start_x + 1, "%s", buffer_entry);
        attron(A_REVERSE);
      }
    }
    attroff(A_REVERSE);
    refresh();

    int c = editorReadKey();
    switch (c) {
      case ARROW_UP:
        selected_buffer--;
        if (selected_buffer < 0) selected_buffer = E.num_buffers - 1;
        break;
      case ARROW_DOWN:
        selected_buffer++;
        if (selected_buffer >= E.num_buffers) selected_buffer = 0;
        break;
      case '\n':
      case KEY_ENTER:
        E.current_buffer = selected_buffer;
        goto end_loop;
      case '\x1b':
        goto end_loop;
    }
  }
end_loop:
  return;
}

void editorCloseBuffer() {
  static int quit_times = 3;
  struct Buffer *b = CURRENT_BUFFER;

  if (b->dirty && quit_times > 0) {
    editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                           "Press Ctrl-Q %d more times to quit.", quit_times);
    quit_times--;
    return;
  }

  if (E.num_buffers <= 1) {
    endwin();
    exit(0);
  }

  quit_times = E.quit_times; // Reset on successful close

  // --- Free all memory associated with the buffer ---
  for (int i = 0; i < b->numrows; i++) {
    editorFreeRow(&b->row[i]);
  }
  free(b->row);
  free(b->filename);
  free(b->clipboard);
  for (int i = 0; i < b->undo_pos; i++) free(b->undo_stack[i].data);
  for (int i = 0; i < b->redo_pos; i++) free(b->redo_stack[i].data);
  free(b->undo_stack);
  free(b->redo_stack);
  free(b);

  // --- Remove the buffer pointer from the array ---
  int closing_idx = E.current_buffer;
  memmove(&E.buffers[closing_idx], &E.buffers[closing_idx + 1], sizeof(struct Buffer *) * (E.num_buffers - closing_idx - 1));
  E.num_buffers--;

  // --- Adjust the current buffer index ---
  if (E.current_buffer >= E.num_buffers) {
    E.current_buffer = E.num_buffers - 1;
  }

  editorSetStatusMessage("Buffer closed.");
}

void editorSave() {
  if (CURRENT_BUFFER->filename == NULL) {
    CURRENT_BUFFER->filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (CURRENT_BUFFER->filename == NULL) {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(CURRENT_BUFFER->filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        CURRENT_BUFFER->dirty = 0;
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
    memcpy(CURRENT_BUFFER->row[saved_hl_line].hl, saved_hl, CURRENT_BUFFER->row[saved_hl_line].rsize);
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
  for (i = 0; i < CURRENT_BUFFER->numrows; i++) {
    current += direction;
    if (current == -1) current = CURRENT_BUFFER->numrows - 1;
    else if (current == CURRENT_BUFFER->numrows) current = 0;

    erow *row = &CURRENT_BUFFER->row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      CURRENT_BUFFER->cy = current;
      CURRENT_BUFFER->cx = editorRowRxToCx(row, match - row->render);
      CURRENT_BUFFER->rowoff = CURRENT_BUFFER->numrows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorFind() {
  int saved_cx = CURRENT_BUFFER->cx;
  int saved_cy = CURRENT_BUFFER->cy;
  int saved_coloff = CURRENT_BUFFER->coloff;
  int saved_rowoff = CURRENT_BUFFER->rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                             editorFindCallback);

  if (query) {
    free(query);
  } else {
    CURRENT_BUFFER->cx = saved_cx;
    CURRENT_BUFFER->cy = saved_cy;
    CURRENT_BUFFER->coloff = saved_coloff;
    CURRENT_BUFFER->rowoff = saved_rowoff;
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
  CURRENT_BUFFER->rx = 0;
  if (CURRENT_BUFFER->cy < CURRENT_BUFFER->numrows) {
    CURRENT_BUFFER->rx = editorRowCxToRx(&CURRENT_BUFFER->row[CURRENT_BUFFER->cy], CURRENT_BUFFER->cx);
  }
  if (E.soft_wrap) {
    CURRENT_BUFFER->coloff = 0; // No horizontal scrolling with soft warp
    int display_y = 0;
    // Calculate the total number of display lines up to the cursor's line
    for (int i = 0; i < CURRENT_BUFFER->cy; i++) {
      display_y += (CURRENT_BUFFER->row[i].rsize / (E.screencols - 5)) + 1;
    }
    // Add the display lines within the cursor's line
    display_y += CURRENT_BUFFER->rx / (E.screencols - 5);

    if(display_y < CURRENT_BUFFER->rowoff) {
      CURRENT_BUFFER->rowoff = display_y;
    }
    if (display_y >= CURRENT_BUFFER->rowoff + E.screenrows) {
      CURRENT_BUFFER->rowoff = display_y - E.screenrows + 1;
    }
  } else {
    if (CURRENT_BUFFER->cy < CURRENT_BUFFER->rowoff) {
      CURRENT_BUFFER->rowoff = CURRENT_BUFFER->cy;
    }
    if (CURRENT_BUFFER->cy >= CURRENT_BUFFER->rowoff + E.screenrows) {
      CURRENT_BUFFER->rowoff = CURRENT_BUFFER->cy - E.screenrows + 1;
    }
    if (CURRENT_BUFFER->rx < CURRENT_BUFFER->coloff) {
      CURRENT_BUFFER->coloff = CURRENT_BUFFER->rx;
    }
    if (CURRENT_BUFFER->rx >= CURRENT_BUFFER->coloff + E.screencols - 5) {
      CURRENT_BUFFER->coloff = CURRENT_BUFFER->rx - (E.screencols - 5) + 1;
    }
  }
 }

int is_char_in_selection(int filerow, int char_idx) {
  if (!CURRENT_BUFFER->selection_active) return 0;

  int start_row, start_col, end_row, end_col;

  if (CURRENT_BUFFER->cy < CURRENT_BUFFER->mark_cy || (CURRENT_BUFFER->cy == CURRENT_BUFFER->mark_cy && CURRENT_BUFFER->cx < CURRENT_BUFFER->mark_cx)) {
    // Cursor is before the mark
    start_row = CURRENT_BUFFER->cy;
    start_col = CURRENT_BUFFER->cx;
    end_row   = CURRENT_BUFFER->mark_cy;
    end_col   = CURRENT_BUFFER->mark_cx;
  } else {
    // Mark is before the cursor
    start_row = CURRENT_BUFFER->mark_cy;
    start_col = CURRENT_BUFFER->mark_cx;
    end_row   = CURRENT_BUFFER->cy;
    end_col   = CURRENT_BUFFER->cx;
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
      int target_display_line = CURRENT_BUFFER->rowoff + y;

      int filerow_idx = -1;
      int line_offset_in_row = 0;

      // Find which file row and which wrapped line within it corresponds to the target_display_line
      int display_line_counter = 0;
      for (int i = 0; i < CURRENT_BUFFER->numrows; i++) {
        int lines_for_this_row = (CURRENT_BUFFER->row[i].rsize / (E.screencols - 5)) + 1;
        if (display_line_counter + lines_for_this_row > target_display_line) {
          filerow_idx = i;
          line_offset_in_row = target_display_line - display_line_counter;
          break;
        }
        display_line_counter += lines_for_this_row;
      }

      if (filerow_idx != -1) {
        erow *row = &CURRENT_BUFFER->row[filerow_idx];
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
      int filerow = y + CURRENT_BUFFER->rowoff;
      if (filerow >= CURRENT_BUFFER->numrows) {
        if (CURRENT_BUFFER->numrows == 0 && y == E.screenrows / 3) {
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
        int len = CURRENT_BUFFER->row[filerow].rsize - CURRENT_BUFFER->coloff;
        if (len < 0) len = 0;
        if (len > E.screencols) len = E.screencols;
        char *c = &CURRENT_BUFFER->row[filerow].render[CURRENT_BUFFER->coloff];
        unsigned char *hl = &CURRENT_BUFFER->row[filerow].hl[CURRENT_BUFFER->coloff];

        attron(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));
        mvprintw(y, 0, "%4d ", filerow + 1);
        attroff(A_DIM | COLOR_PAIR(editorSyntaxToColor(HL_GUTTER)));

        for (int j = 0; j < len; j++) {
          if (is_char_in_selection(filerow, CURRENT_BUFFER->coloff + j)) {
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
                     CURRENT_BUFFER->filename ? CURRENT_BUFFER->filename : "[No name]", CURRENT_BUFFER->numrows,
                     CURRENT_BUFFER->dirty ? "(modified)" : " ");
  int rlen = snprintf(rstatus, sizeof(rstatus), " %s | %d/%d | [%d/%d]",
                      CURRENT_BUFFER->syntax ? CURRENT_BUFFER->syntax->filetype : "no ft", CURRENT_BUFFER->cy + 1, CURRENT_BUFFER->numrows,
                      E.current_buffer + 1, E.num_buffers);

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
    for (int i = 0; i < CURRENT_BUFFER->cy; i++) {
      display_y += (CURRENT_BUFFER->row[i].rsize / (E.screencols - 5)) + 1;
    }
    display_y += CURRENT_BUFFER->rx / (E.screencols - 5);
    final_cy = display_y - CURRENT_BUFFER->rowoff;
    final_cx = (CURRENT_BUFFER->rx % (E.screencols - 5)) + 5;
  } else {
    final_cy = CURRENT_BUFFER->cy - CURRENT_BUFFER->rowoff;
    final_cx = CURRENT_BUFFER->rx - CURRENT_BUFFER->coloff + 5;
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
  erow *row = (CURRENT_BUFFER->cy >= CURRENT_BUFFER->numrows) ? NULL : &CURRENT_BUFFER->row[CURRENT_BUFFER->cy];

  switch(key) {
    case ARROW_LEFT:
      if (CURRENT_BUFFER->cx != 0) {
        CURRENT_BUFFER->cx--;
      } else if (CURRENT_BUFFER->cy > 0) {
        CURRENT_BUFFER->cy--;
        CURRENT_BUFFER->cx = CURRENT_BUFFER->row[CURRENT_BUFFER->cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && CURRENT_BUFFER->cx < row -> size) {
        CURRENT_BUFFER->cx++;
      } else if (row && CURRENT_BUFFER->cx == row -> size) {
        CURRENT_BUFFER->cy++;
        CURRENT_BUFFER->cx = 0;
      }
      break;
    case ARROW_UP:
      if(CURRENT_BUFFER->cy != 0) {
        CURRENT_BUFFER->cy--;
      }
      break;
    case ARROW_DOWN:
      if (CURRENT_BUFFER->cy < CURRENT_BUFFER->numrows) {
        CURRENT_BUFFER->cy++;
      }
      break;
  }

  row = (CURRENT_BUFFER->cy >= CURRENT_BUFFER->numrows) ? NULL : &CURRENT_BUFFER->row[CURRENT_BUFFER->cy];
  int rowlen = row ? row -> size : 0;
  if (CURRENT_BUFFER->cx > rowlen) {
    CURRENT_BUFFER->cx = rowlen;
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
    case KEY_RESIZE: break;
    case '\n':
      editorInsertNewline();
      break;

    case '\t':
      if (CURRENT_BUFFER->soft_tabs) {
        for (int i = 0; i < CURRENT_BUFFER->tab_stop; i++) {
          editorInsertChar(' ');
        }
      } else {
        editorInsertChar('\t');
      }
      break;

    case CTRL_KEY(' '): // Ctrl+Space
      if (CURRENT_BUFFER->selection_active) {
        CURRENT_BUFFER->selection_active = 0;
        editorSetStatusMessage("Selection cancelled.");
      } else {
        CURRENT_BUFFER->selection_active = 1;
        CURRENT_BUFFER->mark_cx = CURRENT_BUFFER->cx;
        CURRENT_BUFFER->mark_cy = CURRENT_BUFFER->cy;
        editorSetStatusMessage("Selection mark set. Move cursor to select. Ctrl+Space to cancel.");
      }
      break;

    case CTRL_KEY('k'):
      editorCopy();
      CURRENT_BUFFER->selection_active = 0;
      break;

    case CTRL_KEY('v'):
      editorPaste();
      break;

    case CTRL_KEY('x'):
      editorCut();
      break;

    case CTRL_KEY('u'):
      editorUndo();
      break;

    case CTRL_KEY('r'):
      editorRedo();
      break;

    case CTRL_KEY('q'):
      editorCloseBuffer();
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    case CTRL_KEY('n'):
      editorNewBuffer();
      break;

    case CTRL_KEY('b'):
      editorSwitchBuffer();
      break;

    case CTRL_KEY('l'):
      editorShowBufferList();
      break;

    case HOME_KEY:
      CURRENT_BUFFER->cx = 0;
      break;

    case END_KEY:
      if (CURRENT_BUFFER->cy < CURRENT_BUFFER->numrows)
        CURRENT_BUFFER->cx = CURRENT_BUFFER->row[CURRENT_BUFFER->cy].size;
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
          CURRENT_BUFFER->cy = CURRENT_BUFFER->rowoff;
        } else if (c == PAGE_DOWN) {
          CURRENT_BUFFER->cy = CURRENT_BUFFER->rowoff + E.screenrows - 1;
          if (CURRENT_BUFFER->cy > CURRENT_BUFFER->numrows) CURRENT_BUFFER->cy = CURRENT_BUFFER->numrows;
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

    case '\x1b':
      break;

    default:
      editorInsertChar(c);
      break;
  }
}

/*** init ***/

void initBuffer(struct Buffer *b) {
  b->cx = 0;
  b->cy = 0;
  b->rx = 0;
  b->rowoff = 0;
  b->coloff = 0;
  b->numrows = 0;
  b->row = NULL;
  b->dirty = 0;
  b->filename = NULL;
  b->syntax = NULL;

  b->mark_cx = 0;
  b->mark_cy = 0;
  b->selection_active = 0;
  b->clipboard = NULL;
  b->undo_stack = NULL;
  b->undo_pos = 0;
  b->undo_len = 0;
  b->redo_stack = NULL;
  b->redo_pos = 0;
  b->redo_len = 0;

  b->soft_tabs = 0;
  b->tab_stop = 8;
}

void initEditor() {
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.quit_times = 3;
  E.soft_wrap = 0;
  E.hard_wrap = 0;

  E.buffers = malloc(sizeof(struct Buffer *));
  E.buffers[0] = malloc(sizeof(struct Buffer));
  initBuffer(E.buffers[0]);

  E.num_buffers = 1;
  E.current_buffer = 0;

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
    // If a filename is provided, open it in a new buffer
    editorNewBuffer(); // Create a new buffer
    editorOpen(argv[1]); // Open the file in the new buffer
  }

  editorSetStatusMessage(
    "C-s:save | C-q:quit | C-f:find | C-spc:select | C-x:cut | C-k:copy | C-v:paste | C-n:new | C-b:next | C-l:list");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
