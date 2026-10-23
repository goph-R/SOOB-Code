/*
 * codeedit.cpp -- the SOOB code editor: tabs, File and Edit menus, dark theme.
 *
 * See docs/code-editor-plan.md. Built by e98.bat (Win98) / e10.bat (Win10).
 *
 *   e98
 *   codeedit                      (one empty untitled tab)
 *   codeedit scripts\main.lua ... (opens each file in its own tab)
 *
 * NOTES FOR WIN98
 *
 * Fl_Double_Window, not Fl_Window. FLTK's plain window is single-buffered, so
 * every partial repaint lands straight on the screen and flickers visibly on
 * period hardware. Double buffering blits only the damaged region, so it costs
 * little and removes the flicker.
 *
 * File dialogs come from edit_filedlg.h, which calls the ANSI comdlg32 entry
 * points directly -- FLTK's Fl_Native_File_Chooser is wide-API only and dead on
 * 9x, and its own drawn chooser does not look like Windows. See
 * SOOB-Core/docs/editor-fltk-win98.md.
 *
 * Fl_Text_Display derives from Fl_Group, so a CodeEditor is a DIRECT child of
 * Fl_Tabs -- no wrapper group is needed, and the child's label is the tab
 * caption. It must be copy_label(), never label(): Fl_Widget stores the pointer
 * without copying, and our captions are built at runtime.
 */
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Tabs.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Hold_Browser.H>
#ifdef _WIN32
#include <FL/x.H>                  /* fl_xid(), for the file dialogs' owner */
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "fltk_ui/edit_dpi.h"
#include "fltk_ui/edit_menupad.h"
#include "fltk_ui/edit_tabs.h"
#include "fltk_ui/edit_code.h"
#include "fltk_ui/edit_filedlg.h"
#include "fltk_ui/edit_split.h"
#include "fltk_ui/edit_shell.h"
#include "fltk_ui/edit_folders.h"
#include "fltk_ui/edit_grep.h"
#include "fltk_ui/edit_find.h"
#include "fltk_ui/edit_json.h"
#include "fltk_ui/edit_place.h"
#include "edit_settings.h"
#include "edit_session.h"

/* major.minor.patch -- bump the minor on a feature change, the third component
 * on a bug fix (and drop it again at the next feature). The __DATE__/__TIME__
 * stamp beside it is the one that cannot lie: the compiler writes it, so a
 * title showing an old timestamp means the running exe is not the one you just
 * built. */
#define CODEEDIT_VERSION "1.21"

#define MAX_DOCS   16
#define MENU_H     25
#define TABROW_H   30         /* tab row incl. the strip above the tabs */
#define STATUS_H   22         /* status bar */
#define FOLD_W_DEF 200        /* Folders sidebar, width when the ini has none */
#define FOLD_W_MIN 110        /* ...and the narrowest the splitter may make it */
#define TABS_W_MIN 240        /* never squeeze the tab row below this */
#define SPLIT_W     5         /* the drag bar between them */
/* Open as Text asks first above this: Fl_Text_Buffer lays every line out up
   front, which on the PII this still has to run on is where the wait is. */
#define FOLD_TEXT_WARN (1024L * 1024L)

typedef struct CodeDoc {
    CodeEditor *ed;
    char path[512];            /* empty => untitled */
    char label[160];           /* what is currently on the tab */
    int  shownDirty;           /* so labels are only rebuilt when they change */
    int  wrapColSet;           /* Wrap at Column... used: ignore the default */
    int  noSaveBack;           /* opened as text though it holds a NUL byte:
                                  the buffer is a TRUNCATION of the file, so
                                  Save must not write it back over it */
} CodeDoc;

static CodeDoc          gDocs[MAX_DOCS];
static int              gDocCount = 0;
static Fl_Double_Window *gWin;
static CodeTabs         *gTabs;
static CodeFolders      *gTree;         /* the Folders sidebar, 0 until built */
static CodeSplitter     *gSplit;
static int               gSideW   = 0;  /* sidebar width, REAL pixels */
static int               gSideShow = 1;
static int              gUntitledSeq = 1;
static Fl_Double_Window *gFindWin;      /* defined with the Find UI below */
static Fl_Double_Window *gGrepWin;      /* ...and with Find in Folder */
static CodeSettings     gSet;           /* codeedit.ini, see edit_settings.h */
static void recentNote(const char *path);
static void findRetireStaleMsg(void);   /* defined with the Find UI below */
/* Reopening what was already open is not "you opened this": a restore must
   not reorder Recent Files, and 16 restored tabs would otherwise rewrite the
   ini 16 times before the window is even up. */
static int  gRestoring = 0;
static void revealInFolders(const char *path);  /* ...and with the sidebar */
static void applySettings(void);        /* ...and with the Settings dialog */

static void msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* ---- helpers ----------------------------------------------------------- */

