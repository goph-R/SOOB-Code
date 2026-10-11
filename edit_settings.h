#ifndef EDIT_SETTINGS_H
#define EDIT_SETTINGS_H

/*
 * edit_settings.h -- the code editor's settings, kept in codeedit.ini next to
 * the exe: plain key=value lines (no registry, works on Win98, and the editor
 * folder can be copied with its settings). Unknown keys are ignored and
 * missing ones keep their defaults, so the file can be hand-edited.
 *
 *   font_size=14         code text, px at 100% display scale
 *   tab_width=4          indent width; also the display width of a tab char
 *   use_tabs=0           Tab / auto-indent insert tab characters, not spaces
 *   line_numbers=1       show the line-number gutter
 *   wrap_column=0        column used when Word Wrap is switched on for a file
 *                        that has no column of its own (0 = window width)
 *   trim_trailing=1      strip trailing spaces / tabs from every line on
 *                        save (Markdown keeps a two-space hard line break)
 *   remember_window=1    restore the window size and position
 *   window=x,y,w,h       last window rectangle (real pixels)
 *   last_dir=...         folder the Open / Save As dialog last used -- ONE
 *                        entry shared by both, so the two always agree
 *   recent1=...          recent files, most recent first
 *   folders_show=1       the Folders sidebar is visible
 *   folders_width=200    its width in real pixels
 *   folder1=...          folders listed in the sidebar, in the order added
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define CODE_MAX_RECENT  8
#define CODE_MAX_FOLDERS 8

typedef struct CodeSettings {
    int  fontSize;
    int  tabWidth;
    int  useTabs;
    int  lineNumbers;
    int  wrapCol;
    int  trimTrailing;
    int  rememberWin;
    int  winX, winY, winW, winH;          /* winW == 0: never saved */
    char lastDir[512];                    /* empty => never used a dialog */
    int  nRecent;
    char recent[CODE_MAX_RECENT][512];
    int  foldersShow;                     /* the Folders sidebar is visible */
    int  foldersW;                        /* its width, real pixels; 0 = default */
    int  nFolders;
    char folders[CODE_MAX_FOLDERS][512];
} CodeSettings;

static int codeClampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void codeSettingsDefaults(CodeSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->fontSize    = 14;
    s->tabWidth    = 4;
    s->useTabs     = 0;
    s->lineNumbers = 1;
    s->wrapCol     = 0;
    s->trimTrailing = 1;                  /* on by default, as asked */
    s->rememberWin = 1;
    s->foldersShow = 1;                   /* visible until it is turned off */
    s->foldersW    = 0;                   /* 0 => the built-in default width */
}

static void codeSettingsPath(char *buf, int len)
{
#ifdef _WIN32
    char *slash;
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)len);
    if (n > 0 && n < (DWORD)len && (slash = strrchr(buf, '\\')) != NULL &&
        (int)(slash - buf) + 14 < len) {
        strcpy(slash + 1, "codeedit.ini");
        return;
    }
#endif
    strncpy(buf, "codeedit.ini", (size_t)len - 1);
    buf[len - 1] = '\0';
}

static void codeSettingsLoad(CodeSettings *s)
{
    char path[600], line[700];
    FILE *f;
    codeSettingsDefaults(s);
    codeSettingsPath(path, (int)sizeof(path));
    f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, (int)sizeof(line), f)) {
        char *eq = strchr(line, '='), *v, *e;
        if (!eq || line[0] == ';' || line[0] == '#') continue;
        *eq = '\0';
        v = eq + 1;
        e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';

        if      (!strcmp(line, "font_size"))       s->fontSize    = codeClampInt(atoi(v), 6, 72);
        else if (!strcmp(line, "tab_width"))       s->tabWidth    = codeClampInt(atoi(v), 1, 16);
        else if (!strcmp(line, "use_tabs"))        s->useTabs     = atoi(v) != 0;
        else if (!strcmp(line, "line_numbers"))    s->lineNumbers = atoi(v) != 0;
        else if (!strcmp(line, "wrap_column"))     s->wrapCol     = codeClampInt(atoi(v), 0, 1000);
        else if (!strcmp(line, "trim_trailing"))   s->trimTrailing = atoi(v) != 0;
        else if (!strcmp(line, "remember_window")) s->rememberWin = atoi(v) != 0;
        else if (!strcmp(line, "window"))
            sscanf(v, "%d,%d,%d,%d", &s->winX, &s->winY, &s->winW, &s->winH);
        else if (!strcmp(line, "last_dir")) {
            strncpy(s->lastDir, v, sizeof(s->lastDir) - 1);
            s->lastDir[sizeof(s->lastDir) - 1] = '\0';
        }
        /* Both scalars MUST be tested before the "folder" prefix below, or
           that prefix match swallows them as folder paths -- the same shape
           of trap the "recent" prefix has always had. */
        else if (!strcmp(line, "folders_show"))  s->foldersShow = atoi(v) != 0;
        else if (!strcmp(line, "folders_width")) s->foldersW = codeClampInt(atoi(v), 0, 4000);
        else if (!strncmp(line, "folder", 6) && *v && s->nFolders < CODE_MAX_FOLDERS) {
            strncpy(s->folders[s->nFolders], v, sizeof(s->folders[0]) - 1);
            s->folders[s->nFolders][sizeof(s->folders[0]) - 1] = '\0';
            s->nFolders++;
        }
        else if (!strncmp(line, "recent", 6) && *v && s->nRecent < CODE_MAX_RECENT) {
            strncpy(s->recent[s->nRecent], v, sizeof(s->recent[0]) - 1);
            s->recent[s->nRecent][sizeof(s->recent[0]) - 1] = '\0';
            s->nRecent++;
        }
    }
    fclose(f);
}

