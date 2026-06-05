# Controls — keyboard, mouse, and touch

Every way to drive Nordstjernen from the keyboard, the mouse, and a
touchscreen. This is a living map of the actual bindings; the browser's
runtime behaviour is the source of truth. The keyboard accelerators are
registered in `src/main.c` (`ns_install_actions`), the in-page key
handling in `src/main.c` (`ns_on_drawing_key_pressed`), and the pointer
and touch gestures in `src/window.c` (`ns_window_build_content`).

**Modifier naming.** Shortcuts below are written with **Ctrl**, but the
accelerators use GTK's `<Primary>` modifier, which maps to **⌘ Command**
on macOS and **Ctrl** on Linux and Windows. So "Ctrl+T" means "⌘T" on a
Mac.

## Keyboard shortcuts

These work from anywhere in the window — they are application/window
accelerators, not tied to page focus.

### Navigation

| Shortcut | Action |
| --- | --- |
| `Ctrl+L` · `Alt+D` · `F6` | Focus the address bar (selects the URL) |
| `Alt+←` | Back |
| `Alt+→` | Forward |
| `Alt+Home` | Go to the home page |
| `Ctrl+R` · `F5` | Reload |
| `Ctrl+Shift+R` · `Ctrl+F5` | Hard reload (bypass the cache) |
| `Escape` | Stop loading / hide the find bar / leave full screen |

### Tabs and windows

| Shortcut | Action |
| --- | --- |
| `Ctrl+T` | New tab |
| `Ctrl+N` | New window |
| `Ctrl+W` · `Ctrl+F4` | Close tab (closes the window if it is the last tab) |
| `Ctrl+Shift+W` | Close the whole window |
| `Ctrl+Shift+T` | Reopen the last closed tab |
| `Ctrl+Tab` · `Ctrl+Page Down` | Next tab |
| `Ctrl+Shift+Tab` · `Ctrl+Page Up` | Previous tab |
| `Ctrl+1` … `Ctrl+8` | Jump to tab 1–8 |
| `Ctrl+9` | Jump to the last tab |
| `Ctrl+Q` | Quit |

### Find in page

| Shortcut | Action |
| --- | --- |
| `Ctrl+F` | Open the find bar |
| `F3` · `Ctrl+G` | Find next (opens the find bar if closed) |
| `Shift+F3` · `Ctrl+Shift+G` | Find previous |
| `Enter` / `Shift+Enter` | Next / previous match (while the find field is focused) |
| `Escape` | Close the find bar |

### Zoom

| Shortcut | Action |
| --- | --- |
| `Ctrl++` · `Ctrl+=` | Zoom in |
| `Ctrl+-` | Zoom out |
| `Ctrl+0` | Reset zoom to 100% |

Zoom is clamped to the 0.4×–5.0× range.

### Page and tools

| Shortcut | Action |
| --- | --- |
| `Ctrl+D` | Bookmark the page (toggles — repeats remove it) |
| `Ctrl+U` | View page source (toggles the raw-source view) |
| `Ctrl+S` | Save the page as HTML |
| `Ctrl+P` | Print |
| `Ctrl+Shift+S` | Save the page as PDF |
| `Ctrl+Shift+P` | Save a screenshot (PNG) |
| `Ctrl+Shift+J` · `Ctrl+Shift+I` | Open the developer console |
| `F11` | Toggle full screen |

## In-page keys

These act on the rendered document and only fire when the page surface
has focus (click the page, or `Tab` into it). When a text input is
focused, typing and editing keys go to that field instead.

| Key | Action |
| --- | --- |
| `Tab` · `Shift+Tab` | Move focus between links and form fields |
| `Space` · `Shift+Space` | Scroll down / up by ~90% of the viewport |
| `Page Down` · `Page Up` | Scroll down / up by ~90% of the viewport |
| `Home` · `End` | Jump to the top / bottom of the page |
| `↑` · `↓` | Scroll up / down a few lines |
| `Ctrl+A` | Select all text on the page |
| `Ctrl+C` | Copy the selected text |
| `Escape` | Dismiss an open JavaScript dialog |

## Mouse

| Input | Action |
| --- | --- |
| Left click | Follow a link, activate a control, focus an input, or place the caret |
| Left drag | Select text |
| `Ctrl`+left click | Open the link in a new window |
| Middle click | Open the link in a new window |
| Right click | Open the context menu |
| Back button (button 8) | History back |
| Forward button (button 9) | History forward |
| Wheel | Scroll vertically |
| `Shift`+wheel | Scroll horizontally |
| `Ctrl`+wheel | Zoom the page in / out |

Moving the pointer updates the cursor to match what is underneath — a
hand over links, an I-beam over editable text, and any CSS `cursor`
the page requests — and a link's target URL is shown in the status bar
while hovered.

The context menu adapts to what was clicked: links offer open / open in
new tab / copy address; images offer open / open in new tab / copy
address; `<audio>`/`<video>` offer open in the external player / copy
address; a selection offers copy and web search; and every menu ends
with Back, Forward, Reload, Copy Page URL, and View Page Source.

## Touchscreen

| Gesture | Action |
| --- | --- |
| Tap | Same as a left click |
| Touch drag / flick | Scroll the page (with kinetic momentum) |
| Long press | Open the context menu |
| Pinch | Zoom the page in / out |

The page zoom from a pinch is clamped to the same 0.4×–5.0× range as the
keyboard and `Ctrl`+wheel zoom, and is applied when the pinch finishes.

> **Mobile sites vs. touch input.** Touch gestures are independent of
> Nordstjernen's *mobile site* handling. For a few hosts (Facebook,
> YouTube) the browser requests the mobile variant and sends a mobile
> user-agent (`src/mobile.c`); that is about which page a site serves,
> not about touch.