/* strncpy that always terminates, so call sites do not have to remember. */
static void copyStr(char *dst, int dstlen, const char *src)
{
    int n;
    if (!dst || dstlen <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = (int)strlen(src);
    if (n > dstlen - 1) n = dstlen - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

/* A modal integer prompt with the default value PRESELECTED, so typing
 * replaces it instead of appending to it. That is the whole reason it exists:
 * fl_input() hands back only the string, never its Fl_Input, so there is no
 * way to select its contents -- and its window is not parented to ours, which
 * is half of why the dialogs felt loose. Used by Go to line and Wrap at
 * Column. Returns 1 and writes *out, or 0 if cancelled.
 *
 * Same Fl::readqueue() loop as cbSettings(): widgets with no callback of their
 * own are queued when activated, so OK / Cancel / Enter need no callbacks. */
static int codeAskInt(const char *title, const char *prompt, int def, int *out)
{
    char buf[32];
    Fl_Double_Window *w = new Fl_Double_Window(340, 104, title);
    Fl_Int_Input *in;
    Fl_Return_Button *ok;
    Fl_Button *cancel;
    Fl_Box *lab;
    int got = 0;

    w->begin();
    lab = new Fl_Box(10, 8, 320, 20, prompt);
    lab->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    in  = new Fl_Int_Input(10, 32, 320, 24);
    in->when(FL_WHEN_ENTER_KEY_ALWAYS);        /* Enter in the field = OK */
    ok     = new Fl_Return_Button(150, 68, 86, 26, "OK");
    cancel = new Fl_Button(244, 68, 86, 26, "Cancel");
    w->end();
    editDpiScaleTree(w);

    sprintf(buf, "%d", def);
    in->value(buf);
    w->set_modal();
    editPlaceDialog(w);
    w->show();
    /* After show(): take_focus() sends FL_FOCUS, and the selection has to be
     * set after that, not before, or the focus handler would drop it. */
    in->take_focus();
    in->position(0, (int)strlen(buf));

    while (w->shown()) {
        Fl_Widget *o = Fl::readqueue();
        if (!o) { Fl::wait(); continue; }
        if (o == cancel) break;
        if (o == ok || o == in) {
            *out = atoi(in->value());
            got = 1;
            break;
        }
    }
    w->hide();
    Fl::delete_widget(w);
    return got;
}

static const char *baseName(const char *p)
{
    const char *a = strrchr(p, '/');
    const char *b = strrchr(p, '\\');
    const char *s = a > b ? a : b;
    return s ? s + 1 : p;
}

/* Where the Open / Save As dialog should start.
 *
 * The folder the last dialog actually used wins, and Open and Save As share
 * the one entry -- so the pair always agrees, and it survives a restart
 * (last_dir in codeedit.ini). Only when there is no stored folder yet do we
 * fall back to the folder the current document lives in. */
static const char *currentDir(char *buf, int len)
{
    int i, n;
    buf[0] = '\0';
    if (gSet.lastDir[0]) { copyStr(buf, len, gSet.lastDir); return buf; }
    for (i = 0; i < gDocCount; i++) {
        if (gTabs->value() == (Fl_Widget *)gDocs[i].ed && gDocs[i].path[0]) {
            const char *b = baseName(gDocs[i].path);
            n = (int)(b - gDocs[i].path);
            if (n > 0 && n < len) { memcpy(buf, gDocs[i].path, (size_t)n); buf[n] = '\0'; }
            break;
        }
    }
    return buf[0] ? buf : ".";
}

static int docIndexOf(Fl_Widget *w)
{
    int i;
    for (i = 0; i < gDocCount; i++) if ((Fl_Widget *)gDocs[i].ed == w) return i;
    return -1;
}

static int currentIndex(void)
{
    return gTabs ? docIndexOf(gTabs->value()) : -1;
}

static void docArea(int *X, int *Y, int *W, int *H)
{
    *X = gTabs->x();
    *Y = gTabs->y() + editDpi(TABROW_H);
    *W = gTabs->w();
    *H = gTabs->h() - editDpi(TABROW_H);
}

/* Rebuild a tab caption, but only when it would actually change -- copy_label()
 * reallocates and redraws, and this is called from a timer. */
static void refreshLabel(int i)
{
    CodeDoc *d = &gDocs[i];
    char want[160];
    int dirty = d->ed->dirty();

    sprintf(want, "%.120s%s",
            d->path[0] ? baseName(d->path) : "untitled",
            dirty ? " *" : "");
    if (strcmp(want, d->label) == 0) return;

    strcpy(d->label, want);
    d->shownDirty = dirty;
    d->ed->copy_label(d->label);     /* copy_label, not label -- see file header */
    gTabs->redraw();
}

static void syncWrapItem(void);
static void syncLangItems(void);
static void syncEncItems(void);

static void labelTimer(void *)
{
    int i;
    for (i = 0; i < gDocCount; i++) refreshLabel(i);
    syncWrapItem();               /* the Word Wrap check follows the active tab */
    syncLangItems();              /* ...and so does the Language check */
    syncEncItems();               /* ...and the Encoding / line-ending checks */
    Fl::repeat_timeout(0.4, labelTimer);
}

/* ---- documents --------------------------------------------------------- */

static void retireEditor(CodeEditor *ed);

static CodeEditor *addDoc(const char *path)
{
    int X, Y, W, H;
    CodeDoc *d;

    if (gDocCount >= MAX_DOCS) {
        fl_alert("Too many open files (limit %d).", MAX_DOCS);
        return 0;
    }
    d = &gDocs[gDocCount];
    memset(d, 0, sizeof(*d));

    codeTrace(" addDoc: enter path=%s count=%d", path ? path : "(new)", gDocCount);
    docArea(&X, &Y, &W, &H);
    gTabs->begin();
    d->ed = new CodeEditor(X, Y, W, H);
    codeTrace(" addDoc: widget created %p", (void *)d->ed);
    d->ed->labelcolor(CODE_COL_TAB_OFF);      /* used only while NOT selected */
    gTabs->end();

    if (path && *path) {
        if (d->ed->loadFile(path) != 0) {
            fl_alert("Could not open\n%s", path);
            retireEditor(d->ed);      /* same three hazards as closeDoc */
            d->ed = 0;
            return 0;
        }
        copyStr(d->path, (int)sizeof(d->path), path);
        /* Asked of the editor rather than of the path, so this covers every
           way a file gets opened -- the tree, Recent, the command line, a
           grep hit -- not just the one that knew to look. */
        d->noSaveBack = d->ed->truncated();
        recentNote(path);
    } else {
        gUntitledSeq++;
        d->ed->language(LEX_LANG_LUA);
        d->ed->text("");
    }

    gDocCount++;
    codeTrace(" addDoc: content set, count=%d", gDocCount);
    refreshLabel(gDocCount - 1);
    gTabs->value(d->ed);
    codeTrace(" addDoc: value ok");
    gTabs->resizable(gDocs[0].ed);   /* always a live child; see retireEditor */
    d->ed->take_focus();
    codeTrace(" addDoc: EXIT CLEAN");
    return d->ed;
}

/* Save doc i. `forcePrompt` turns Save into Save As. Returns 1 if written. */
static int saveDoc(int i, int forcePrompt)
{
    CodeDoc *d;
    char dirbuf[512];

    if (i < 0 || i >= gDocCount) return 0;
    d = &gDocs[i];

    /* Opened as text over a NUL byte: the buffer stops where codeReadText()
       did, so writing it back would cut the file off there. Save becomes Save
       As, and the flag goes once the path is somewhere else -- that new file
       really is what the buffer holds. */
    if (d->noSaveBack && !forcePrompt) {
        fl_message("%s holds binary data and is only partly loaded.\n"
                   "Pick another name: saving it back would truncate it.",
                   baseName(d->path));
        forcePrompt = 1;
    }

    if (forcePrompt || !d->path[0]) {
        char picked[512];
        const char *dir = currentDir(dirbuf, (int)sizeof(dirbuf));
        const char *suggest = d->path[0] ? baseName(d->path) : "untitled.lua";
        if (!codeFileSaveDialog(picked, (int)sizeof(picked), dir, suggest)) return 0;
        copyStr(d->path, (int)sizeof(d->path), picked);
        d->noSaveBack = 0;
        /* the extension may have changed the language */
        d->ed->language(lexLangFromPath(d->path));
        codeLastDirFromPath(&gSet, d->path);   /* shared with Open; recentNote saves */
        recentNote(d->path);
    }
    /* Trim before the encoding check, so a line that is nothing but
     * whitespace cannot be what makes an ANSI save look lossy. The widget
     * does it as one undo step, so Ctrl+Z brings the whitespace back. */
    if (gSet.trimTrailing) d->ed->trimTrailingBlanks();
    /* ANSI can only hold the characters of the system code page */
    if (d->ed->encoding() == CODE_ENC_ANSI && d->ed->ansiLossy()) {
        int c = fl_choice("Some characters in\n%s\ncannot be saved as ANSI.",
                          "Cancel", "Save as UTF-8", "Save anyway (as ?)",
                          baseName(d->path));
        if (c == 0) return 0;
        if (c == 1) d->ed->encoding(CODE_ENC_UTF8);
    }
    if (d->ed->saveFile(d->path) != 0) {
        fl_alert("Could not save\n%s", d->path);
        return 0;
    }
    refreshLabel(i);
    msg("saved %s\n", d->path);
    return 1;
}

/* Returns 1 if it is OK to discard doc i (saved, or the user said so). */
static int confirmDiscard(int i)
{
    int c;
    if (i < 0 || i >= gDocCount || !gDocs[i].ed->dirty()) return 1;
    gTabs->value(gDocs[i].ed);            /* show what is being asked about */
    gTabs->redraw();
    c = fl_choice("Save changes to\n%s?",
                  "Cancel", "Save", "Don't save",
                  gDocs[i].path[0] ? baseName(gDocs[i].path) : "untitled");
    if (c == 0) return 0;                 /* Cancel */
    if (c == 1) return saveDoc(i, 0);     /* Save (may itself be cancelled) */
    return 1;                             /* Don't save */
}

/* Retire an editor widget safely.
 *
 * THREE separate hazards, all of which bite when closing a tab from the File
 * menu, because the callback runs inside the menu's own event loop:
 *
 * 1. Fl::delete_widget(), NOT delete. FLTK still holds references to a widget
 *    while a callback is running; deleting outright is documented as unsafe and
 *    is what crashed File / New -> Close. delete_widget() queues it for
 *    Fl::do_widget_deletion() once the loop unwinds.
 * 2. Fl_Group::remove() clears savedfocus_ but NOT resizable_ (Fl_Group.cxx),
 *    so removing the resizable child leaves a dangling pointer that the next
 *    Fl_Group::sizes() happily dereferences.
 * 3. Fl_Tabs::push_ is a bare Fl_Widget* for the tab under the mouse button. It
 *    is normally cleared on release, but clearing it costs nothing.
 */
static void retireEditor(CodeEditor *ed)
{
    if (!ed) return;
    codeTrace(" retire: enter ed=%p children=%d", (void *)ed, gTabs->children());
    gTabs->push(0);                                            /* hazard 3 */
    codeTrace(" retire: push(0) ok");
    if (gTabs->resizable() == (Fl_Widget *)ed)
        gTabs->resizable((Fl_Widget *)0);                      /* hazard 2 */
    codeTrace(" retire: resizable ok");
    gTabs->remove(ed);
    codeTrace(" retire: removed, children=%d", gTabs->children());
    Fl::delete_widget(ed);                                     /* hazard 1 */
    codeTrace(" retire: queued for deletion -- exit");
}

static void closeDoc(int i)
{
    int j;
    CodeEditor *dying;

    codeTrace("closeDoc: enter i=%d count=%d", i, gDocCount);
    if (i < 0 || i >= gDocCount) { codeTrace("closeDoc: bad index -- return"); return; }
    if (!confirmDiscard(i)) { codeTrace("closeDoc: user cancelled"); return; }

    codeTrace("closeDoc: confirmed, dying=%p", (void *)gDocs[i].ed);
    dying = gDocs[i].ed;

    /* Drop it from the array BEFORE retiring the widget, so nothing reachable
     * (the label timer, a redraw) can still find it. */
    for (j = i; j < gDocCount - 1; j++) gDocs[j] = gDocs[j + 1];
    gDocCount--;
    gDocs[gDocCount].ed = 0;

    codeTrace("closeDoc: array shifted, count=%d", gDocCount);
    retireEditor(dying);

    if (gDocCount == 0) {
        codeTrace("closeDoc: was last doc -> addDoc(0)");
        addDoc(0);
        codeTrace("closeDoc: addDoc returned");
    } else {
        int sel = i < gDocCount ? i : gDocCount - 1;
        codeTrace("closeDoc: selecting %d", sel);
        gTabs->value(gDocs[sel].ed);
        codeTrace("closeDoc: value ok");
        gTabs->resizable(gDocs[0].ed);      /* restore a LIVE resizable child */
        codeTrace("closeDoc: resizable ok");
        gDocs[sel].ed->take_focus();
        codeTrace("closeDoc: focus ok");
    }
    gTabs->redraw();
    codeTrace("closeDoc: EXIT CLEAN");
}

/* ---- the session --------------------------------------------------------
 *
 * Hot exit, as Sublime does it: closing the window asks nothing and the next
 * start puts the desk back, unsaved buffers included. Settings / Restore open
 * files on start switches the whole thing off.
 *
 * The store is edit_session.h. What is here is the two halves that need the
 * documents: reading the desk out of them, and building it back.
 */
static CodeSession gSes;                  /* the session being restored */
static int gSesDoc[CODE_SES_MAXTABS];     /* which document each tab became */

/* Everything on the desk, written where the next start will find it.
 *
 * Returns 0 if ANY part of it failed -- and then the new generation is thrown
 * away and the old one left untouched, so a failed write loses nothing that
 * was already safe. The caller must fall back to asking about unsaved files
 * the old way: dropping them silently is the one outcome a feature whose
 * whole promise is "your work comes back" must never produce. */
static int sessionSave(void)
{
    const char *dirs[CODE_SES_MAXOPEN];
    CodeSession old;
    char dir[600];
    int i, k, n, ok = 1;

    codeSesLoad(&old);                    /* for its generation letter only */
    codeSesInit(&gSes);
    gSes.gen = codeSesNextGen(old.gen);
    codeSesDir(dir, (int)sizeof(dir));
    if (dir[0]) fl_mkdir(dir, 0700);      /* already there: fails, which is fine */

    for (i = 0; i < gDocCount && gSes.nTabs < CODE_SES_MAXTABS; i++) {
        CodeDoc *d = &gDocs[i];
        CodeSesTab *t = &gSes.tab[gSes.nTabs];
        int dirty = d->ed->dirty();

        /* An untitled tab with nothing in it is not a thing to put back. */
        if (!d->path[0] && !dirty) continue;
        memset(t, 0, sizeof(*t));
        copyStr(t->path, (int)sizeof(t->path), d->path);
        t->caret = d->ed->insert_position();
        t->top   = d->ed->topLine();
        t->enc   = d->ed->encoding();
        t->crlf  = d->ed->crlf();
        t->lang  = d->ed->language();
        if (dirty || !d->path[0]) {
            char *text = d->ed->text();
            if (!text || !codeSesWriteStash(gSes.gen, gSes.nTabs, text,
                                            t->stash, (int)sizeof(t->stash)))
                ok = 0;                   /* its only copy did not reach disk */
            free(text);
        }
        if (gTabs && gTabs->value() == (Fl_Widget *)d->ed) gSes.active = gSes.nTabs;
        gSes.nTabs++;
    }

    if (gTree) {
        n = gTree->openedDirs(dirs, CODE_SES_MAXOPEN);
        for (k = 0; k < n; k++)
            copyStr(gSes.open[gSes.nOpen++], 512, dirs[k]);
    }

    if (!ok || !codeSesSave(&gSes)) {
        codeSesDropGen(gSes.gen);         /* the old desk is still on disk */
        return 0;
    }
    codeSesDropGen(old.gen);              /* only now is it safe to let go */
    return 1;
}

/* The caret and the scroll, one event loop turn after the window is up.
 * Fl_Text_Display cannot place either until it knows how tall it is, and at
 * restore time it does not: the window has not been shown yet. */
static void sessionPlaceCarets(void *)
{
    int k, i, caret, len;

    for (k = 0; k < gSes.nTabs; k++) {
        i = gSesDoc[k];
        if (i < 0 || i >= gDocCount) continue;
        len   = gDocs[i].ed->buffer()->length();
        caret = gSes.tab[k].caret;
        if (caret > len) caret = len;     /* the file changed under us */
        if (caret < 0)   caret = 0;
        gDocs[i].ed->insert_position(gDocs[i].ed->buffer()->utf8_align(caret));
        gDocs[i].ed->topLine(gSes.tab[k].top);   /* after the caret: that
                                                    scrolls too */
    }
}

/* Build the desk back. Called before the command line is opened, so a file
 * passed on it lands on top of the restored tabs rather than instead. */
static void sessionRestore(void)
{
    int k, active = -1;

    if (!codeSesLoad(&gSes)) return;
    gRestoring = 1;
    for (k = 0; k < gSes.nTabs; k++) {
        CodeSesTab *t = &gSes.tab[k];
        CodeEditor *ed = 0;

        gSesDoc[k] = -1;
        if (t->stash[0]) {
            /* Unsaved: the stash IS the document. Its path may still point at
               a real file -- that file is the older text, and must not be the
               one we show. */
            char *text = codeSesReadStash(t->stash);
            if (!text) continue;
            ed = addDoc(0);
            if (ed) {
                int i = gDocCount - 1;
                copyStr(gDocs[i].path, (int)sizeof(gDocs[i].path), t->path);
                ed->text(text);
                if (t->lang >= 0) ed->language(t->lang);
                ed->encoding(t->enc);
                ed->crlf(t->crlf);
                ed->markDirty();          /* text() cleared it; it IS unsaved */
                refreshLabel(i);
            }
            free(text);
        } else if (t->path[0]) {
            /* Probe first. addDoc() raises an fl_alert when a file will not
               open, and a folder cleaned out since last time would then meet
               the user with one modal dialog per tab before the window is
               even up. A file that is gone has nothing to restore: drop the
               tab and say nothing, which is what the file being gone means. */
            FILE *probe = fl_fopen(t->path, "rb");
            if (!probe) continue;
            fclose(probe);
            ed = addDoc(t->path);
            if (ed && t->lang >= 0) ed->language(t->lang);
        }
        if (!ed) continue;
        gSesDoc[k] = gDocCount - 1;
        if (k == gSes.active) active = gDocCount - 1;
    }
    gRestoring = 0;

    if (active >= 0 && gTabs) gTabs->value(gDocs[active].ed);
    if (gTree)
        for (k = 0; k < gSes.nOpen; k++) gTree->expandTo(gSes.open[k]);
    if (gSes.nTabs) Fl::add_timeout(0.0, sessionPlaceCarets, 0);
}

static int quitAll(void)
{
    int i;
    for (i = 0; i < gDocCount; i++)
        if (!confirmDiscard(i)) return 0;
    return 1;
}

/* ---- menu callbacks ---------------------------------------------------- */

static void cbNew (Fl_Widget *, void *) { addDoc(0); }

static void cbOpen(Fl_Widget *, void *)
{
    char picked[512], dirbuf[512];
    if (codeFileOpenDialog(picked, (int)sizeof(picked),
                           currentDir(dirbuf, (int)sizeof(dirbuf)))) {
        /* Before addDoc(): its recentNote() writes the ini, so the new folder
         * is persisted by the same save. Recorded even if the open then fails
         * -- that is still where the user was looking. */
        codeLastDirFromPath(&gSet, picked);
        if (!addDoc(picked)) codeSettingsSave(&gSet);
    }
}

/* Re-read the active document from disk -- for a file another program has
 * changed. Asks first if that would throw away unsaved edits. Keeps the
 * language (it may have been picked by hand) and, as near as the new text
 * allows, the caret; the undo history goes, as on a fresh open. */
static void cbReload(Fl_Widget *, void *)
{
    int i = currentIndex(), lang, pos, len;
    CodeEditor *ed;

    if (i < 0) return;
    ed = gDocs[i].ed;
    if (!gDocs[i].path[0]) { fl_beep(); return; }       /* untitled: nothing to read */
    if (ed->dirty() &&
        fl_choice("Reload\n%s\nfrom disk and lose your changes?",
                  "Cancel", "Reload", 0, baseName(gDocs[i].path)) != 1)
        return;

    lang = ed->language();
    pos  = ed->insert_position();
    if (ed->loadFile(gDocs[i].path) != 0) {
        fl_alert("Could not reload\n%s", gDocs[i].path);
        return;
    }
    ed->language(lang);
    gDocs[i].noSaveBack = ed->truncated();   /* the file may have changed kind */
    len = ed->buffer()->length();
    if (pos > len) pos = len;
    ed->insert_position(ed->buffer()->utf8_align(pos));
    ed->show_insert_position();
    ed->take_focus();
    ed->redraw();
    refreshLabel(i);
}

static void cbSave  (Fl_Widget *, void *) { saveDoc(currentIndex(), 0); }
static void cbSaveAs(Fl_Widget *, void *) { saveDoc(currentIndex(), 1); }
/* A tab's X / middle-click: close that document (not necessarily the active
 * one). closeDoc() prompts for unsaved changes. */
static void cbTabClose(Fl_Widget *w, void *)
{
    closeDoc(docIndexOf(w));
}

/* A tab's right-click menu. Both items answer the same question -- where does
 * this file live -- one for another program, one in the sidebar. Deliberately
 * no Close: a Close one row away from that would be a misclick waiting to
 * happen, and the X and middle-click already close.
 * The paths are absolute already: the dialogs return them that way and
 * command-line names go through ipcFullPath(). Grayed out on an untitled tab,
 * which has no file for either to point at. */
enum { TAB_M_COPY, TAB_M_REVEAL };
static Fl_Menu_Item gTabMenu[] = {
    { "Copy &absolute path", 0, 0, (void *)TAB_M_COPY },
    { "&Select in Folders",  0, 0, (void *)TAB_M_REVEAL },
    { 0 }
};

static void cbTabMenu(Fl_Widget *w, void *)
{
    const Fl_Menu_Item *m;
    int i = docIndexOf(w);
    const char *path;

    if (i < 0) return;
    path = gDocs[i].path;
    if (path[0]) {
        gTabMenu[TAB_M_COPY].activate();
        gTabMenu[TAB_M_REVEAL].activate();
    } else {
        gTabMenu[TAB_M_COPY].deactivate();
        gTabMenu[TAB_M_REVEAL].deactivate();
    }
    m = gTabMenu->popup(Fl::event_x(), Fl::event_y(), 0, 0, codeContextLook);
    if (!m) return;
    switch ((int)(long)m->user_data()) {
    case TAB_M_COPY:   Fl::copy(path, (int)strlen(path), 1); break;
    case TAB_M_REVEAL: revealInFolders(path);                break;
    }
}

static void cbClose (Fl_Widget *, void *)
{
    int i = currentIndex();
    codeTrace("cbClose: currentIndex=%d", i);
    closeDoc(i);
    codeTrace("cbClose: returned");
}

#ifdef _WIN32
static void ipcUnregister(void);           /* single instance, near main() */
#endif

static void cbExit(Fl_Widget *, void *)
{
    /* Hot exit. quitAll() -- the per-file "save changes?" -- is skipped
       entirely, because the whole point is that the work is kept, not
       decided about. If the session could not be written, the old prompt is
       still there to catch what would otherwise be lost. */
    int go = gSet.restoreSession ? (sessionSave() || quitAll()) : quitAll();
    if (go) {
        /* Switched off: leave nothing behind that a later switch-on could
           restore by surprise. Inside the branch, so a cancelled exit does
           not quietly throw the last desk away. */
        if (!gSet.restoreSession) codeSesClear();
        if (gSet.rememberWin) {
            gSet.winX = gWin->x();
            gSet.winY = gWin->y();
            gSet.winW = gWin->w();
            gSet.winH = gWin->h();
        }
        gSet.foldersShow = gSideShow;
        gSet.foldersW    = gSideW;
        codeSettingsSave(&gSet);
        if (gFindWin) gFindWin->hide();   /* else Fl::run() never returns */
        if (gGrepWin) gGrepWin->hide();   /* ...and the same for this one */
#ifdef _WIN32
        ipcUnregister();
#endif
        gWin->hide();
    }
}

static void cbWinClose(Fl_Widget *, void *) { cbExit(0, 0); }

static void cbUndo(Fl_Widget *, void *) { int i = currentIndex(); if (i >= 0) gDocs[i].ed->undo(); }
static void cbRedo(Fl_Widget *, void *) { int i = currentIndex(); if (i >= 0) gDocs[i].ed->redo(); }

/* Word wrap is per document, so the menu check has to follow the active tab --
 * syncWrapItem() is called from the label timer, which is already running.
 *
 * The cost of wrapping is real but narrow: FLTK repaints the whole widget on a
 * keystroke that re-flows a paragraph (see the note in edit_code.h). Reading
 * and scrolling are unaffected, which is exactly the case this is for -- long
 * prose lines in a .md you are not editing. */
static Fl_Menu_Bar *gMenuBar;
static void cbWrap(Fl_Widget *, void *);   /* found by address below */

static void syncWrapItem(void)
{
    Fl_Menu_Item *it;
    int i = currentIndex();
    if (!gMenuBar) return;
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbWrap);
    if (!it) return;
    editMenuCheck(it, i >= 0 && gDocs[i].ed->wrapped());
}

static void cbWrap(Fl_Widget *, void *)
{
    int i = currentIndex();
    CodeEditor *ed;
    if (i < 0) return;
    ed = gDocs[i].ed;
    /* switching on, and this file never had its own column: use the default */
    if (!ed->wrapped() && !gDocs[i].wrapColSet) ed->wrapColumn(gSet.wrapCol);
    ed->wrapEnable(!ed->wrapped());
    ed->redraw();
    syncWrapItem();
}

static void cbWrapCol(Fl_Widget *, void *)
{
    CodeEditor *ed;
    int col, i = currentIndex();

    if (i < 0) return;
    ed = gDocs[i].ed;
    if (!codeAskInt("Wrap at Column", "Wrap at column  (0 = wrap to window width):",
                    gDocs[i].wrapColSet ? ed->wrapColumn() : gSet.wrapCol, &col))
        return;
    /* Same range the Settings dialog clamps wrap_column to. fl_input() let a
     * negative through, which reaches wrapPixels() as a negative width. */
    ed->wrapColumn(codeClampInt(col, 0, 1000));
    gDocs[i].wrapColSet = 1;
    ed->wrapEnable(1);
    ed->redraw();
    syncWrapItem();
}

static void cbLang(Fl_Widget *w, void *v)
{
    int i = currentIndex();
    (void)w;
    if (i >= 0) gDocs[i].ed->language((int)(long)v);
    syncLangItems();
}

/* Language is per document too: check the active tab's language. The four
 * items are one contiguous run with user_data = LEX_LANG_*. Also catches
 * language changes the menu never saw (Save As with a new suffix). */
static void syncLangItems(void)
{
    Fl_Menu_Item *it;
    int i = currentIndex();
    int lang = i >= 0 ? gDocs[i].ed->language() : -1;
    if (!gMenuBar) return;
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbLang);   /* first of the run */
    for (; it && it->text && it->callback() == cbLang; it = it->next()) {
        editMenuCheck(it, (int)(long)it->user_data() == lang);
    }
}

