#ifndef CONFIG_H
#define CONFIG_H

// We need this for the erow struct
#include <sys/types.h>

typedef struct erow {
  int idx;
  int size;
  int rsize;
  char *chars;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
} erow;

enum {
  COLOR_PAIR_NORMAL = 1,
  COLOR_PAIR_COMMENT,
  COLOR_PAIR_KEYWORD1,
  COLOR_PAIR_KEYWORD2,
  COLOR_PAIR_STRING,
  COLOR_PAIR_NUMBER,
  COLOR_PAIR_MATCH,
  COLOR_PAIR_GUTTER
};

struct Buffer {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  struct editorSyntax *syntax;
  int tab_stop;
  int soft_tabs;
  int mark_cx, mark_cy;
  int selection_active;
  char *clipboard;
  struct editorAction *undo_stack;
  int undo_pos;
  int undo_len;
  struct editorAction *redo_stack;
  int redo_pos;
  int redo_len;
};

struct editorConfig {
  int screenrows;
  int screencols;
  char statusmsg[80];
  time_t statusmsg_time;
  int quit_times;
  int soft_wrap;
  int hard_wrap;

  struct Buffer **buffers;
  int num_buffers;
  int current_buffer;
};

enum editorActionType {
  ACTION_INSERT,
  ACTION_DELETE
};

typedef struct editorAction {
  enum editorActionType type;
  int cx, cy;
  char *data;
  size_t len;
} editorAction;

extern struct editorConfig E;

void load_config();

#endif
