# Feature requests

All of the original list is implemented. 1.2 is the app chrome, 1.3 the
editing behaviour, 1.4 adds two languages.

## Done in 1.2

- [x] Find: show a label if not found anything
- [x] Store the last folder for the open and save dialog (start there), the
      same folder for both  (`last_dir` in codeedit.ini)
- [x] The dialogs are behaving stange, make the modal if possible
      (comdlg32 `hwndOwner`; `Go to line` / `Wrap at Column` are now own
      modal dialogs instead of `fl_input`)
- [x] `Go to line` input value should be selected in default
- [x] IE5 menu style: 1px dark gray + white groove top and bottom, 11px font

## Done in 1.3

- [x] Find: scroll to the center of the view on find
- [x] Home first move the caret to the first non whitespace character, for
      second Home jump to the first character
- [x] Do indent next line for `[`, `(`, `then` (lua) too as for `{`
      (Lua: net `function` / `then` / `do` / `repeat`, or a trailing `else`)
- [x] Do back indent for `]`, `)` and `end` (lua) as for `}`
      (Lua: `end`, `until`, and `else` / `elseif` line up with their `if`)
- [x] On paste use the indent if needed

The indent rules live in SOOB-Core `fltk_ui/edit_indent.h` as buffer-only
functions and are covered by `tools/test_linux.sh`, so they can be changed
without testing by hand on Win98.

## Done in 1.4

- [x] .bat and .sh support (Batch, Bash)
      Bash is a `LexCf` config + a new `shell` flag; Batch needed its own
      lexer (`rem` / `::` comments, `%VAR%`, `:label`). Neither costs a
      carry state.

## Fixed

- [x] Changing the word wrap marked the file as modified. Any OK in the
      Settings dialog did it, to every open document: Fl_Text_Buffer's
      tab_distance() reports a whole-buffer edit so displays re-layout, and
      it fires even when the value has not changed. It also pushed a junk
      undo step. SOOB-Core edit_code.h.

## Done in 1.5

- [x] Pascal `begin` / `end` indent. Needed a fourth word category, `hint`:
      Pascal's `then` / `do` govern ONE statement and are closed by nothing,
      so counting them as openers made a later `end` align to the nearest
      dangling `then` instead of its `begin`. `else` is a hint too, not a
      re-indent trigger -- aligning it with its `if` would need `if` tracked,
      and `if` has no closer.
- [x] Strip trailing whitespace on save, `trim_trailing` in codeedit.ini,
      on by default, with a Settings checkbox. Markdown keeps a two-space
      hard line break; a run containing a tab is not a break and goes.

## Ideas not asked for yet
- ```bash / ```bat fenced blocks in Markdown. NOT free: it would push
  `LEX_MD_NSUB` to 13 and the fence carry states into `LS_CF_BLOCK`, so
  every `LS_CF_*` would have to shift up and `LS_MAX` would land on 62 --
  exactly the ceiling, with no headroom left.
- Bash heredocs (`<<EOF`): the body is currently lexed as code. Needs a
  carry state plus the delimiter, so it is the one shell feature that is
  not cheap.