/* ---- encoding / line ending ---------------------------------------------
 * Per document, like the language; the label timer keeps the checks on the
 * active tab. Choosing one converts on the next save (and marks the file
 * modified) -- the text in the editor is UTF-8 either way. */
static char gAnsiLabel[48] = "ANSI";     /* gets the code page in main() */

static void cbEnc(Fl_Widget *, void *v)
{
    int i = currentIndex();
    if (i >= 0) gDocs[i].ed->encoding((int)(long)v);
    syncEncItems();
}
static void cbEol(Fl_Widget *, void *v)
{
    int i = currentIndex();
    if (i >= 0) gDocs[i].ed->crlf((int)(long)v);
    syncEncItems();
}
static void syncEncItems(void)
{
    Fl_Menu_Item *it;
    int i = currentIndex();
    int enc  = i >= 0 ? gDocs[i].ed->encoding() : -1;
    int crlf = i >= 0 ? gDocs[i].ed->crlf() : -1;
    if (!gMenuBar) return;
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbEnc);
    for (; it && it->text && it->callback() == cbEnc; it = it->next())
        editMenuCheck(it, (int)(long)it->user_data() == enc);
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbEol);
    for (; it && it->text && it->callback() == cbEol; it = it->next())
        editMenuCheck(it, (int)(long)it->user_data() == crlf);
}

/* ---- recent files ------------------------------------------------------
 * CODE_MAX_RECENT fixed slots in the static menu, shown / hidden and
 * relabelled as the list changes. Fl_Menu_::find_item(callback) walks the raw
 * array, hidden items included, and the slots are contiguous, so slot k is
 * simply first + k. */
static char gRecentLabel[CODE_MAX_RECENT][600];
static void cbRecent(Fl_Widget *, void *v);
static void cbRecentNone(Fl_Widget *, void *) {}

static void syncRecentItems(void)
{
    Fl_Menu_Item *first, *none;
    int k;
    if (!gMenuBar) return;
    first = (Fl_Menu_Item *)gMenuBar->find_item(cbRecent);
    none  = (Fl_Menu_Item *)gMenuBar->find_item(cbRecentNone);
    if (!first) return;
    for (k = 0; k < CODE_MAX_RECENT; k++) {
        Fl_Menu_Item *it = first + k;
        if (k < gSet.nRecent) {
            /* "&1  path" -- a '&' in the path would be read as a shortcut */
            char *o = gRecentLabel[k], *end = o + sizeof(gRecentLabel[k]) - 2;
            const char *p = gSet.recent[k];
            o += sprintf(o, "&%d  ", k + 1);
            for (; *p && o < end; p++) { if (*p == '&') *o++ = '&'; *o++ = *p; }
            *o = '\0';
            it->labeltype(EDIT_MENUPAD_LABEL);   /* hidden at editMenuPad() time */
            it->show();
        } else {
            it->hide();
        }
    }
    if (none) {
        none->labeltype(EDIT_MENUPAD_LABEL);
        if (gSet.nRecent) none->hide(); else none->show();
    }
}

static void recentNote(const char *path)
{
    if (gRestoring) return;     /* see the declaration: a restore is not a
                                   reason to reorder Recent Files */
    codeRecentAdd(&gSet, path);
    codeSettingsSave(&gSet);
    syncRecentItems();
}

/* Show path's tab if it is already open, else open it in a new one.
 * Returns 0 if it cannot be opened (addDoc() has said so). */
static int openOrShow(const char *path)
{
    int i;
    for (i = 0; i < gDocCount; i++)                   /* already open: just show it */
        if (codePathEq(gDocs[i].path, path)) {
            gTabs->value(gDocs[i].ed);
            gTabs->redraw();
            gDocs[i].ed->take_focus();
            recentNote(path);
            return 1;
        }
    return addDoc(path) != 0;
}

static void cbRecent(Fl_Widget *, void *v)
{
    char path[512];
    int i, k = (int)(long)v;
    if (k < 0 || k >= gSet.nRecent) return;
    copyStr(path, (int)sizeof(path), gSet.recent[k]);
    if (!openOrShow(path)) {                          /* gone: drop it from the list */
        for (i = 0; i < gSet.nRecent; i++)
            if (codePathEq(gSet.recent[i], path)) { codeRecentRemove(&gSet, i); break; }
        codeSettingsSave(&gSet);
        syncRecentItems();
    }
}

/* ---- Folders sidebar ----------------------------------------------------
 * Layout is one routine. Everything that can change the sidebar -- the
 * splitter, the View toggle, the window growing -- ends up in layoutSide(),
 * so there is exactly one place that knows where the three widgets go.
 */
static void syncFoldersItem(void);

static void layoutSide(void)
{
    int top = editDpi(MENU_H);
    int bot = gWin->h() - editDpi(STATUS_H);
    int gw  = editDpi(SPLIT_W);

    if (!gTree) return;
    if (gSideShow) {
        gTree->resize(0, top, gSideW, bot - top);
        gSplit->resize(gSideW, top, gw, bot - top);
        gTree->show();
        gSplit->show();
        gTabs->resize(gSideW + gw, top, gWin->w() - gSideW - gw, bot - top);
    } else {
        /* Hidden widgets are skipped by draw() and by Fl_Group::handle(), so
         * they are simply left where they are. Resizing them to zero width
         * instead would hand Fl_Tree::calc_dimensions() a negative inner
         * width to divide by. */
        gTree->hide();
        gSplit->hide();
        gWin->cursor(FL_CURSOR_DEFAULT);   /* the bar may have vanished from
                                              under the pointer, and then no
                                              FL_LEAVE is ever delivered */
        gTabs->resize(0, top, gWin->w(), bot - top);
    }
    /* The group caches every child's rectangle for proportional resizing;
     * without this the next window resize interpolates from the old ones. */
    gWin->init_sizes();
    gWin->redraw();
}

static void cbSplitDrag(int leftW, void *)
{
    int lo = editDpi(FOLD_W_MIN);
    int hi = gWin->w() - editDpi(SPLIT_W) - editDpi(TABS_W_MIN);
    if (hi < lo) hi = lo;
    if (leftW < lo) leftW = lo;
    if (leftW > hi) leftW = hi;
    /* FL_DRAG fires per motion event and layoutSide() resizes every open
     * editor, so going nowhere must cost nothing. */
    if (leftW == gSideW) return;
    gSideW = leftW;
    /* Mirrored into the settings but NOT written: an ini rewrite per mouse
       motion would be unbearable on a PII. Whoever saves next -- the View
       toggle, Settings, Exit -- picks up the current width. */
    gSet.foldersW = gSideW;
    layoutSide();
}

/* Opening a document from inside the tree's handle() would create an
 * Fl_Text_Display, move the focus and possibly raise an fl_alert while
 * Fl::pushed() still points at the tree. Defer it by one event loop turn --
 * the same trick the single-instance receiver uses for the same reason. */
static char gFoldPending[600];