static void codeSettingsSave(const CodeSettings *s)
{
    char path[600];
    FILE *f;
    int i;
    codeSettingsPath(path, (int)sizeof(path));
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "; SOOB Code Editor settings\n");
    fprintf(f, "font_size=%d\n", s->fontSize);
    fprintf(f, "tab_width=%d\n", s->tabWidth);
    fprintf(f, "use_tabs=%d\n", s->useTabs);
    fprintf(f, "line_numbers=%d\n", s->lineNumbers);
    fprintf(f, "wrap_column=%d\n", s->wrapCol);
    fprintf(f, "trim_trailing=%d\n", s->trimTrailing);
    fprintf(f, "remember_window=%d\n", s->rememberWin);
    if (s->winW > 0)
        fprintf(f, "window=%d,%d,%d,%d\n", s->winX, s->winY, s->winW, s->winH);
    if (s->lastDir[0])
        fprintf(f, "last_dir=%s\n", s->lastDir);
    for (i = 0; i < s->nRecent; i++)
        fprintf(f, "recent%d=%s\n", i + 1, s->recent[i]);
    fprintf(f, "folders_show=%d\n", s->foldersShow);
    if (s->foldersW > 0)
        fprintf(f, "folders_width=%d\n", s->foldersW);
    for (i = 0; i < s->nFolders; i++)
        fprintf(f, "folder%d=%s\n", i + 1, s->folders[i]);
    fclose(f);
}

static int codePathEq(const char *a, const char *b)
{
#ifdef _WIN32
    return _stricmp(a, b) == 0;          /* Windows paths are case-insensitive */
#else
    return strcmp(a, b) == 0;
#endif
}

/* Move (or add) path to the top of the recent list. */
static void codeRecentAdd(CodeSettings *s, const char *path)
{
    int i, j;
    if (!path || !*path) return;
    for (i = 0; i < s->nRecent; i++) if (codePathEq(s->recent[i], path)) break;
    if (i == s->nRecent) i = (s->nRecent < CODE_MAX_RECENT) ? s->nRecent++ : CODE_MAX_RECENT - 1;
    for (j = i; j > 0; j--) strcpy(s->recent[j], s->recent[j - 1]);
    strncpy(s->recent[0], path, sizeof(s->recent[0]) - 1);
    s->recent[0][sizeof(s->recent[0]) - 1] = '\0';
}

/* Remember the folder `path` lives in, as the starting point for the next
 * Open / Save As. A bare filename has no folder in it and leaves the stored
 * one alone -- better to keep where the user last browsed than to replace it
 * with nothing. */
/* Add a folder to the sidebar list. Unlike the recent list this APPENDS and
   keeps the order the user added them in -- these are a workspace, not a
   history, and having them shuffle would be maddening. Already listed, or
   full, is a no-op; returns 1 if the list changed. */
static int codeFolderAdd(CodeSettings *s, const char *path)
{
    int i;
    if (!path || !*path || s->nFolders >= CODE_MAX_FOLDERS) return 0;
    for (i = 0; i < s->nFolders; i++)
        if (codePathEq(s->folders[i], path)) return 0;
    strncpy(s->folders[s->nFolders], path, sizeof(s->folders[0]) - 1);
    s->folders[s->nFolders][sizeof(s->folders[0]) - 1] = '\0';
    s->nFolders++;
    return 1;
}

static int codeFolderRemove(CodeSettings *s, const char *path)
{
    int i, k;
    for (i = 0; i < s->nFolders; i++) {
        if (!codePathEq(s->folders[i], path)) continue;
        for (k = i; k < s->nFolders - 1; k++)
            strcpy(s->folders[k], s->folders[k + 1]);
        s->nFolders--;
        s->folders[s->nFolders][0] = '\0';
        return 1;
    }
    return 0;
}

/* codeedit.ini in the exe's folder (the current folder off Windows). */

static void codeLastDirFromPath(CodeSettings *s, const char *path)
{
    const char *fwd, *back, *sl;
    int n;
    if (!path || !*path) return;
    fwd  = strrchr(path, '/');
    back = strrchr(path, '\\');
    sl   = fwd > back ? fwd : back;
    if (!sl) return;
    n = (int)(sl - path);
    /* Keep the separator when dropping it would change the meaning: "\" is
     * the root, and a bare "C:" is "the current directory on drive C:" to
     * comdlg32 -- NOT C:\ -- which would send the dialog somewhere else
     * entirely. Everywhere else the trailing separator is left off. */
    if (n == 0) n = 1;                              /* "\file"  -> "\"   */
    else if (n == 2 && path[1] == ':') n = 3;       /* "C:\file" -> "C:\" */
    if (n > (int)sizeof(s->lastDir) - 1) return;
    memcpy(s->lastDir, path, (size_t)n);
    s->lastDir[n] = '\0';
}

static void codeRecentRemove(CodeSettings *s, int i)
{
    int j;
    if (i < 0 || i >= s->nRecent) return;
    for (j = i; j < s->nRecent - 1; j++) strcpy(s->recent[j], s->recent[j + 1]);
    s->nRecent--;
}

#endif /* EDIT_SETTINGS_H */
