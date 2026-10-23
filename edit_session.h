#ifndef EDIT_SESSION_H
#define EDIT_SESSION_H

/*
 * edit_session.h -- what was open last time, and the TEXT of whatever was not
 * saved. The store behind "hot exit": closing the window asks nothing, and the
 * next start puts every tab back where it was, unsaved edits and all.
 *
 * WHY THIS IS NOT IN codeedit.ini
 *
 * Settings are what the user chose; a session is what the editor happened to
 * be doing. They have different lifetimes and different owners -- copy
 * codeedit.ini to another machine and you want your tab width, not somebody
 * else's open files -- so they live in different files.
 *
 * WHAT IS STORED
 *
 * WHERE IT LIVES
 *
 * Next to codeedit.ini, wherever codeDataDir() put that -- beside the exe for
 * a portable or Win98 install, %APPDATA%\SOOB-Code on XP and newer. The two
 * must never separate: a session that outlived its settings, or sat in a
 * VirtualStore shadow of Program Files, would be worse than no session.
 *
 *   index.ini   one record per tab: path, caret, first visible line, the
 *               encoding and line ending it will be saved with, the language
 *               if it was picked by hand, and the name of its stash file if
 *               the buffer was unsaved. Then the directories the Folders tree
 *               had open, parents first.
 *   a01.txt ..  the stash files: the raw buffer, exactly as it sits in
 *   b01.txt     memory (UTF-8, LF), for every tab that was modified or had no
 *               file at all.
 *
 * THE TWO GENERATIONS ARE THE POINT
 *
 * An unsaved buffer that only exists in the stash is the user's only copy, so
 * the write must never be able to destroy the previous one. Stash files are
 * therefore written under the generation the index does NOT currently name
 * ('a' or 'b'), the index is written second, and only then is the old
 * generation deleted. Lose power anywhere in that sequence and the index on
 * disk still names a complete set of files: either the old one, or the new.
 *
 * Deliberately NOT a full history. A session is replaced wholesale on exit;
 * it is not a backup, and it does not protect against a crash BEFORE exit --
 * a save that happens never happened.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <FL/fl_utf8.h>

#include "edit_settings.h"            /* codeSettingsPath: where we live */
#include "fltk_ui/edit_fileio.h"      /* codeFileJoin: the right separator */

#define CODE_SES_MAXTABS 16     /* must be >= MAX_DOCS in codeedit.cpp */
#define CODE_SES_MAXOPEN 32     /* expanded folders remembered */

typedef struct CodeSesTab {
    char path[512];             /* empty: a buffer that never had a file */
    char stash[16];             /* its unsaved text; empty: it was saved */
    int  caret;
    int  top;                   /* first visible line, 1-based */
    int  enc;                   /* CODE_ENC_*  */
    int  crlf;
    int  lang;                  /* LEX_LANG_*, so a hand-picked one survives */
} CodeSesTab;

typedef struct CodeSession {
    CodeSesTab tab[CODE_SES_MAXTABS];
    int  nTabs;
    int  active;                /* index into tab[], or -1 */
    char gen;                   /* 'a' or 'b': the live stash generation */
    char open[CODE_SES_MAXOPEN][512];
    int  nOpen;
} CodeSession;

/* <the folder codeedit.ini is in>\session -- codeDataDir() decides which
   folder that is, so the settings and the session can never end up in
   different places. It ends with a separator already, or is empty for the
   working directory. */
static void codeSesDir(char *buf, int len)
{
    const char *dir = codeDataDir();
    buf[0] = '\0';
    if ((int)strlen(dir) + 8 >= len) return;
    sprintf(buf, "%ssession", dir);
}

static void codeSesFile(char *buf, int len, const char *name)
{
    char dir[600];
    codeSesDir(dir, (int)sizeof(dir));
    codeFileJoin(buf, len, dir, name);
}

static void codeSesInit(CodeSession *s)
{
    memset(s, 0, sizeof(*s));
    s->active = -1;
    s->gen    = 'a';
}

/* The generation to write next: never the one the index names now. */
static char codeSesNextGen(char cur) { return cur == 'a' ? 'b' : 'a'; }

/* 8.3 all the way -- a01.txt -- because Win98 is the floor here. The index
   bound is not decoration: it is what keeps the name two digits, and what
   lets the compiler see that it fits. An out-of-range index yields an empty
   name, which every caller then fails on rather than writing somewhere
   unexpected. */
static void codeSesStashName(char *buf, int len, char gen, int idx)
{
    if (!buf || len < 9) { if (buf && len > 0) buf[0] = '\0'; return; }
    if (idx < 0 || idx >= CODE_SES_MAXTABS) { buf[0] = '\0'; return; }
    sprintf(buf, "%c%02d.txt", gen, idx + 1);
}

/* The buffer, byte for byte -- UTF-8 with LF ends, the form it has in memory.
 * NOT codeWriteText(): re-encoding the user's only copy of an unsaved file to
 * ANSI on the way out would be a lossy round trip for the sake of nothing.
 * The encoding it will be SAVED with travels in the index instead. */
