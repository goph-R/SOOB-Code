# Feature requests

All of the original list is implemented. 1.2 is the app chrome, 1.3 the
editing behaviour.

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

## Ideas not asked for yet

- Pascal `begin` / `end` block indent: one row in `codeBlockWordTable`
