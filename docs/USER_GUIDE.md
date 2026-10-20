# RDSM User Guide

Remote Disk Space Monitor (RDSM) shows the free space on a list of network shares (UNC paths) and Windows drive letters in one window, and re-checks them automatically.

## Starting the app

- Run `RDSM.exe`. On first run it creates a config file with a default list of shares; on later runs it loads your saved list.
- **Custom share list:** run `RDSM.exe --JSON <FileName>` to use `%APPDATA%\RDSM\<FileName>` instead of the default `shares.json`. If that file doesn't exist yet, it is created with an **empty** share list (no default shares) — add your shares via File → Edit Shares...

## Main window

| Column | Meaning |
|---|---|
| Share Name | The share path (UNC path or drive letter) from your share list |
| Total Size | Total size of the share's volume |
| Used Space | Space in use |
| Free Space | Space available |
| Free % | Percentage free, with a color bar showing how much is left |

- A share that can't be reached shows **Offline** in the Total Size column. Selecting that row shows the error in the status bar at the bottom.
- The status bar normally shows a summary, e.g. `5 of 6 shares online`.
- **Column widths, window size, and window position are remembered** — resize or move the window and it will look the same next time you start the app.

## Menu

| Item | What it does |
|---|---|
| File → Refresh (F5) | Re-check all shares now |
| File → Edit Shares... | Add, edit, remove, or reorder shares |
| File → Settings... | Change the auto-refresh interval |
| File → Exit | Close the app |
| Help → About | Show version and build time |

## Edit Shares dialog

The dialog shows your current share list on the left and a text box below it. Selecting a share in the list loads its path into the text box.

| Button | What it does |
|---|---|
| **Add** | Appends the text in the text box as a **new** share at the end of the list. Empty text is ignored. The text box is cleared and the new entry is selected. |
| **Update** | Replaces the **selected** share with the text in the text box. This is how you rename or fix an existing entry (e.g. correct a typo in a UNC path). The entry keeps its position in the list. |
| **Remove** | Deletes the selected share from the list. |
| **Move Up** / **Move Down** | Moves the selected share one position up or down, to control the order shares appear in the main window. **Move Up** is grayed out when the top share is selected; **Move Down** when the bottom share is selected. Both are grayed out when there are 0 or 1 shares. |
| **Save** | Writes the list to the config file and updates the main window in the new order. Grayed out (disabled) while the list contains duplicates (see below). |
| **Cancel** | Closes the dialog and discards all changes made in it. Grayed out (disabled) while the list contains duplicates (see below). |

Notes:

- Changes are only in memory until you click **Save** — closing the window any other way (or clicking Cancel) loses them.
- Share paths are UNC paths such as `\\server\share`, or Windows drive letters such as `C:` or `D:`. A trailing backslash is not required.
- The order of the list is the order the shares appear in the main window.
- **Duplicate shares are flagged in red.** If the same share appears more than once (case-insensitive, so `C:` and `c:` count as the same), the **first** entry is shown in a red font. While any entry is red you **cannot close the dialog** — Save, Cancel, and the window's X are all **grayed out** — use **Update** to rename one of the duplicates or **Remove** to delete one, and the red marking (and the buttons) clear automatically.

## Settings dialog

- **Auto Refresh (minutes):** how often the app re-checks all shares automatically. Must be a whole number between **1 and 3600** (1 minute to 1 day). The default is 15.
- Click **Save** to apply (the refresh timer restarts immediately with the new interval) or **Cancel** to discard.

### Auto-refresh behavior

- The app re-checks all shares on the configured interval.
- While the window is **minimized**, the timer is suspended (no background checks).
- When the window is **restored or maximized from minimized**, a refresh runs immediately and the timer restarts.

## About dialog

Shows the app name, version, and build time.

## Config file

- Location: `%APPDATA%\RDSM\shares.json` (for the default list) or `%APPDATA%\RDSM\<FileName>` when started with `--JSON <FileName>`.
- The file stores your share list, the auto-refresh interval, and UI state (column widths, window size and position). It is plain JSON, so you can back it up or edit it by hand while the app is closed.
- Deleting the file resets the app: the next start recreates it with the default share list (or an empty list for a custom `--JSON` file).
