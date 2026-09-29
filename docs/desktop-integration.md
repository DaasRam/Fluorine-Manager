# Desktop integration

Fluorine uses the desktop's XDG portal for native file and directory dialogs.
Install an XDG desktop portal backend for your desktop environment if these
dialogs are unavailable. When its bundled Qt portal plugin is available,
Fluorine selects it only when `QT_QPA_PLATFORMTHEME` is unset; an explicit
platform theme supplied by the user is preserved. The portal's appearance
follows the desktop theme.
Fluorine's selected instance stylesheet and font settings apply to its own UI.

The **Pin** button beside the executable selector shows or hides that program
on Fluorine's toolbar and Run menu.

The menu attached to the **Pin** button also offers **Desktop shortcut** and
**Application menu shortcut**. Fluorine creates a `.desktop` entry and a
paired launch script in the selected XDG location. Each generated pair carries
an identity marker; Fluorine updates or removes only files with the matching
marker, and leaves unmarked files or symbolic links untouched. Shortcut names
are sanitized and include an identity suffix so similarly named executables
do not overwrite one another.

Older shortcut pairs do not carry identity markers, so Fluorine leaves them in
place. Remove only legacy artifacts you recognize. If the Desktop directory
was redirected through XDG user-directory settings, inspect that configured
desktop location too. Existing shortcut pairs remain launch-compatible.
