#include <stddef.h>
#include "syntax.h"

/*** filetype ***/

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",

  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

char *SH_HL_extensions[] = {".sh", ".bash", NULL};
char *SH_HL_keywords[] = {
  /* Core Keywords */
  "if", "then", "else", "elif", "fi", "case", "esac", "for",
  "select", "while", "until", "do", "done", "in", "function", "time",
  
  /* Common Built-in Commands & Operators (secondary highlight) */
  "echo|", "read|", "let|", "export|", "unset|", "readonly|", "declare|",
  "local|", "exit|", "return|", "true|", "false|", "set|", "shift|",
  "source|", "trap|", "test|", "type|", "ulimit|", "umask|", "alias|",
  "unalias|", "cd|", "pwd|", "history|", "kill|", "jobs|", "bg|", "fg|",
  "getopts|", "printf|",
  NULL
};

char *PY_HL_extensions[] = {".py", NULL};
char *PY_HL_keywords[] = {
  "and", "as", "assert", "break", "class", "continue", "def", "del",
  "elif", "else", "except", "finally", "for", "from", "global", "if",
  "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass",
  "raise", "return", "try", "while", "with", "yield", "async", "await",

  "True|", "False|", "None|", "str|", "int|", "float|", "list|", "tuple|", "dict|",
  NULL
};

char *JS_HL_extensions[] = {".js", NULL};
char *JS_HL_keywords[] = {
  /* Standard Keywords */
  "break", "case", "catch", "class", "const", "continue", "debugger",
  "default", "delete", "do", "else", "export", "extends", "finally",
  "for", "function", "if", "import", "in", "instanceof", "new",
  "return", "super", "switch", "this", "throw", "try", "typeof", "var",
  "void", "while", "with", "yield",

  /* Contextual/Future-Proof Keywords with secondary highlighting */
  "let|", "static|", "enum|", "await|", "implements|", "package|",
  "protected|", "interface|", "private|", "public|",

  /* Literals with secondary highlighting */
  "true|", "false|", "null|",
  NULL
};

struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
  {
    "sh",
    SH_HL_extensions,
    SH_HL_keywords,
    "#", "", "",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
  {
    "python",
    PY_HL_extensions,
    PY_HL_keywords,
    "#", NULL, NULL,
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
  {
    "javascript",
    JS_HL_extensions,
    JS_HL_keywords,
    "//", "/*", "*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
  { NULL, NULL, NULL, NULL, NULL, NULL, 0 } // Terminator
};