static void foldOpenLater(void *)
{
    if (!gFoldPending[0]) return;
    openOrShow(gFoldPending);
    gFoldPending[0] = '\0';
}

static void cbFoldActivate(Fl_Tree_Item *it, void *)
{
    const char *path = CodeFolders::pathOf(it);
    if (!path || CodeFolders::isDirOf(it)) return;
    if (!codeFileOpenableName(foldBaseName(path))) {
        if (!codeShellOpen(path)) fl_alert("Windows could not open\n%s", path);
        return;
    }
    copyStr(gFoldPending, (int)sizeof(gFoldPending), path);
    Fl::add_timeout(0.0, foldOpenLater, 0);
}

/* "Open as Text": open a file the extension lists refuse -- an .obj, a .dat,
 * a .foo nobody has taught the editor yet. That list is the only thing
 * standing between codeReadText() and a 40 MB .iso (see the gate comment in
 * edit_fileio.h), so stepping around it means asking the two questions it was
 * answering by proxy, which is what codeFileSniff() reads the file for.
 * Deferred through the same timeout as a double-click, for the same reason. */
static void cbFoldOpenText(Fl_Tree_Item *it)
{
    const char *path = CodeFolders::pathOf(it);
    long size = 0;
    int  nul  = 0;

    if (!path || CodeFolders::isDirOf(it)) return;
    if (!codeFileSniff(path, &size, &nul)) {
        fl_alert("Could not read\n%s", path);
        return;
    }
    if (size > FOLD_TEXT_WARN &&
        fl_choice("%s is %ld KB.\nOpening it as text may take a while.",
                  "Cancel", "Open", 0, baseName(path), size / 1024L) != 1)
        return;
    /* A NUL byte is not a "maybe": the document WILL be short of the file,
       which is why loadFile() arms the Save guard for it. Said here anyway,
       before the tab opens, because a half-loaded file looks whole. */
    if (nul &&
        fl_choice("%s is not a text file.\n\nOnly the part before its first "
                  "zero byte can be shown, so Save\nwill ask for another name "
                  "rather than truncate it.",
                  "Cancel", "Open anyway", 0, baseName(path)) != 1)
        return;
    copyStr(gFoldPending, (int)sizeof(gFoldPending), path);
    Fl::add_timeout(0.0, foldOpenLater, 0);
}

/* Fl_Tree's own callback: the only place that catches every way an item can
 * be opened -- the [+] icon, Enter, Right-arrow and our double-click. */
static void cbFoldTree(Fl_Widget *w, void *)
{
    CodeFolders *t = (CodeFolders *)w;
    if (t->callback_reason() == FL_TREE_REASON_OPENED)
        t->populate(t->callback_item());
}

static void addFolder(const char *path)
{
    Fl_Tree_Item *it;
    if (!gTree || !path || !*path) return;
    if (gSet.nFolders >= CODE_MAX_FOLDERS) {
        fl_alert("The sidebar already lists %d folders.", CODE_MAX_FOLDERS);
        return;
    }
    it = gTree->addRoot(path);
    if (!it) return;
    gTree->open(it);                      /* docallback 1 -> populate */
    gTree->redraw();
    if (codeFolderAdd(&gSet, path)) codeSettingsSave(&gSet);
    if (!gSideShow) { gSideShow = 1; layoutSide(); syncFoldersItem(); }
}

static void cbOpenFolder(Fl_Widget *, void *)
{
    char picked[512], dirbuf[512];
    if (!codeFolderDialog(picked, (int)sizeof(picked),
                          currentDir(dirbuf, (int)sizeof(dirbuf))))
        return;
    addFolder(picked);
}

/* Removes the root CONTAINING the selection, and only from the list -- the
 * folder itself is never touched. */
static void cbRemoveFolder(Fl_Widget *, void *)
{
    Fl_Tree_Item *sel, *root;
    const char *path;
    char keep[512];

    if (!gTree) return;
    sel = gTree->first_selected_item();
    root = gTree->rootOf(sel);
    if (!root) {
        fl_alert("Select a folder in the sidebar first.");
        return;
    }
    path = CodeFolders::pathOf(root);
    copyStr(keep, (int)sizeof(keep), path ? path : "");
    gTree->removeRoot(root);              /* frees the subtree's FoldItems */
    if (keep[0] && codeFolderRemove(&gSet, keep)) codeSettingsSave(&gSet);
}

/* The same switch as the Settings checkbox, one click away -- it is the kind
 * of thing you turn off for one crowded file and back on after. Both write
 * the ini, so whichever was used last is what the next run starts with. */
static void syncHighlightItem(void);

static void cbHighlightWord(Fl_Widget *, void *)
{
    gSet.highlightWord = !gSet.highlightWord;
    applySettings();                  /* clears or repaints the underlines */
    syncHighlightItem();
    codeSettingsSave(&gSet);
}

static void syncHighlightItem(void)
{
    Fl_Menu_Item *it;
    if (!gMenuBar) return;
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbHighlightWord);
    if (it) editMenuCheck(it, gSet.highlightWord);
}

static void cbFolders(Fl_Widget *, void *)
{
    gSideShow = !gSideShow;
    gSet.foldersShow = gSideShow;
    layoutSide();
    syncFoldersItem();
    codeSettingsSave(&gSet);
}

static void syncFoldersItem(void)
{
    Fl_Menu_Item *it;
    if (!gMenuBar) return;
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbFolders);
    if (it) editMenuCheck(it, gSideShow);
}

/* The tree's right-click menu. Same shape as the tab menu: a standalone
 * array padded once at startup, popped with the menu bar as its look. */
enum { FOLD_M_TEXT, FOLD_M_OPEN, FOLD_M_REMOVE, FOLD_M_REFRESH };
static Fl_Menu_Item gFoldMenu[] = {
    { "Open as &Text",   0, 0, (void *)FOLD_M_TEXT, FL_MENU_DIVIDER },
    { "Open &Folder...", 0, 0, (void *)FOLD_M_OPEN },
    { "&Remove Folder",  0, 0, (void *)FOLD_M_REMOVE, FL_MENU_DIVIDER },
    { "Re&fresh",        0, 0, (void *)FOLD_M_REFRESH },
    { 0 }
};

static void cbFoldMenu(Fl_Tree_Item *it, void *)
{
    const Fl_Menu_Item *m;
    /* A file and only a file -- on a folder the command means nothing. */
    if (it && !CodeFolders::isDirOf(it)) gFoldMenu[FOLD_M_TEXT].activate();
    else                                 gFoldMenu[FOLD_M_TEXT].deactivate();
    /* Both need something to act on; Open Folder never does. */
    if (gTree->rootOf(it)) {
        gFoldMenu[FOLD_M_REMOVE].activate();
        gFoldMenu[FOLD_M_REFRESH].activate();
    } else {
        gFoldMenu[FOLD_M_REMOVE].deactivate();
        gFoldMenu[FOLD_M_REFRESH].deactivate();
    }
    m = gFoldMenu->popup(Fl::event_x(), Fl::event_y(), 0, 0, codeContextLook);
    if (!m) return;
    switch ((int)(long)m->user_data()) {
    case FOLD_M_TEXT:    cbFoldOpenText(it);   break;
    case FOLD_M_OPEN:    cbOpenFolder(0, 0);   break;
    case FOLD_M_REMOVE:  cbRemoveFolder(0, 0); break;
    /* `it` is still good here: Refresh is the one branch that does not
       destroy the item it was handed -- Remove does, which is why that one
       re-finds the selection for itself. */
    case FOLD_M_REFRESH: gTree->refresh(it);   break;
    }
}

/* ---- Select in Folders (the tab menu) -----------------------------------
 *
 * Deferred by one event loop turn, the same way a double-click in the tree
 * is: Fl_Tree::show_item() scrolls using the y each item was given in the
 * last draw, and when the sidebar has just come out of hiding -- the common
 * case for this command -- those are stale or were never set at all. One turn
 * later the tree has drawn itself and the scroll lands where it should.
 */
static char gRevealPending[600];

static void revealLater(void *)
{
    if (!gRevealPending[0]) return;
    if (gTree && !gTree->reveal(gRevealPending))
        fl_message("%s is not shown in the Folders sidebar.\n\n"
                   "Its folder is not listed there, the file is hidden, or\n"
                   "the folder was read before the file existed -- Refresh\n"
                   "on the tree's right-click menu re-reads it.",
                   baseName(gRevealPending));
    gRevealPending[0] = '\0';
}

/* Show the sidebar if it was hidden -- the command means nothing otherwise --
 * but never ADD a folder: "select it if the folders hold it" is the whole
 * contract, and listing a new root behind the user's back would be a
 * different command wearing this one's name. The focus stays in the editor:
 * this answers a question about the file being edited, and typing on should
 * not need a click back. */
static void revealInFolders(const char *path)
{
    if (!gTree || !path || !path[0]) return;
    if (!gSideShow) {
        gSideShow = 1;
        gSet.foldersShow = gSideShow;
        layoutSide();
        syncFoldersItem();
        codeSettingsSave(&gSet);
    }
    copyStr(gRevealPending, (int)sizeof(gRevealPending), path);
    Fl::add_timeout(0.0, revealLater, 0);
}

/* ---- settings ---------------------------------------------------------- */
static void applySettings(void)
{
    int i;
    codeIndent      = gSet.tabWidth;
    codeUseTabs     = gSet.useTabs;
    codeLineNumbers = gSet.lineNumbers;
    codeOccurs      = gSet.highlightWord;
    codeSetFontSize(editDpi(gSet.fontSize));
    for (i = 0; i < gDocCount; i++) gDocs[i].ed->applySettings();
    if (gTabs) {
        gTabs->firstTabAt(codeLineNumbers ? codeGutterWidth() : 0);
        gTabs->redraw();
    }
}

static void cbSettings(Fl_Widget *, void *)
{
    char buf[16];
    Fl_Double_Window *w = new Fl_Double_Window(340, 298, "Settings");
    Fl_Int_Input *font, *tab, *wrap;
    Fl_Check_Button *tabs, *nums, *occ, *trim, *rem, *ses;
    Fl_Return_Button *ok;
    Fl_Button *cancel;
    Fl_Box *hint;

    w->begin();
    font = new Fl_Int_Input(150, 10, 60, 24, "Code font size:");
    tab  = new Fl_Int_Input(150, 40, 60, 24, "Tab width:");
    wrap = new Fl_Int_Input(150, 70, 60, 24, "Word wrap column:");
    hint = new Fl_Box(216, 70, 116, 24, "0 = window width");
    hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    tabs = new Fl_Check_Button(16, 104, 310, 22, "Indent with tab characters");
    nums = new Fl_Check_Button(16, 128, 310, 22, "Show line numbers");
    occ  = new Fl_Check_Button(16, 152, 310, 22,
                               "Underline the word at the caret");
    trim = new Fl_Check_Button(16, 176, 310, 22,
                               "Remove trailing whitespace on save");
    rem  = new Fl_Check_Button(16, 200, 310, 22, "Remember window size and position");
    ses  = new Fl_Check_Button(16, 224, 310, 22, "Restore open files on start");
    ses->tooltip("Closing the window keeps unsaved files instead of asking "
                 "about them");
    ok     = new Fl_Return_Button(150, 262, 86, 26, "OK");
    cancel = new Fl_Button(244, 262, 86, 26, "Cancel");
    w->end();
    editDpiScaleTree(w);

    sprintf(buf, "%d", gSet.fontSize); font->value(buf);
    sprintf(buf, "%d", gSet.tabWidth); tab->value(buf);
    sprintf(buf, "%d", gSet.wrapCol);  wrap->value(buf);
    tabs->value(gSet.useTabs);
    nums->value(gSet.lineNumbers);
    occ->value(gSet.highlightWord);
    trim->value(gSet.trimTrailing);
    rem->value(gSet.rememberWin);
    ses->value(gSet.restoreSession);

    /* Modal, driven by Fl::readqueue(): widgets without a callback of their
     * own are queued when activated. Closing the window just hides it. */
    w->set_modal();
    editPlaceDialog(w);
    w->show();
    while (w->shown()) {
        Fl_Widget *o = Fl::readqueue();
        if (!o) { Fl::wait(); continue; }
        if (o == cancel) break;
        if (o == ok) {
            gSet.fontSize    = codeClampInt(atoi(font->value()), 6, 72);
            gSet.tabWidth    = codeClampInt(atoi(tab->value()), 1, 16);
            gSet.wrapCol     = codeClampInt(atoi(wrap->value()), 0, 1000);
            gSet.useTabs     = tabs->value() ? 1 : 0;
            gSet.lineNumbers = nums->value() ? 1 : 0;
            gSet.highlightWord = occ->value() ? 1 : 0;
            gSet.trimTrailing = trim->value() ? 1 : 0;
            gSet.rememberWin = rem->value() ? 1 : 0;
            gSet.restoreSession = ses->value() ? 1 : 0;
            /* Switched off: drop the desk now rather than leave it to be
               restored by surprise if it is ever switched back on. */
            if (!gSet.restoreSession) codeSesClear();
            applySettings();
            syncHighlightItem();      /* the View check follows the dialog */
            codeSettingsSave(&gSet);
            break;
        }
    }
    w->hide();
    Fl::delete_widget(w);
}

