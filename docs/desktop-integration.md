# Desktop integration

Fluorine uses the desktop's XDG portal for native file and directory dialogs.
Install an XDG desktop portal backend for your desktop environment if these
dialogs are unavailable. The portal's appearance follows the desktop theme.
Fluorine's selected instance stylesheet and font settings apply to its own UI.

The **Pin** button beside the executable selector shows or hides that program
on Fluorine's toolbar and Run menu.

Releases before the external-shortcut publisher was retired may also have
created per-executable `.desktop` files and paired `.sh` scripts on the
desktop or in `~/.local/share/applications`, plus cached icons below
`~/.local/share/icons/fluorine`. These names have no ownership marker and
can collide with unrelated files, so current releases deliberately do not scan or delete
them. Remove only artifacts you recognize. If the Desktop directory was
redirected through XDG user-directory settings, inspect that configured
desktop location too. Existing shortcut pairs remain launch-compatible,
but current releases no longer create or manage them.