static int codeSesWriteStash(char gen, int idx, const char *text,
                             char *nameOut, int nameLen)
{
    char name[16], path[600];
    FILE *f;
    size_t n;

    if (!text) return 0;
    codeSesStashName(name, (int)sizeof(name), gen, idx);
    codeSesFile(path, (int)sizeof(path), name);
    f = fl_fopen(path, "wb");
    if (!f) return 0;
    n = strlen(text);
    if (n && fwrite(text, 1, n, f) != n) { fclose(f); remove(path); return 0; }
    if (fclose(f) != 0) { remove(path); return 0; }
    if (nameOut && nameLen > (int)strlen(name)) strcpy(nameOut, name);
    return 1;
}

static char *codeSesReadStash(const char *name)
{
    char path[600];
    FILE *f;
    long n;
    char *t;

    if (!name || !*name) return 0;
    codeSesFile(path, (int)sizeof(path), name);
    f = fl_fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return 0; }
    t = (char *)malloc((size_t)n + 1);
    if (!t) { fclose(f); return 0; }
    if (n && fread(t, 1, (size_t)n, f) != (size_t)n) { free(t); fclose(f); return 0; }
    fclose(f);
    t[n] = '\0';
    return t;
}

/* Every stash file of one generation. Called only once the OTHER generation
   is on disk and the index names it. */
static void codeSesDropGen(char gen)
{
    char name[16], path[600];
    int k;
    for (k = 0; k < CODE_SES_MAXTABS; k++) {
        codeSesStashName(name, (int)sizeof(name), gen, k);
        codeSesFile(path, (int)sizeof(path), name);
        remove(path);
    }
}

static int codeSesLoad(CodeSession *s)
{
    char path[600], line[700];
    CodeSesTab *t = 0;
    FILE *f;

    codeSesInit(s);
    codeSesFile(path, (int)sizeof(path), "index.ini");
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, (int)sizeof(line), f)) {
        char *eq = strchr(line, '='), *v, *e;
        if (!eq || line[0] == ';' || line[0] == '#') continue;
        *eq = '\0';
        v = eq + 1;
        e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';

        if (!strcmp(line, "gen")) { s->gen = (*v == 'b') ? 'b' : 'a'; }
        else if (!strcmp(line, "active")) s->active = atoi(v);
        else if (!strcmp(line, "tab")) {
            /* Starts a record; everything after it fills that record in. */
            if (s->nTabs >= CODE_SES_MAXTABS) { t = 0; continue; }
            t = &s->tab[s->nTabs++];
            memset(t, 0, sizeof(*t));
            t->top = 1;
            t->lang = -1;
            strncpy(t->path, v, sizeof(t->path) - 1);   /* memset above */
        }
        else if (!strcmp(line, "open")) {
            if (s->nOpen < CODE_SES_MAXOPEN)
                strncpy(s->open[s->nOpen++], v, 511);
        }
        else if (!t) continue;
        else if (!strcmp(line, "stash")) strncpy(t->stash, v, sizeof(t->stash) - 1);
        else if (!strcmp(line, "caret")) t->caret = atoi(v);
        else if (!strcmp(line, "top"))   t->top   = atoi(v);
        else if (!strcmp(line, "enc"))   t->enc   = atoi(v);
        else if (!strcmp(line, "crlf"))  t->crlf  = atoi(v);
        else if (!strcmp(line, "lang"))  t->lang  = atoi(v);
    }
    fclose(f);
    if (s->active >= s->nTabs) s->active = s->nTabs - 1;
    return s->nTabs > 0 || s->nOpen > 0;
}

/* The index, written AFTER the stash files it names. Returns 0 if it could
   not be written, which the caller must treat as "the session is not safe" --
   codeedit.cpp falls back to asking about unsaved files the old way. */
static int codeSesSave(const CodeSession *s)
{
    char dir[600], path[600];
    FILE *f;
    int k;

    codeSesDir(dir, (int)sizeof(dir));
    if (dir[0]) fl_mkdir(dir, 0700);        /* already there: fails, fine */
    codeSesFile(path, (int)sizeof(path), "index.ini");
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "; SOOB Code Editor session -- written on exit, read on start\n");
    fprintf(f, "gen=%c\n", s->gen);
    fprintf(f, "active=%d\n", s->active);
    for (k = 0; k < s->nTabs; k++) {
        const CodeSesTab *t = &s->tab[k];
        fprintf(f, "tab=%s\n", t->path);
        if (t->stash[0]) fprintf(f, "stash=%s\n", t->stash);
        fprintf(f, "caret=%d\n", t->caret);
        fprintf(f, "top=%d\n", t->top);
        fprintf(f, "enc=%d\n", t->enc);
        fprintf(f, "crlf=%d\n", t->crlf);
        fprintf(f, "lang=%d\n", t->lang);
    }
    for (k = 0; k < s->nOpen; k++) fprintf(f, "open=%s\n", s->open[k]);
    if (fclose(f) != 0) return 0;
    return 1;
}

/* Forget everything: both generations and the index. For switching the
   feature off, so turning it back on later cannot resurrect a stale desk. */
static void codeSesClear(void)
{
    char path[600];
    codeSesDropGen('a');
    codeSesDropGen('b');
    codeSesFile(path, (int)sizeof(path), "index.ini");
    remove(path);
}

#endif /* EDIT_SESSION_H */