/* ---- status bar --------------------------------------------------------
 * Right-aligned fields: Ln/Col, language, line ending, encoding. Refreshed
 * from an Fl::add_check() hook (after every event batch); it only redraws
 * when the text actually changed. */
static const char *langName(int lang)
{
    switch (lang) {
    case LEX_LANG_PASCAL:   return "Pascal";
    case LEX_LANG_LUA:      return "Lua";
    case LEX_LANG_MARKDOWN: return "Markdown";
    case LEX_LANG_C:        return "C / C++";
    case LEX_LANG_JAVA:     return "Java";
    case LEX_LANG_JS:       return "JavaScript";
    case LEX_LANG_PYTHON:   return "Python";
    case LEX_LANG_CSS:      return "CSS";
    case LEX_LANG_HTML:     return "HTML";
    case LEX_LANG_PHP:      return "PHP";
    case LEX_LANG_SQL:      return "SQL";
    case LEX_LANG_BASH:     return "Bash";
    case LEX_LANG_BAT:      return "Batch";
    case LEX_LANG_INI:      return "INI";
    case LEX_LANG_JSON:     return "JSON";
    case LEX_LANG_XML:      return "XML";
    default:                return "Plain text";
    }
}

class CodeStatus : public Fl_Widget {
public:
    CodeStatus(int X, int Y, int W, int H) : Fl_Widget(X, Y, W, H) { mText[0] = '\0'; }
    /* Fields separated by '\t'. */
    void set(const char *t)
    {
        if (strcmp(t, mText) == 0) return;
        copyStr(mText, (int)sizeof(mText), t);
        redraw();
    }
protected:
    void draw()
    {
        const char *f[8];
        int len[8], n = 0, k, X, base;
        int pad = editDpi(10), gap = editDpi(20);
        const char *p = mText;
        fl_rectf(x(), y(), w(), h(), color());
        while (n < 8) {
            const char *e = strchr(p, '\t');
            f[n] = p;
            len[n] = e ? (int)(e - p) : (int)strlen(p);
            n++;
            if (!e) break;
            p = e + 1;
        }
        fl_font(FL_HELVETICA, FL_NORMAL_SIZE);
        base = y() + (h() + fl_height()) / 2 - fl_descent();
        X = x() + w() - pad;
        for (k = n - 1; k >= 0; k--) {
            X -= (int)fl_width(f[k], len[k]);
            fl_color(labelcolor());
            fl_draw(f[k], len[k], X, base);
            if (k > 0) {                      /* thin divider between fields */
                fl_color(fl_color_average(labelcolor(), color(), 0.3f));
                fl_yxline(X - gap / 2, y() + h() / 4, y() + h() - h() / 4);
            }
            X -= gap;
        }
    }
private:
    char mText[256];
};
static CodeStatus *gStatus;

static void statusCheck(void *)
{
    char buf[200];
    int i, line, col;
    findRetireStaleMsg();
    if (!gStatus || !gTabs) return;
    i = currentIndex();
    if (i < 0) { gStatus->set(""); return; }
    gDocs[i].ed->caretLineCol(&line, &col);
    sprintf(buf, "Ln %d, Col %d\t%s\t%s\t%s", line, col,
            langName(gDocs[i].ed->language()),
            gDocs[i].ed->crlf() ? "Windows (CRLF)" : "Unix (LF)",
            codeEncName(gDocs[i].ed->encoding()));
    gStatus->set(buf);
}

/* ---- Find / Replace ----------------------------------------------------
 * Modeless, so you can keep hunting while editing -- which means it must look
 * up the CURRENT editor on every click rather than holding one, since the user
 * can change tabs while it is open.
 */
static Fl_Input  *gFindIn, *gReplIn;
static Fl_Check_Button *gCaseChk, *gWrapChk;

/* The Find window's own message line -- "Not found", or the Replace All
 * count. In the window rather than an fl_alert() because a modal popup for
 * "no match" is heavy-handed when you are hammering Find Next.
 *
 * gFindMsgFor is the needle the message belongs to, so statusCheck() can
 * retire a stale "Not found" the moment the query is edited: the label must
 * never outlive the search that produced it. label() with a static buffer,
 * not copy_label(), to keep this allocation-free. */
static Fl_Box *gFindMsg;
static char    gFindMsgText[80];
static char    gFindMsgFor[128];     /* truncated; gFindMsgForLen is not */
static int     gFindMsgForLen;

static void findSay(const char *needle, const char *fmt, ...)
{
    va_list ap;
    if (!gFindMsg) return;
    va_start(ap, fmt);
    vsnprintf(gFindMsgText, sizeof(gFindMsgText), fmt, ap);
    va_end(ap);
    gFindMsgText[sizeof(gFindMsgText) - 1] = '\0';
    copyStr(gFindMsgFor, (int)sizeof(gFindMsgFor), needle);
    gFindMsgForLen = needle ? (int)strlen(needle) : 0;
    gFindMsg->label(gFindMsgText);
    gFindMsg->redraw();
}

static void findSayClear(void)
{
    if (!gFindMsg) return;
    gFindMsgText[0]  = '\0';
    gFindMsgFor[0]   = '\0';
    gFindMsgForLen   = 0;
    gFindMsg->label(gFindMsgText);
    gFindMsg->redraw();
}

/* Called from statusCheck() after every event batch: the moment the needle no
 * longer matches the one the message was about, the message is wrong, so drop
 * it. One strcmp, and only while the Find window is up with something to say. */
static void findRetireStaleMsg(void)
{
    const char *v;
    if (!gFindMsg || !gFindMsgText[0] || !gFindIn) return;
    v = gFindIn->value();
    /* Compare the length too: gFindMsgFor holds only the first 127 bytes, so
     * a longer needle would never match itself and the message would vanish
     * the instant it appeared. */
    if ((int)strlen(v) != gFindMsgForLen ||
        strncmp(v, gFindMsgFor, sizeof(gFindMsgFor) - 1) != 0)
        findSayClear();
}

static CodeEditor *curEd(void)
{
    int i = currentIndex();
    return i >= 0 ? gDocs[i].ed : 0;
}

/* Select [at, at+len) and bring it to the MIDDLE of the view, not just barely
 * on screen -- a match arriving with context around it is much easier to read
 * than one pinned to the last line. */
static void selectMatch(CodeEditor *ed, int at, int len)
{
    ed->showCentered(at + len);
    ed->buffer()->select(at, at + len);
    ed->redraw();
}

static void doFind(int backwards)
{
    CodeEditor *ed = curEd();
    const char *needle;
    int at = 0, from, mc, wrap, found;

    if (!ed || !gFindIn) return;
    needle = gFindIn->value();
    if (!needle || !*needle) return;
    mc   = gCaseChk->value();
    wrap = gWrapChk->value();

    /* Search from the edge of the current selection, so repeated Find Next
     * walks forward instead of re-finding what is already highlighted. */
    if (backwards) {
        int selStart, selEnd;
        from = ed->buffer()->selection_position(&selStart, &selEnd)
             ? selStart : ed->insert_position();
        found = codeFindPrev(ed->buffer(), needle, from, mc, wrap, &at);
    } else {
        from = ed->insert_position();
        found = codeFindNext(ed->buffer(), needle, from, mc, wrap, &at);
    }

    if (found) {
        selectMatch(ed, at, (int)strlen(needle));
        findSayClear();
    } else {
        fl_beep();
        findSay(needle, "Not found");
    }
}

static void cbFindNext(Fl_Widget *, void *) { doFind(0); }
static void cbFindPrev(Fl_Widget *, void *) { doFind(1); }

static void cbReplaceOne(Fl_Widget *, void *)
{
    CodeEditor *ed = curEd();
    const char *needle, *repl;
    int selStart, selEnd;

    if (!ed) return;
    needle = gFindIn->value();
    repl   = gReplIn->value();
    if (!needle || !*needle) return;

    /* Only replace when the selection IS the match -- otherwise Replace would
     * clobber whatever happens to be selected. */
    if (ed->buffer()->selection_position(&selStart, &selEnd)) {
        int n = codeReplaceAt(ed->buffer(), selStart, needle, repl,
                              gCaseChk->value());
        if (n >= 0) {
            ed->insert_position(selStart + n);
            ed->buffer()->unselect();
        }
    }
    doFind(0);                       /* then move to the next one */
}

static void cbReplaceAll(Fl_Widget *, void *)
{
    CodeEditor *ed = curEd();
    const char *needle, *repl;
    int n;

    if (!ed) return;
    needle = gFindIn->value();
    repl   = gReplIn->value();
    if (!needle || !*needle) return;

    /* One undo step for the whole sweep, not one per match. */
    ed->beginUndoGroup();
    n = codeReplaceAll(ed->buffer(), needle, repl, gCaseChk->value());
    ed->endUndoGroup();

    ed->buffer()->unselect();
    ed->redraw();
    findSay(needle, "Replaced %d occurrence%s.", n, n == 1 ? "" : "s");
}

static void cbFindClose(Fl_Widget *, void *) { if (gFindWin) gFindWin->hide(); }

static void openFindWindow(int withReplace)
{
    if (!gFindWin) {
        gFindWin = new Fl_Double_Window(380, 150, "Find / Replace");
        gFindWin->begin();
        gFindIn  = new Fl_Input(75, 10, 290, 24, "Find:");
        gFindIn->when(FL_WHEN_ENTER_KEY_ALWAYS);   /* Enter = Find Next */
        gFindIn->callback(cbFindNext);
        gReplIn  = new Fl_Input(75, 40, 290, 24, "Replace:");
        gCaseChk = new Fl_Check_Button(75, 68, 110, 22, "Match case");
        gWrapChk = new Fl_Check_Button(190, 68, 110, 22, "Wrap around");
        gWrapChk->value(1);
        { Fl_Button *b;
          b = new Fl_Button( 10, 96,  80, 24, "Find Next");   b->callback(cbFindNext);
          b = new Fl_Button( 95, 96,  80, 24, "Find Prev");   b->callback(cbFindPrev);
          b = new Fl_Button(180, 96,  80, 24, "Replace");     b->callback(cbReplaceOne);
          b = new Fl_Button(265, 96, 100, 24, "Replace All"); b->callback(cbReplaceAll);
          b = new Fl_Button(285, 122, 80, 24, "Close");       b->callback(cbFindClose);
        }
        /* The message line fills the space left of Close -- no resize needed. */
        gFindMsg = new Fl_Box(10, 122, 270, 24, gFindMsgText);
        gFindMsg->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        gFindWin->end();
        editDpiScaleTree(gFindWin);
        gFindWin->callback(cbFindClose);
    }
    gReplIn->activate();
    findSayClear();                  /* a fresh window starts with no verdict */
    if (!withReplace) gFindIn->take_focus();
    /* Centred each time it is opened, but not while it is up: Ctrl+F with
     * the window already open must not yank it from where it was dragged. */
    if (!gFindWin->shown()) editPlaceDialog(gFindWin);
    gFindWin->show();
    gFindIn->take_focus();
}

static void cbFind   (Fl_Widget *, void *) { openFindWindow(0); }
static void cbReplace(Fl_Widget *, void *) { openFindWindow(1); }

static void cbGotoLine(Fl_Widget *, void *)
{
    CodeEditor *ed = curEd();
    int line, pos;

    if (!ed) return;
    if (!codeAskInt("Go to Line", "Go to line:",
                    codeLineOfPos(ed->buffer(), ed->insert_position()), &line))
        return;
    if (line < 1) return;
    pos = codeGotoLinePos(ed->buffer(), line);
    ed->insert_position(pos);
    ed->buffer()->unselect();
    ed->show_insert_position();
    ed->take_focus();
    ed->redraw();
}

/* ---- Tools ------------------------------------------------------------- */

/* Format JSON: the selection if there is one, else the whole document, using
 * the indent from Settings (Tab width / Indent with tab characters). Broken
 * JSON is left untouched and the caret goes to the problem instead. One undo
 * step either way. */
static void cbFormatJson(Fl_Widget *, void *)
{
    CodeEditor *ed = curEd();
    Fl_Text_Buffer *buf;
    char *src, *out;
    const char *why = 0;
    int a, b, at = 0;

    if (!ed) return;
    buf = ed->buffer();
    if (!buf->selection_position(&a, &b)) { a = 0; b = buf->length(); }
    src = buf->text_range(a, b);
    if (!src) return;
    out = codeJsonFormat(src, b - a, codeIndent, codeUseTabs, &at, &why);
    free(src);

    if (!out) {
        int pos = a + at, line = codeLineOfPos(buf, pos);
        buf->unselect();
        ed->insert_position(pos);
        ed->show_insert_position();
        ed->take_focus();
        fl_alert("Not valid JSON: %s\n(line %d, column %d)", why, line,
                 pos - buf->line_start(pos) + 1);
        return;
    }
    ed->beginUndoGroup();
    buf->replace(a, b, out);
    ed->endUndoGroup();
    free(out);
    buf->unselect();
    ed->insert_position(a);
    ed->show_insert_position();
    ed->take_focus();
    ed->redraw();
}

