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
      (superseded in 1.7 -- the Office 97 look was what was wanted)

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

## Done in 1.6

- [x] INI and XML support. INI is its own small lexer: `[section]`, `key=`,
      and `;` / `#` comments only at the start of a line, which is what
      Windows' own GetPrivateProfileString honours -- an inline `;` belongs
      to the value. XML reuses the HTML lexer with an `xml` flag that
      suppresses the `<script>` / `<style>` islands (ordinary element names
      in XML) and colours `<?...?>` processing instructions.
- [x] JSON, plus extension aliases onto lexers that already existed:
      `.glsl` / `.vert` / `.frag` / `.inl` / `.incl` / `.rc` to C,
      `.ts` / `.tsx` to JavaScript, `.tmx` / `.qrc` / `.csproj` / `.props`
      to XML. The C and JS ones went into the shared fence-tag tables, so
      ```glsl and ```ts work in Markdown as a free side effect.
      JSON is its own lexer for one reason worth having: a quoted string
      followed by `:` is a KEY and gets its own colour. An unquoted key is
      flagged as an error, since in strict JSON it is one.

## Done in 1.7

- [x] Office 97 menu style, replacing the IE5 one: the band ruled 1px white
      at the top and dark gray at the bottom, the open title pushed in with
      a 1px sunken border on plain button face, drop-downs in a 3D button
      frame, items highlighting solid blue.

      The whole job is working around menuwindow taking its frame from the
      BAR's boxtype -- which is why the old custom bar box leaked into the
      popups and ruled them top and bottom instead of framing them. Leaving
      the bar FL_FLAT_BOX makes FLTK fall back to FL_UP_BOX for the popups
      on its own, and drops the title window's inset to 1px so the pressed
      border lands exactly between the band's two rules. The band is then
      drawn by `EditMenuBar::draw()`, and the pressed title by a labeltype
      (`down_box()` cannot serve both the title and the blue item fill).
      All in `fltk_ui/edit_menupad.h`.

## Done in 1.8

- [x] Word 97 menu metrics, measured off a screenshot of Word itself rather
      than guessed: 19px rows (the text height plus three), the icon column
      a square the height of the row with the text 25px in from the edge,
      separators etched 128-gray over white and inset 3px from both content
      edges, and a checked item marked with Word's pressed-toolbar-button
      square -- 1px sunken, a white/face checkerboard, the Win95 tick.

      FLTK draws FL_MENU_DIVIDER itself, after the label, so a labeltype
      cannot restyle it; `editMenuPad()` now clears the flag and hands the
      rule to the item below, which draws it in the leading above its own
      cell. Only those two rows are safe -- drawentry()'s erase clip reaches
      exactly to one row above them. The four item states (checked, ruled,
      both, neither) are one labeltype plus a two-bit code, since Fl_Label
      carries its own type.

      Not matched, and not matchable: Word gives a separator its own 10px
      row, FLTK has only the 4px leading, so the rule has less air (5/4 vs
      Word's 7/10). A dummy item would cost a full 19px row and land no
      closer.

## Done in 1.9

- [x] Win98-sized arrows. FLTK draws both of its arrow glyphs bigger than
      Windows 98 does: the scrollbar's is 9x5 in a 16px bar where PuTTY's
      on the same screen is 7x4, and the submenu arrow had just gone from
      4x7 to 5x9 because FLTK sizes it off the ROW height and 1.8 took the
      rows to 19px. Two one-line patches to the vendored FLTK, written up
      in `../SOOB-Core/docs/editor-fltk-win98.md`: the scrollbar divides by
      4 instead of 3, and the submenu arrow comes off the menu FONT (as
      Marlett does on Windows) instead of the row, so it stays 4x7 however
      tall the rows get.

      This one needs `fltk98` before `e98` -- but only two objects:
      `Fl_Scrollbar.o`, `Fl_Menu.o` and `fltkok.tag` out of
      `vendor\fltk-1.3\FL\lib`.

## Done in 1.10

- [x] Single instance: starting codeedit while one is running hands the
      file names to the running window (one tab each; a file that is
      already open just gets its tab selected), restores and raises it, and
      exits. So Total Commander's F4 on several files gives tabs, not
      windows. Win98-safe: a named mutex to detect, WM_COPYDATA with full
      paths to hand over (the two processes need not share a working
      directory), and the window is found by its class plus a window
      property -- FLTK reuses the first window's class for the dialogs too.
      The NEW instance raises the window, since Windows only lets the
      foreground process do that; on 2000 / Me and later it also allows the
      running one (AllowSetForegroundWindow, looked up at runtime).

## Ideas not asked for yet
- A `-new` switch, or a Settings checkbox, for the odd time a second
  window is wanted.
- ```bash / ```bat fenced blocks in Markdown. NOT free: it would push
  `LEX_MD_NSUB` to 13 and the fence carry states into `LS_CF_BLOCK`, so
  every `LS_CF_*` would have to shift up and `LS_MAX` would land on 62 --
  exactly the ceiling, with no headroom left.
- Bash heredocs (`<<EOF`): the body is currently lexed as code. Needs a
  carry state plus the delimiter, so it is the one shell feature that is
  not cheap.
- YAML: NOT wanted -- asked and declined, do not re-propose. Uncovered but
  rarer here, and unasked: asm, cs, kt, pl, cmake.
- ```json fences in Markdown: same carry-state wall as ```bash.
- XML `<![CDATA[ ... ]]>` spanning lines: the `<!` branch stops at the first
  `>`, so a multi-line CDATA block is only partly coloured. One carry state
  would fix it; 4 are still free (LS_MAX is 58, the ceiling is 62).
- Trim exemption for `vendor/`: saving an upstream FLTK source from the
  editor would strip its trailing whitespace (Fl_Text_Buffer.cxx alone has
  137 such lines). Only bites if you edit one, and Ctrl+Z undoes it.
