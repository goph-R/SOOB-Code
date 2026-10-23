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

## Done in 1.17

- [x] Open as Text on the Folders tree's right-click menu, for a file whose
      extension the editor does not know -- an `.obj`, a `.dat`, a `.foo`.
      `codeFileOpenableName()` was the only thing keeping `codeReadText()`
      away from a 40 MB `.iso`, so going around it asks the two questions
      that list was answering by proxy: a new `codeFileSniff()` reads the
      size (above 1 MB it asks first) and looks for a NUL byte.
      A NUL matters more than it looks. `codeReadText()` compacts CRLF with
      a `for (; text[i]; i++)` walk, so the document STOPS at the first zero
      byte -- open a `.exe`, press Ctrl+S, and the first few hundred bytes
      are written back over the whole file. The guard therefore sits in the
      reader, not in the menu command: `codeReadText()` reports it,
      `CodeEditor::truncated()` carries it, and `addDoc()` arms the
      document, so Recent Files, the command line and a grep hit are covered
      too, not just the one entry point that knew to look. Such a document
      turns Save into Save As. Find in Folder's on-disk Replace All refuses
      a truncated read for the same reason -- unreachable today, since its
      candidates are filtered by the same list, and free to close.
- [x] ```bash, ```bat, ```ini, ```xml and ```json fenced blocks in Markdown
      -- the two entries below this section, which said it could not be
      done. It could, for a price: a fence has to remember which character
      opened it, so a sub-language cost TWO states, one for ``` and one for
      ~~~. Giving the tilde fence a single flat state (what ```` already
      had) turned `2 x 11` carry states into `16 + 1`, which paid for all
      five with room left: `LS_MAX` is 55 now, not 60, against the same
      ceiling of 62.
      The trade is that ~~~php no longer colours as PHP. Backticks are what
      documents are actually written with -- and the five tables also stop
      being function-local in `lexLangFromPath()`, so an extension and a
      fence tag finally mean the same thing for every language there is.

## Done in 1.18

- [x] Select in Folders, next to Copy absolute path on a tab's right-click
      menu: find the tab's file in the sidebar and select it. Both items
      answer the same question -- where does this file live -- one for
      another program, one in the tree.
      `CodeFolders::reveal()` walks down from the listed folder that holds
      the path, reading and opening each directory on the way, then scrolls
      the row into view. The prefix test is `codeFileUnder()`, so `C:\proj`
      cannot swallow `C:\project2\x.c`.
      It will not re-read a directory that is already loaded: a stale folder
      is what Refresh is for, and re-reading here would close whatever is
      open underneath -- a steep price for a command whose job is to show one
      row. So a file created since the folder was read is not found, and the
      message says so rather than just failing.
      Nothing is ever ADDED to the sidebar: "select it if the folders hold
      it" is the whole contract. The sidebar does come out if it was hidden,
      since the command means nothing otherwise, and the focus stays in the
      editor so typing on needs no click back.
      Deferred by one event loop turn, like a double-click in the tree:
      `show_item()` scrolls using the y each item got in the last draw, and
      when the sidebar has just come out of hiding those were never set.

## Done in 1.19

- [x] The word at the caret underlined wherever else it is on screen -- for
      following a variable by eye while scrolling. Asked for as "just string
      match", and that is what it is: `codeWordHits()` compares bytes, whole
      words only, exact spelling. No scoping, no declarations, nothing
      parsed.
      `codeWordAt()` decides whether there is a name there at all. It refuses
      a word starting with a digit (standing past `x = 0;` must not underline
      every 0), and a word whose style slot is `LEX_KEYWORD` or `LEX_TYPE` --
      the two slots a lexer gives to vocabulary rather than to names. That is
      what makes the veto per-language for free: `end` is a keyword in Lua
      and a name in C, and the style buffer already knows which. Builtins
      (`LEX_IDENT`) are deliberately allowed: `printf` is worth tracing.
      Both are buffer-only and tested headless.
      The widget half is about not doing work. The hits and their x / y are
      cached against (word, first visible char, last visible char, horizontal
      offset), so the caret blink -- twice a second, forever -- recomputes
      nothing. A scroll rescans inside `occDraw()`, where the whole text area
      is being repainted anyway, so it needs no damage of its own. Only a
      change of WORD pays for a redisplay, and then only over the old and new
      underlines. An edit shifts the recorded positions the way `matchShift()`
      does for the pair boxes.
      Drawn as an underline rather than an outline box: there can be dozens
      on screen, and a box each turns the view into a grid. The pair box
      keeps the outline, so the two never read as the same thing.
      Switchable from two places, which are one setting: View / Highlight
      Word at Caret for the file you are in the middle of, and the Settings
      checkbox where the other display options live. Both write
      `highlight_word` to the ini, so whichever was used last is what the
      next run starts with. Off has to actively erase what is on screen --
      `occUpdate()` returns at the door once the switch is off, so
      `occApply()` is the one that clears, from the same per-editor
      `applySettings()` the font size and the gutter already go through.

## Ideas not asked for yet
- A `-new` switch, or a Settings checkbox, for the odd time a second
  window is wanted.
- Bash heredocs (`<<EOF`): the body is currently lexed as code. Needs a
  carry state plus the delimiter, so it is the one shell feature that is
  not cheap.
- YAML: NOT wanted -- asked and declined, do not re-propose. Uncovered but
  rarer here, and unasked: asm, cs, kt, pl, cmake.
- XML `<![CDATA[ ... ]]>` spanning lines: the `<!` branch stops at the first
  `>`, so a multi-line CDATA block is only partly coloured. One carry state
  would fix it; 7 are still free (LS_MAX is 55, the ceiling is 62).
- Trim exemption for `vendor/`: saving an upstream FLTK source from the
  editor would strip its trailing whitespace (Fl_Text_Buffer.cxx alone has
  137 such lines). Only bites if you edit one, and Ctrl+Z undoes it.