/* ---- menu -------------------------------------------------------------- */

/* ---- Find in Folder -----------------------------------------------------
 * Ctrl+Shift+F searches the current PROJECT, recursively -- see
 * grepFolder() for which of the sidebar's top-level folders that is. With
 * All ticked it searches every folder the sidebar lists instead, which is
 * the same search and the same Replace All over a wider set of files. The
 * model half -- the walk, the matching, the text replace -- is edit_grep.h;
 * this is the window over it.
 */
static Fl_Input         *gGrepFind, *gGrepRepl;
static Fl_Check_Button  *gGrepCase, *gGrepAll;
static Fl_Hold_Browser  *gGrepList;
static Fl_Box           *gGrepMsg;
static char              gGrepMsgText[160];
static char              gGrepRoot[512];
static CodeGrep          gGrep;                /* owns the current results */

#define GREP_MAX_HITS 2000

/* Hand the search the live text of anything open in a tab, so results match
   what is on screen rather than what was last saved -- and so a file just
   replaced in its buffer stops reporting the matches it no longer has.
   Fl_Text_Buffer::text() mallocs; edit_grep.h frees it. */
static char *grepOpenText(const char *path)
{
    int i;
    for (i = 0; i < gDocCount; i++)
        if (gDocs[i].path[0] && codePathEq(gDocs[i].path, path))
            return gDocs[i].ed->buffer()->text();
    return 0;
}

/* Which folder a search runs in -- the CURRENT PROJECT. The sidebar's
 * top-level folders are the projects, so this is whichever of those the work
 * in front of you belongs to, tried in order:
 *
 *   1. the project holding the file in the active tab -- you are editing it,
 *      so that is the project you mean;
 *   2. failing that, the project holding whatever is selected in the tree,
 *      the only other thing that says "I am looking at this one";
 *   3. failing both -- nothing open, nothing selected -- the first listed.
 *
 * The window title names the result, so a search never runs against a
 * project you did not expect without saying which. */
static const char *grepFolder(void)
{
    Fl_Tree_Item *r = gTree ? gTree->root() : 0;
    Fl_Tree_Item *sel;
    const char *p;
    int i, k;

    if (!r || r->children() < 1) return 0;

    i = currentIndex();
    if (i >= 0 && gDocs[i].path[0]) {
        for (k = 0; k < r->children(); k++) {
            p = CodeFolders::pathOf(r->child(k));
            if (p && codeFileUnder(gDocs[i].path, p)) return p;
        }
    }
    sel = gTree->rootOf(gTree->first_selected_item());
    if (sel && (p = CodeFolders::pathOf(sel)) != 0) return p;
    return CodeFolders::pathOf(r->child(0));
}

/* Every top-level folder in the sidebar -- what All searches -- minus any
 * that is already inside another listed one. Listing both X:\Projects and
 * X:\Projects\SOOB-Code is perfectly reasonable, but walking both would
 * report every hit under SOOB-Code twice, and then Replace All would count
 * each of them twice. edit_grep.h cannot notice that for us: two walks have
 * no idea they met the same file. codeFileUnder() compares on a separator
 * boundary, so X:\Projects does not swallow X:\Projects2.
 *
 * The pointers are the tree's own and stay valid only while it does. Use
 * them and drop them; never keep them across anything that could remove a
 * folder. */
static int grepRoots(const char **out, int maxN)
{
    Fl_Tree_Item *r = gTree ? gTree->root() : 0;
    const char *p, *q;
    int k, j, nested, n = 0;

    if (!r) return 0;
    for (k = 0; k < r->children() && n < maxN; k++) {
        p = CodeFolders::pathOf(r->child(k));
        if (!p || !*p) continue;
        nested = 0;
        for (j = 0; j < r->children() && !nested; j++)
            if (j != k && (q = CodeFolders::pathOf(r->child(j))) != 0)
                nested = codeFileUnder(p, q);
        if (!nested) out[n++] = p;
    }
    return n;
}

/* Paths are shown relative to the folder they were found in -- absolute ones
 * are all prefix and push the interesting part off the right of the list.
 *
 * With `nr` roots (All), the folder's own NAME is kept and only what is above
 * it is cut: "SOOB-Code\codeedit.cpp". A bare "codeedit.cpp" in a list drawn
 * from five projects would not say which one it came from, and that is the
 * one thing the All list has to make obvious. */
static const char *grepRel(const char *path, const char *const *roots, int nr)
{
    const char *base;
    int k, n;

    for (k = 0; k < nr; k++) {
        if (!codeFileUnder(path, roots[k])) continue;
        base = baseName(roots[k]);
        return (*base && base > roots[k]) ? path + (base - roots[k]) : path;
    }
    if (nr) return path;                      /* All: not under any of them */

    n = (int)strlen(gGrepRoot);
    if (n > 0 && !strncmp(path, gGrepRoot, (size_t)n)) {
        const char *p = path + n;
        while (*p == '/' || *p == '\\') p++;
        if (*p) return p;
    }
    return path;
}

static void grepSay(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsprintf(gGrepMsgText, fmt, ap);
    va_end(ap);
    if (gGrepMsg) { gGrepMsg->label(gGrepMsgText); gGrepMsg->redraw(); }
}

static void grepFill(void)
{
    const char *roots[CODE_MAX_FOLDERS];
    char row[800];
    int i, nr = 0;

    /* Once for the whole list, not once per row: this walks the tree. */
    if (gGrepAll && gGrepAll->value()) nr = grepRoots(roots, CODE_MAX_FOLDERS);
    gGrepList->clear();
    for (i = 0; i < gGrep.n; i++) {
        sprintf(row, "%.300s:%d:  %.400s", grepRel(gGrep.hit[i].path, roots, nr),
                gGrep.hit[i].lineNo, gGrep.hit[i].line);
        gGrepList->add(row);
    }
    if (!gGrep.n)
        grepSay("No matches.");
    else
        grepSay("%d match%s in %d file%s%s.", gGrep.n, gGrep.n == 1 ? "" : "es",
                gGrep.files, gGrep.files == 1 ? "" : "s",
                gGrep.truncated ? " -- list truncated" : "");
}

/* The one place a search actually runs -- Find and the re-run after Replace
 * All both come through here, so the two can never disagree about what was
 * searched. Returns how many folders were walked.
 *
 * All stops the moment the cap is reached: edit_grep.h measures maxHits
 * against the running total, so once `truncated` is set every further folder
 * could only add to a list that is already short of the truth -- and walking
 * them would cost minutes on a share for nothing. */
static int grepRunNow(const char *needle, int matchCase)
{
    const char *roots[CODE_MAX_FOLDERS];
    int n, k;

    grepSay("Searching...");
    Fl::flush();                       /* the only progress there is: say so
                                          before a walk that can take a while
                                          on a PII over a share */
    if (!gGrepAll->value()) {
        codeGrepRun(&gGrep, gGrepRoot, needle, matchCase, 1, GREP_MAX_HITS);
        return 1;
    }
    n = grepRoots(roots, CODE_MAX_FOLDERS);
    codeGrepFree(&gGrep);              /* frees and re-initialises */
    for (k = 0; k < n; k++) {
        if (gGrep.truncated) return k;
        codeGrepAdd(&gGrep, roots[k], needle, matchCase, 1, GREP_MAX_HITS);
    }
    return n;
}

static void cbGrepFind(Fl_Widget *, void *)
{
    const char *needle = gGrepFind->value();
    const char *dir    = grepFolder();

    if (!dir)            { grepSay("No folder in the sidebar."); return; }
    if (!needle || !*needle) { grepSay("Nothing to find."); return; }
    copyStr(gGrepRoot, (int)sizeof(gGrepRoot), dir);
    grepRunNow(needle, gGrepCase->value());
    grepFill();
}

/* Double-click a result: open that file and put the caret on the line. */
static void cbGrepList(Fl_Widget *, void *)
{
    int row = gGrepList->value(), i;
    CodeEditor *ed;

    if (Fl::event_clicks() < 1) return;          /* single click: just select */
    Fl::event_clicks(0);
    if (row < 1 || row > gGrep.n) return;
    if (!openOrShow(gGrep.hit[row - 1].path)) return;
    i = currentIndex();
    if (i < 0) return;
    ed = gDocs[i].ed;
    ed->insert_position(codeGotoLinePos(ed->buffer(), gGrep.hit[row - 1].lineNo));
    ed->buffer()->unselect();
    ed->show_insert_position();
    ed->take_focus();
    ed->redraw();
}

/* Replace in one file that is NOT open in a tab: on disk, keeping the
   encoding and line ending codeReadText() found. Returns the count. */
static int grepReplaceOnDisk(const char *path, const char *needle,
                             const char *repl, int matchCase)
{
    int enc, crlf, trunc, n = 0;
    char *text = codeReadText(path, &enc, &crlf, &trunc);
    char *out;
    if (!text) return 0;
    /* Only part of the file came back (a NUL byte in it), so writing the
       result would drop the rest. The candidate list is filtered by
       codeFileOpenableName() and should never offer such a file, which is
       exactly why this costs nothing to refuse. */
    if (trunc) { free(text); return 0; }
    out = codeGrepReplace(text, needle, repl, matchCase, &n);
    free(text);
    if (!out) return 0;
    if (codeWriteText(path, out, (int)strlen(out), enc, crlf) != 0) n = 0;
    free(out);
    return n;
}

static void cbGrepReplaceAll(Fl_Widget *, void *)
{
    const char *needle = gGrepFind->value();
    const char *repl   = gGrepRepl->value();
    int matchCase = gGrepCase->value();
    int i, k, total = 0, touched = 0;

    if (!gGrep.n)            { grepSay("Nothing to replace -- run Find first."); return; }
    if (!needle || !*needle) return;
    if (gGrep.truncated) {
        grepSay("Too many matches to replace safely. Narrow the search.");
        return;
    }
    /* The only brake there is: a file that is not open in a tab is rewritten
       on disk with no undo behind it. With All ticked the files can be spread
       over projects the user has not looked at in weeks, so the question says
       how many folders they are in -- the count of folders that actually HAVE
       hits, not of folders searched. */
    if (gGrepAll->value()) {
        const char *roots[CODE_MAX_FOLDERS];
        int nr = grepRoots(roots, CODE_MAX_FOLDERS), nf = 0;
        for (k = 0; k < nr; k++)
            for (i = 0; i < gGrep.n; i++)
                if (codeFileUnder(gGrep.hit[i].path, roots[k])) { nf++; break; }
        if (fl_choice("Replace %d occurrence%s in %d file%s, across %d folder%s?",
                      "Cancel", "Replace", 0,
                      gGrep.n, gGrep.n == 1 ? "" : "s",
                      gGrep.files, gGrep.files == 1 ? "" : "s",
                      nf, nf == 1 ? "" : "s") != 1)
            return;
    } else if (fl_choice("Replace %d occurrence%s in %d file%s?",
                         "Cancel", "Replace", 0,
                         gGrep.n, gGrep.n == 1 ? "" : "s",
                         gGrep.files, gGrep.files == 1 ? "" : "s") != 1)
        return;

    for (i = 0; i < gGrep.n; i++) {
        const char *path = gGrep.hit[i].path;
        int done = 0, open = -1;
        /* One pass per FILE: the hit list has an entry per match, so skip
           any path already dealt with. */
        for (k = 0; k < i; k++)
            if (codePathEq(gGrep.hit[k].path, path)) { done = 1; break; }
        if (done) continue;

        for (k = 0; k < gDocCount; k++)
            if (codePathEq(gDocs[k].path, path)) { open = k; break; }

        if (open >= 0) {
            /* Open in a tab: through the buffer, so it joins that tab's undo
               and stays unsaved until the user decides. */
            CodeEditor *ed = gDocs[open].ed;
            ed->beginUndoGroup();
            total += codeReplaceAll(ed->buffer(), needle, repl, matchCase);
            ed->endUndoGroup();
            ed->redraw();
            refreshLabel(open);
        } else {
            total += grepReplaceOnDisk(path, needle, repl, matchCase);
        }
        touched++;
    }

    /* Re-run, so the list shows what is true now rather than what was. */
    grepRunNow(needle, matchCase);
    grepFill();
    grepSay("Replaced %d occurrence%s in %d file%s.", total, total == 1 ? "" : "s",
            touched, touched == 1 ? "" : "s");
}

/* The title and the message line are the only things that say WHERE a search
 * will run, so they have to follow the All tick immediately -- not at the
 * next Find, by which time the walk has already happened. */
static void grepSayWhere(void)
{
    const char *roots[CODE_MAX_FOLDERS];
    char title[600];
    int nr;

    if (!gGrepWin) return;
    if (gGrepAll->value()) {
        nr = grepRoots(roots, CODE_MAX_FOLDERS);
        gGrepWin->copy_label("Find in Folder -- all folders");
        if (nr < 1) grepSay("No folders in the sidebar.");
        else        grepSay("All folders in the sidebar -- %d to search.", nr);
        return;
    }
    if (!gGrepRoot[0]) { grepSay("Add a folder to the sidebar first."); return; }
    sprintf(title, "Find in Folder -- %.500s", baseName(gGrepRoot));
    gGrepWin->copy_label(title);
    grepSay("Folder: %.48s", baseName(gGrepRoot));
}

