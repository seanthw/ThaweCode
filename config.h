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

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct editorSyntax *syntax;
  // --- ADDED CONFIG FIELDS ---
  int tab_stop;
  int quit_times;
};

extern struct editorConfig E;

void load_config();

#endif // CONFIG_H