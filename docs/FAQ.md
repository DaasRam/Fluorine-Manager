# FAQ

## Where Are Logs Stored?
Logs are written to the active instance's `logs/` folder, beside its
`ModOrganizer.ini`. The log panel can open that folder.

## Does Removing an Instance Delete My Files?
Not by default. It removes the profile from the menu, and gives you the option to delete it if you want to.

## Do I Need to Install 9 Million Different Dependencies?
No, the dependencies are handled by NaK! If there is something missing I will gladly add it to the list. This also includes WINEDLLOVERWRITES as well!

## How Do I Set Up Fluorine Before Playing?
Open **Settings > Compatibility**, select a Proton version, choose the prefix
location (or keep the default), and click **Set up now**. Wait for setup
to finish installing the Windows components before launching a game or tool.

## Do I Need to Configure FUSE Permissions?

FUSE mounts are accessible only to the mounting user by default; no change to
`/etc/fuse.conf` is needed. To share an instance's mounts with other users
(including root), enable **Settings > Compatibility > Advanced > Allow other users to
access FUSE mounts (allow_other)**. This takes effect on the next FUSE mount,
enforces file permissions, and does not affect USVFS launches.

If `user_allow_other` is missing, enabling the checkbox offers to add it to
`/etc/fuse.conf` through your desktop's administrator authentication dialog.
Fluorine never receives your password. Existing configuration is preserved,
and the checkbox stays off if authentication is cancelled or the change fails.
No authentication is needed if the directive is already enabled.

The host permission remains enabled if you later turn the checkbox off or
cancel Settings; each instance still controls whether its mounts use
`allow_other`. If polkit or a desktop authentication agent is unavailable,
or the system configuration is read-only, an administrator can enable
`user_allow_other` manually in the host's `/etc/fuse.conf`.

## Does First-Time FUSE Indexing Read Every Mod File?

Fluorine builds its mount index from file paths, sizes, permissions, and
filesystem timestamps. New and changed files do not need a full content hash
before mounting. BSA/BA2 member lists are read from the archive directories
and cached using each archive's filesystem identity and timestamps.

Directory scanning still takes time, especially with many small files, but
indexing no longer needs to read every gigabyte of mod contents. This is a
file inventory, not an integrity check. Content verification remains in file
promotion operations; overlapping files without verified hashes are not
reported as identical.

## Can I Enable Kernel FUSE Passthrough?

There is no passthrough option yet. Kernel passthrough could reduce overhead
when games read files after mounting. It does not accelerate building the
initial file index.

The [Linux passthrough interface](https://docs.kernel.org/filesystems/fuse/fuse-passthrough.html)
currently requires `CAP_SYS_ADMIN`. It also requires consistent passthrough
opens and a shared backing file for each open inode. Fluorine's write handling
can change a file's backing location to staging on its first write, so simply
enabling passthrough for read-only handles would conflict with that behavior.
Supporting it needs coordinated backing-file and copy-on-write handling,
plus a privilege arrangement that keeps the desktop application unprivileged.

## Does It Work with Existing Modlists?
Yes, it can parse Wine paths and read them out as Linux paths in the GUI. It will also save the paths as wine paths in case you move to MO2 via proton/wine.

To open an existing portable instance, run `./fluorine-manager --instance /path/to/instance/`.
The directory should contain its `ModOrganizer.ini`.

And all the buttons like associate with mod manager downloads button and MO2 OAuth also works.

## Why Does Fluorine Install LOOT 0.29.1?

The managed Windows LOOT download is pinned to 0.29.1 for Proton compatibility.
Newer LOOT releases changed their Wine runtime requirements; see
[issue #187](https://github.com/SulfurNitride/Fluorine-Manager/issues/187).
Fluorine verifies the download and prepares the replacement before replacing
an existing installation. Cancelling a download keeps the previous installation.

## Why Is Bethesda Plugin Manager Disabled?

Fluorine automatically disables the Bethesda Plugin Manager extension at startup
because its replacement Plugins panel conflicts with the workspace. The built-in
**Plugins** tab remains available, including in setups that previously enabled
the extension. **Settings > Plugins** shows the reason it cannot be enabled.

This is a runtime compatibility block: the extension's files and saved settings
are preserved. It does not disable your game's ESP, ESM, or ESL plugins.

## Does UTF-8 Support Change My Game's Language?

Fluorine uses UTF-8 for Wine's Linux filenames so mods can contain names from
multiple languages at once. Explicit launch locale settings take priority.
Otherwise, Fluorine uses Steam's selected game language when available. For
GOG and other installations without Steam language metadata, it reads the
Bethesda-style `[General] sLanguage` setting from the INIs used by the selected
profile. A matching game `Custom.ini` can override the main INI. Profile-specific
INIs are used when enabled; otherwise, the game's normal INIs are read.

When applying a game language, Fluorine removes conflicting inherited locale
categories from the child process. It leaves the desktop environment and game
INIs unchanged. If neither source supplies a recognized language, it preserves
the host's language and region while upgrading the encoding to UTF-8 (for
example, `ja_JP.SJIS` becomes `ja_JP.UTF-8`). Empty, `C`, and `POSIX` defaults
become `C.UTF-8`. Games with other language-configuration formats can use an
explicit launch override.

To explicitly select a Wine locale for an executable, set
`HOST_LC_ALL=ja_JP.UTF-8` (or another language's UTF-8 locale) in its executable's
wrapper options. This works without Steam and is
[Proton's locale override](https://github.com/ValveSoftware/Proton#runtime-config-options).
It does not install game translations or fonts. Fluorine prepares this environment
before launch and marks it with `SteamEnv=1` so Steam does not reset it to ASCII;
no locale preload helper or system-wide locale change is needed.