static void cbGrepAll(Fl_Widget *, void *) { grepSayWhere(); }

static void cbGrepClose(Fl_Widget *, void *) { if (gGrepWin) gGrepWin->hide(); }

static void cbFindInFolder(Fl_Widget *, void *)
{
    const char *dir = grepFolder();

    codeGrepOpenText = grepOpenText;
    if (!gGrepWin) {
        gGrepWin = new Fl_Double_Window(560, 360, "Find in Folder");
        gGrepWin->begin();
        gGrepFind = new Fl_Input(75, 10, 300, 24, "Find:");
        gGrepFind->when(FL_WHEN_ENTER_KEY_ALWAYS);      /* Enter = Find */
        gGrepFind->callback(cbGrepFind);
        gGrepCase = new Fl_Check_Button(385, 10, 110, 24, "Match case");
        gGrepRepl = new Fl_Input(75, 40, 300, 24, "Replace:");
        /* Under Match case, beside Replace: the two switches that change what
           a Find does, in the one place the eye already goes. */
        gGrepAll  = new Fl_Check_Button(385, 40, 160, 24, "All folders");
        gGrepAll->callback(cbGrepAll);
        gGrepAll->tooltip("Search every folder listed in the sidebar, "
                          "not just the current one");
        gGrepList = new Fl_Hold_Browser(10, 74, 540, 220);
        gGrepList->callback(cbGrepList);
        /* Full width: FLTK does not clip a label to its widget, so a
           message wider than the box paints over whatever is beside it and
           survives the next redraw, which only repaints the box itself. */
        gGrepMsg  = new Fl_Box(10, 300, 540, 22, gGrepMsgText);
        gGrepMsg->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        { Fl_Button *b;
          b = new Fl_Button( 10, 326,  90, 24, "Find");        b->callback(cbGrepFind);
          b = new Fl_Button(105, 326, 110, 24, "Replace All");  b->callback(cbGrepReplaceAll);
          b = new Fl_Button(470, 326,  80, 24, "Close");        b->callback(cbGrepClose);
        }
        gGrepWin->end();
        editDpiScaleTree(gGrepWin);
        gGrepWin->callback(cbGrepClose);
    }
    if (dir) copyStr(gGrepRoot, (int)sizeof(gGrepRoot), dir);
    else     gGrepRoot[0] = '\0';
    grepSayWhere();
    if (!gGrepWin->shown()) editPlaceDialog(gGrepWin);
    gGrepWin->show();
    gGrepFind->take_focus();
}

static Fl_Menu_Item gMenu[] = {
    { "&File", 0, 0, 0, FL_SUBMENU },
        { "&New",        FL_CTRL + 'n', cbNew    },
        { "&Open...",    FL_CTRL + 'o', cbOpen   },
        { "&Reload",     FL_CTRL + 'r', cbReload },
        { "&Save",       FL_CTRL + 's', cbSave   },
        { "Save &As...", 0,             cbSaveAs, 0, FL_MENU_DIVIDER },
        { "Open &Folder...", 0,         cbOpenFolder },
        { "Remove Folder",   0,         cbRemoveFolder, 0, FL_MENU_DIVIDER },
        { "Recent &Files", 0, 0, 0, FL_SUBMENU | FL_MENU_DIVIDER },
            { gRecentLabel[0], 0, cbRecent, (void *)0, FL_MENU_INVISIBLE },
            { gRecentLabel[1], 0, cbRecent, (void *)1, FL_MENU_INVISIBLE },
            { gRecentLabel[2], 0, cbRecent, (void *)2, FL_MENU_INVISIBLE },
            { gRecentLabel[3], 0, cbRecent, (void *)3, FL_MENU_INVISIBLE },
            { gRecentLabel[4], 0, cbRecent, (void *)4, FL_MENU_INVISIBLE },
            { gRecentLabel[5], 0, cbRecent, (void *)5, FL_MENU_INVISIBLE },
            { gRecentLabel[6], 0, cbRecent, (void *)6, FL_MENU_INVISIBLE },
            { gRecentLabel[7], 0, cbRecent, (void *)7, FL_MENU_INVISIBLE },
            { "(none)",        0, cbRecentNone, 0, FL_MENU_INACTIVE },
            { 0 },
        { "&Close",      FL_CTRL + 'w', cbClose  },
        { "E&xit",       FL_CTRL + 'q', cbExit   },
        { 0 },
    { "&Edit", 0, 0, 0, FL_SUBMENU },
        /* CodeEditor::handle() intercepts these first; the accelerators are
         * here so the menu documents them. Both routes call the same undo(). */
        { "&Undo", FL_CTRL + 'z', cbUndo },
        { "&Redo", FL_CTRL + 'y', cbRedo, 0, FL_MENU_DIVIDER },
        { "&Find...",     FL_CTRL + 'f', cbFind     },
        { "Find &Next",   FL_F + 3,      cbFindNext },
        { "&Replace...",  FL_CTRL + 'h', cbReplace  },
        /* Ctrl+Shift+F. Fl::test_shortcut() rejects a shortcut whose SHIFT
           is required but absent, so this cannot be reached by a plain
           Ctrl+F -- unlike the reverse, which is why View uses Alt+Z. */
        { "Find in &Folder...", FL_CTRL + FL_SHIFT + 'f', cbFindInFolder },
        { "&Go to line...", FL_CTRL + 'g', cbGotoLine, 0, FL_MENU_DIVIDER },
        { "Se&ttings...", 0, cbSettings },
        { 0 },
    { "&View", 0, 0, 0, FL_SUBMENU },
        /* Alt+Z, not Ctrl+Shift+W: Fl::test_shortcut() only requires META/ALT/
         * CTRL to match exactly and treats SHIFT loosely, so a Ctrl+Shift+W
         * binding lives dangerously next to Ctrl+W (Close). Alt is strict. */
        { "&Word Wrap", FL_ALT + 'z', cbWrap },
        { "Wrap at &Column...", 0, cbWrapCol, 0, FL_MENU_DIVIDER },
        /* No accelerator: Alt+F/E/V/T/N/L are the menu-bar titles and
           Alt+Z / Alt+J are taken. */
        { "&Folders", 0, cbFolders },
        { "&Highlight Word at Caret", 0, cbHighlightWord },
        { 0 },
    { "&Tools", 0, 0, 0, FL_SUBMENU },
        { "Format &JSON", FL_ALT + 'j', cbFormatJson },
        { 0 },
    { "E&ncoding", 0, 0, 0, FL_SUBMENU },
        { "UTF-8",          0, cbEnc, (void *)CODE_ENC_UTF8 },
        { "UTF-8 with BOM", 0, cbEnc, (void *)CODE_ENC_UTF8_BOM },
        { gAnsiLabel,       0, cbEnc, (void *)CODE_ENC_ANSI, FL_MENU_DIVIDER },
        { "Windows (CRLF)", 0, cbEol, (void *)1 },
        { "Unix (LF)",      0, cbEol, (void *)0 },
        { 0 },
    { "&Language", 0, 0, 0, FL_SUBMENU },
        { "Plain text", 0, cbLang, (void *)LEX_LANG_TEXT, FL_MENU_DIVIDER },
        { "Bash",       0, cbLang, (void *)LEX_LANG_BASH },
        { "Batch",      0, cbLang, (void *)LEX_LANG_BAT },
        { "C / C++",    0, cbLang, (void *)LEX_LANG_C },
        { "CSS",        0, cbLang, (void *)LEX_LANG_CSS },
        { "HTML",       0, cbLang, (void *)LEX_LANG_HTML },
        { "INI",        0, cbLang, (void *)LEX_LANG_INI },
        { "Java",       0, cbLang, (void *)LEX_LANG_JAVA },
        { "JavaScript", 0, cbLang, (void *)LEX_LANG_JS },
        { "JSON",       0, cbLang, (void *)LEX_LANG_JSON },
        { "Lua",        0, cbLang, (void *)LEX_LANG_LUA },
        { "Markdown",   0, cbLang, (void *)LEX_LANG_MARKDOWN },
        { "Pascal",     0, cbLang, (void *)LEX_LANG_PASCAL },
        { "PHP",        0, cbLang, (void *)LEX_LANG_PHP },
        { "Python",     0, cbLang, (void *)LEX_LANG_PYTHON },
        { "SQL",        0, cbLang, (void *)LEX_LANG_SQL },
        { "XML",        0, cbLang, (void *)LEX_LANG_XML },
        { 0 },
    { 0 }
};

/* ---- main -------------------------------------------------------------- */

static void flMsg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    msg("FLTK: %s\n", buf);
}

/* ---- single instance ---------------------------------------------------
 * Starting codeedit while one is already running hands the file names to the
 * running one and exits, so Total Commander's F4 on five files gives five
 * tabs in one window, not five windows. Win98-safe throughout.
 *
 *   detect   A named mutex (CreateMutexA). The first instance keeps it for
 *            its lifetime; Windows releases it at exit.
 *   find     The main window has its own class (CODE_IPC_CLASS) and a window
 *            property (CODE_IPC_PROP). The PROPERTY is what identifies it:
 *            FLTK 1.3 gives every later window the first window's class, so
 *            the Find and Settings dialogs share CODE_IPC_CLASS.
 *   send     One WM_COPYDATA per file, with the FULL path -- the two
 *            processes need not share a working directory. An empty one
 *            just raises the window.
 *   receive  FLTK 1.3 passes messages it does not handle to Fl::add_handler
 *            as event 0, with the message in fl_msg (Fl_win32.cxx, WndProc's
 *            default branch). The data only lives for the SendMessage, so the
 *            paths are copied out at once and opened from a zero-delay
 *            timeout, outside the window procedure.
 *   focus    Windows lets only the foreground process take the foreground,
 *            and the new instance is that process (the user just started it),
 *            so the SENDER restores and raises the window. It also lets the
 *            receiver do it (AllowSetForegroundWindow: 2000 / Me and later,
 *            looked up at runtime).
 *
 * A second instance that finds no window within 3 s -- the first one still
 * starting up, or shutting down -- runs on its own instead. */
#ifdef _WIN32
#define CODE_IPC_MUTEX "SOOB-Code.codeedit.instance"
#define CODE_IPC_CLASS "SOOBCodeEdit"
#define CODE_IPC_PROP  "SOOBCodeEditMain"
#define CODE_IPC_MAGIC 0x534F4F42UL        /* 'SOOB': our WM_COPYDATA */
#define CODE_IPC_MAXQ  64

static HANDLE gIpcMutex;                   /* first instance: held until exit */
static HWND   gIpcFound;
static char  *gIpcQueue[CODE_IPC_MAXQ];    /* paths received, not yet opened */
static int    gIpcCount;

/* Full path for a command-line argument, resolved against OUR directory. */
static void ipcFullPath(const char *in, char *out, int len)
{
    char *file;
    DWORD n = GetFullPathNameA(in, (DWORD)len, out, &file);
    if (n == 0 || n >= (DWORD)len) copyStr(out, len, in);
}

static BOOL CALLBACK ipcEnum(HWND h, LPARAM l)
{
    char cls[64];
    (void)l;
    if (GetClassNameA(h, cls, (int)sizeof(cls)) && strcmp(cls, CODE_IPC_CLASS) == 0 &&
        GetPropA(h, CODE_IPC_PROP)) {
        gIpcFound = h;
        return FALSE;
    }
    return TRUE;
}

static HWND ipcFindMain(void)
{
    gIpcFound = 0;
    EnumWindows(ipcEnum, 0);
    return gIpcFound;
}

/* path == 0: just "come to the front". */
static int ipcSend(HWND h, const char *path)
{
    COPYDATASTRUCT cd;
    DWORD res = 0;              /* DWORD_PTR is the same type on 32-bit */
    cd.dwData = CODE_IPC_MAGIC;
    cd.cbData = path ? (DWORD)strlen(path) + 1 : 0;
    cd.lpData = (void *)path;
    return SendMessageTimeoutA(h, WM_COPYDATA, 0, (LPARAM)&cd,
                               SMTO_ABORTIFHUNG, 5000, &res) != 0;
}

/* Second instance: hand our files to the running one. 1 = done, exit. */
static int ipcHandOff(int argc, char **argv)
{
    HWND h = 0;
    int i, tries, sent = 0;
    char full[600];

    gIpcMutex = CreateMutexA(NULL, FALSE, CODE_IPC_MUTEX);
    if (!gIpcMutex || GetLastError() != ERROR_ALREADY_EXISTS) return 0;
    for (tries = 0; tries < 30 && !(h = ipcFindMain()); tries++) Sleep(100);
    if (!h) return 0;

    {   /* let the running instance raise itself, where Windows has the call */
        typedef BOOL (WINAPI *AllowFn)(DWORD);
        AllowFn allow = (AllowFn)GetProcAddress(GetModuleHandleA("user32.dll"),
                                                "AllowSetForegroundWindow");
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (allow && pid) allow(pid);
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-trace") == 0) continue;
        ipcFullPath(argv[i], full, (int)sizeof(full));
        if (!ipcSend(h, full)) {
            if (!sent) return 0;           /* not answering: run on our own */
            break;
        }
        sent++;
    }
    if (!sent) ipcSend(h, 0);
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    SetForegroundWindow(h);
    return 1;
}

/* Closing: stop being a hand-off target, so a codeedit started while we shut
 * down runs on its own instead of sending files to a dying window. */
static void ipcUnregister(void)
{
    if (gWin && gWin->shown()) RemovePropA(fl_xid(gWin), CODE_IPC_PROP);
}

static void ipcRaise(void)
{
    HWND h;
    if (!gWin || !gWin->shown()) return;
    h = fl_xid(gWin);
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    SetForegroundWindow(h);
}

static void ipcOpenQueued(void *)
{
    int k, n = gIpcCount;
    gIpcCount = 0;
    for (k = 0; k < n; k++) {
        openOrShow(gIpcQueue[k]);
        free(gIpcQueue[k]);
    }
    ipcRaise();
}

/* First instance: receive. Called for every message FLTK does not handle,
 * so the test for ours comes first and is cheap. */
static int ipcHandler(int ev)
{
    COPYDATASTRUCT *cd;
    if (ev != 0 || fl_msg.message != WM_COPYDATA) return 0;
    cd = (COPYDATASTRUCT *)fl_msg.lParam;
    if (!cd || cd->dwData != CODE_IPC_MAGIC) return 0;
    if (cd->cbData > 1 && cd->lpData && gIpcCount < CODE_IPC_MAXQ) {
        char *p = (char *)malloc(cd->cbData + 1);
        if (p) {
            memcpy(p, cd->lpData, cd->cbData);
            p[cd->cbData] = '\0';
            gIpcQueue[gIpcCount++] = p;
        }
    }
    Fl::remove_timeout(ipcOpenQueued);
    Fl::add_timeout(0.0, ipcOpenQueued);
    return 1;
}
#endif

int main(int argc, char **argv)
{
    int i;

#ifdef _WIN32
    /* Before anything else: if codeedit is already running, give it our
     * files and leave (see "single instance" above). */
    if (ipcHandOff(argc, argv)) return 0;
#endif

    /* First: DPI awareness must be declared before FLTK opens the display.
     * Widget font is 12 px at 96 DPI (FLTK default is 14), scaled for the
     * display -- same as the level editor. */
    editDpiInit(12);
    codeSettingsLoad(&gSet);
    codeIndent      = gSet.tabWidth;
    codeUseTabs     = gSet.useTabs;
    codeLineNumbers = gSet.lineNumbers;
    codeOccurs      = gSet.highlightWord;
    codeSetFontSize(editDpi(gSet.fontSize));
#ifdef _WIN32
    sprintf(gAnsiLabel, "ANSI (code page %u)", (unsigned)GetACP());
#endif
#ifdef _WIN32
    /* Code face: Consolas on Vista+ -- heavier and clearer than Courier New at
     * the same size. CODE_FONT is FL_COURIER, so remapping that slot switches
     * the text, the gutter and the wrap-column maths together. Monospaced in
     * all four styles, as the style table requires. Win98 keeps Courier New. */
    if (editDpiAware) {
        Fl::set_font(FL_COURIER,             " Consolas");
        Fl::set_font(FL_COURIER_BOLD,        "BConsolas");
        Fl::set_font(FL_COURIER_ITALIC,      "IConsolas");
        Fl::set_font(FL_COURIER_BOLD_ITALIC, "PConsolas");
    }
#endif

    Fl::error = flMsg;
    Fl::warning = flMsg;
    Fl::scheme("none");                 /* plain FLTK drawing: cheapest on a PII */

    /* -trace writes a step-by-step codeedit.log next to the exe, flushed per
     * line so a hard crash still leaves the last one. Off by default. */
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-trace") == 0) codeTraceOn = 1;
    if (codeTraceOn) {
        remove("codeedit.log");
        codeTrace("codeedit: start");
    }
    /* ABI sanity. fltk98.bat skips any source whose .o already exists, so a
     * libfltk.a built before the FL_ABI_VERSION 10304 change would disagree
     * with this object about sizeof(Fl_Text_Display) -- silent memory
     * corruption that typically only bites on destruction. */
    codeTrace("abi: codeedit.o sees FL_ABI_VERSION=%d sizeof(Fl_Text_Display)=%d"
              " sizeof(CodeEditor)=%d",
              (int)FL_ABI_VERSION, (int)sizeof(Fl_Text_Display), (int)sizeof(CodeEditor));

    /* Static: Fl_Window::label() stores the pointer without copying. */
    static char winTitle[160];
    sprintf(winTitle, "SOOB Code Editor  v%s  (built %s %s)",
            CODEEDIT_VERSION, __DATE__, __TIME__);

    gWin = new Fl_Double_Window(760, 560, winTitle);
    gWin->color(CODE_COL_BG);
    /* Every dialog -- ours and fl_alert / fl_choice -- opens centred over
     * this window and on screen (edit_place.h). */
    editPlaceOwner = gWin;
    fl_message_place(editPlaceDialog);
    gWin->begin();
    {
        gMenuBar = new EditMenuBar(0, 0, 760, MENU_H);
        gMenuBar->menu(gMenu);
        editMenuPad(gMenuBar);
        /* Office 97 look: a 1px white / dark gray band, the open title pushed
         * in with a 1px border, 3D-framed drop-downs with blue highlights,
         * 11px labels. Before editDpiScaleTree(), which is why the size is
         * scaled by hand here -- textsize() is not geometry, so the tree walk
         * does not touch it. */
        editMenuBarStyle(gMenuBar, editDpi(11));
        /* The editors' and tabs' right-click menus get the same items and,
         * through codeContextLook, the bar's frame and font size. */
        editMenuPadLevel(codeContextMenu);
        editMenuPadLevel(gTabMenu);
        editMenuPadLevel(gFoldMenu);
        codeContextLook = gMenuBar;

        /* The sidebar's slot is left empty here: Fl_Tree lays out its own
         * scrollbars, which is exactly what editDpiScaleTree() must not walk,
         * so it is built after the walk at already-scaled coordinates -- the
         * same rule that puts every CodeEditor there. The splitter is a plain
         * widget and scales fine. */
        gSplit = new CodeSplitter(FOLD_W_DEF, MENU_H,
                                  SPLIT_W, 560 - MENU_H - STATUS_H);
        gSplit->color(CODE_COL_TABROW);
        gSplit->selection_color(CODE_COL_SEL);
        gSplit->splitCallback(cbSplitDrag, 0);

        gTabs = new CodeTabs(FOLD_W_DEF + SPLIT_W, MENU_H,
                             760 - FOLD_W_DEF - SPLIT_W, 560 - MENU_H - STATUS_H);
        /* Flat, borderless tabs (edit_tabs.h): the row strip in color(), the
         * active tab in selection_color() -- the editor background, so it
         * merges into the page below -- with a white label. Inactive tabs are
         * bare captions in the child's labelcolor(). */
        gTabs->color(CODE_COL_TABROW);
        gTabs->selection_color(CODE_COL_BG);
        gTabs->labelcolor(CODE_COL_FG);
        gTabs->firstTabAt(codeLineNumbers ? codeGutterWidth() : 0);  /* tabs start where the gutter ends */
        gTabs->closeCallback(cbTabClose, 0);
        gTabs->menuCallback(cbTabMenu, 0);
        gTabs->end();

        gStatus = new CodeStatus(0, 560 - STATUS_H, 760, STATUS_H);
        gStatus->color(CODE_COL_TABROW);
        gStatus->labelcolor(CODE_COL_TAB_OFF);
    }
    gWin->end();
    editDpiScaleTree(gWin);             /* before addDoc: editors are added pre-scaled */

    /* The tree, at already-scaled coordinates -- see the note in the block
       above for why it cannot be built with the rest. */
    gSideW    = gSet.foldersW > 0 ? gSet.foldersW : editDpi(FOLD_W_DEF);
    gSideShow = gSet.foldersShow;
    gWin->begin();
    gTree = new CodeFolders(0, editDpi(MENU_H), gSideW,
                            gWin->h() - editDpi(MENU_H) - editDpi(STATUS_H));
    gWin->end();
    codeFoldersTheme(gTree);            /* before the first addRoot(): items
                                           copy the item colours at birth */
    gTree->menuCallback(cbFoldMenu, 0);
    gTree->activateCallback(cbFoldActivate, 0);
    gTree->callback(cbFoldTree, 0);

    gWin->resizable(gTabs);
    /* Without a floor the window can be dragged narrower than sidebar plus
       splitter, and layoutSide() then hands the tabs a negative width. */
    gWin->size_range(editDpi(FOLD_W_MIN + SPLIT_W + TABS_W_MIN), editDpi(220));
    gWin->callback(cbWinClose);         /* the X button must ask about unsaved work */
    syncRecentItems();

    /* Last window rectangle -- the position only if its title bar would land
     * on a screen (the monitor may be gone), the size either way. */
    if (gSet.rememberWin && gSet.winW >= 200 && gSet.winH >= 150) {
        int sx, sy, sw, sh, cx = gSet.winX + gSet.winW / 2, cy = gSet.winY + 10;
        Fl::screen_xywh(sx, sy, sw, sh, cx, cy);
        if (cx >= sx && cx < sx + sw && cy >= sy && cy < sy + sh)
            gWin->resize(gSet.winX, gSet.winY, gSet.winW, gSet.winH);
        else
            gWin->size(gSet.winW, gSet.winH);
    }

    {   /* The listed folders. Roots stay collapsed, so this is one cheap
           item each and not a recursive walk of the disk at startup. */
        int k;
        for (k = 0; k < gSet.nFolders; k++) gTree->addRoot(gSet.folders[k]);
    }
    layoutSide();                       /* after the saved size is applied */
    syncFoldersItem();
    syncHighlightItem();

    /* The desk as it was left. Before the command line, so a file passed
       there lands ON TOP of the restored tabs and ends up the active one --
       opening codeedit on a file must still show you that file. */
    if (gSet.restoreSession) sessionRestore();

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-trace") == 0) continue;
#ifdef _WIN32
        {   /* full paths, the same form a second instance hands over, so the
             * "already open?" test in openOrShow() matches */
            char full[600];
            ipcFullPath(argv[i], full, (int)sizeof(full));
            addDoc(full);
        }
#else
        addDoc(argv[i]);
#endif
    }
    if (gDocCount == 0) addDoc(0);

    /* If libfltk.a is stale (ABI 10300) its linenumber_bgcolor() setter is a
     * no-op and the getter returns a hard-coded 53. Our value surviving proves
     * the library really was rebuilt at 10304. */
    if (gDocCount > 0) {
        /* Runs even without -trace: a mixed-ABI libfltk.a is silent until it
         * crashes in a destructor, so it is worth checking every run. */
        unsigned got = (unsigned)gDocs[0].ed->linenumber_bgcolor();
        codeTrace("abi: linenumber_bgcolor()=0x%08x expected=0x%08x -> libfltk is %s",
                  got, (unsigned)CODE_COL_GUTTER,
                  got == (unsigned)CODE_COL_GUTTER ? "10304 (good)"
                                                   : "STALE 10300 -- CLEAN REBUILD NEEDED");
        if (got != (unsigned)CODE_COL_GUTTER)
            fl_alert("libfltk.a is STALE.\n\n"
                     "It was built before FL_ABI_VERSION became 10304, so it\n"
                     "disagrees with this program about sizeof(Fl_Text_Display).\n\n"
                     "Fix, in SOOB-Core: delete vendor\\fltk-1.3\\FL\\lib\n"
                     "and raw\\obj\\fltk, run fltk98 (build_fltk_win10 for\n"
                     "the Win10 build), then e98 / e10 here again.");
    }

    Fl::add_timeout(0.4, labelTimer);   /* keeps the " *" dirty marks current */
    Fl::add_check(statusCheck, 0);      /* status bar: after every event batch */

    /* Build stamp + the actual menu the RUNNING binary has. If "&View" is not
     * in this list, the exe is stale -- e98.bat always recompiles, so that
     * means it was not run, or it failed and the old exe is still there. */
    msg("codeedit: v%s built %s %s\n", CODEEDIT_VERSION, __DATE__, __TIME__);
    {
        int mi, n = gMenuBar->size();
        msg("codeedit: menu has %d entries:", n);
        for (mi = 0; mi < n; mi++)
            msg(" %s", gMenu[mi].text ? gMenu[mi].text : "|");
        msg("\n");
    }

#ifdef _WIN32
    gWin->xclass(CODE_IPC_CLASS);       /* single instance: see above */
#endif
    gWin->show();
#ifdef _WIN32
    SetPropA(fl_xid(gWin), CODE_IPC_PROP, (HANDLE)1);
    Fl::add_handler(ipcHandler);
    /* After show(): fl_xid() only has a window to return once it is mapped.
     * Gives the comdlg32 Open / Save dialogs an owner, so they are modal to
     * us instead of to the desktop -- see edit_filedlg.h. */
    codeFileDlgSetOwner((void *)fl_xid(gWin));
#endif
    msg("codeedit: %d document(s) open\n", gDocCount);
    return Fl::run();
}
