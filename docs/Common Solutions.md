# Common Solutions

Working notes on non-obvious problems hit during RDSM development and how they were solved.

## Dialog controls render bold text (buttons, statics, list boxes)

### Symptom

In the Settings and Edit Shares dialogs, the text on **buttons, static labels, and the list box looked bold**, while the **edit control looked normal**. The About dialog looked correct (its text is GDI+-drawn and its OK button uses the default system font). The main window's ListView also looked fine (it never receives a custom font).

### What did NOT work

1. **Changing the font face/weight.** The dialogs originally used a hand-built font:

   ```cpp
   HFONT font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
   ```

   Swapping this for the system message font (`lfMessageFont`) produced **no visible change at all** — the controls stayed bold. A font change that changes nothing means the font is not actually being applied; the rendered text is coming from somewhere else.

### Root cause

The font was **deleted immediately after being set**:

```cpp
HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
// ...
for (HWND c : {hwndList_, hwndEdit_, btnAdd, /* ... */}) {
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}
DeleteObject(font);   // BUG: controls still hold this handle
```

A control that receives `WM_SETFONT` only **stores the `HFONT` handle** — it does not take a reference to the GDI object. Deleting the font right away leaves every control with a **dangling handle**. When the control later paints, `SelectObject(hdc, hFont)` fails on the invalid handle and the control falls back to whatever font the DC already has (the default GUI font), which is what was rendering "bold". Because the fallback font was doing the rendering, changing the font we created had no effect.

A secondary factor (confirmed with a reference app built against the same SDK): on this machine a hand-built `CreateFontW(..., L"Segoe UI")` font renders visibly heavier than the system message font, even with `FW_NORMAL`. The system message font (`lfMessageFont`) renders at the expected normal weight.

### Solution

Two parts, both required:

1. **Use the system message font** (the same font the edit control and the About dialog's button render correctly with):

   ```cpp
   NONCLIENTMETRICSW ncm{};
   ncm.cbSize = sizeof(ncm);
   SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
   ncm.lfMessageFont.lfWeight = FW_NORMAL;
   hFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
   ```

2. **Keep the font alive for the dialog's lifetime.** Store it in a member and delete it only in `WM_DESTROY`:

   ```cpp
   // header
   HFONT hFont_ = nullptr;

   // onCreate: after creating all controls
   for (HWND c : {hwndList_, hwndEdit_, btnAdd, /* ... */}) {
       SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(hFont_), TRUE);
   }

   // handleMsg
   case WM_DESTROY:
       hwnd_ = nullptr;
       if (hFont_) {
           DeleteObject(hFont_);
           hFont_ = nullptr;
       }
       return 0;
   ```

Applied in `src/settingsdialog.cpp` and `src/editdialog.cpp`. Verified on the desktop: all controls in both dialogs now render at normal weight.

### General lessons

- **Never `DeleteObject` a font (or brush/pen) that a control is still using.** Controls store the handle without referencing the GDI object. The owner of the font must outlive every window that uses it.
- **If a font change has no visible effect, suspect the font isn't being applied** — dangling handle, sent to the wrong window, or sent before the control exists — before chasing font metrics.
- **Prefer `lfMessageFont` over hand-built Segoe UI fonts for dialog controls on this machine**; hand-built `CreateFontW` Segoe UI renders heavier than expected here.
- GDI+-drawn text (`Graphics::DrawString`) and controls left on the default system font are not affected by this issue — useful as a control group when diagnosing.

## ListView custom draw: the Free % bar is not drawn

### Symptom

The GDI+ free-space bar in the **Free %** column of the main window's ListView was not being drawn. The column showed only the default percentage text, no bar, even though a custom-draw handler was present and the app built and ran fine.

This took two rounds to fix because there were **two independent bugs**, and fixing the first one was not enough.

### Bug 1 — a fabricated "non-standard SDK" workaround

An earlier `src/customdraw.h` was created on the (false) assumption that this machine's `commctrl.h` declared a **non-standard** `NMCUSTOMDRAW` layout. It defined a "classic" `RealNMLVCUSTOMDRAW` struct with **fabricated fields** (`hdcText`, `oStateText`, a duplicate item-spec, and a `dwReturnFlags` field) plus hand-defined `CDRF_PREPAINT*` draw-stage constants (1..6). The code reinterpreted the `NM_CUSTOMDRAW` pointer to `RealNMLVCUSTOMDRAW*` and wrote the return flags into `nmcd.dwReturnFlags`.

The SDK header is in fact **standard** — `NMCUSTOMDRAW`/`NMLVCUSTOMDRAW` and the `CDDS_*`/`CDRF_*` constants match the Microsoft docs exactly. So that workaround was wrong on every count:

- **The CDRF flags are the `WM_NOTIFY` return value, not a struct field.** `NMCUSTOMDRAW` has **no `dwReturnFlags` field**. The control acts on the value the window procedure **returns** from `WM_NOTIFY`. The handler did `onNotify(...); return 0;` — returning 0 **silently disables custom draw**.
- **Writing to a non-existent field** (`pcd->nmcd.dwReturnFlags = ...`).
- **Wrong struct offsets** — the fabricated fields meant `rc`, `dwItemSpec`, and `iSubItem` were read from the wrong places.
- **Wrong stage constants** — the hand-defined `CDRF_PREPAINT*` values (1..6) are not the real `CDDS_*` draw-stage constants.

Fixing this (deleting `customdraw.h`, using the SDK types, returning the flags from `WM_NOTIFY`) was necessary but **not sufficient** — the bar still did not draw.

### Bug 2 — the missing `CDDS_ITEMPREPAINT` stage (the real root cause)

Getting **subitem-level** notifications from a ListView is a **two-stage handshake** (per the `NM_CUSTOMDRAW` docs):

1. `CDDS_PREPAINT` → return `CDRF_NOTIFYITEMDRAW`
2. `CDDS_ITEMPREPAINT` → return `CDRF_NOTIFYSUBITEMDRAW`
3. `CDDS_ITEMPREPAINT | CDDS_SUBITEM` → draw each subitem, return `CDRF_SKIPDEFAULT`/`CDRF_DODEFAULT`

The trap: **`CDRF_NOTIFYITEMDRAW` and `CDRF_NOTIFYSUBITEMDRAW` are the same value (0x20).** The SDK header even comments "flags are the same, we can distinguish by context." The control decides item-level vs. subitem-level by the **draw stage the flag is returned at**, not by the value.

The code returned `CDRF_NOTIFYSUBITEMDRAW` at `CDDS_PREPAINT`. Because that equals `CDRF_NOTIFYITEMDRAW`, the ListView correctly started sending **item-level** (`CDDS_ITEMPREPAINT`) notifications — but the handler had **no `case CDDS_ITEMPREPAINT:`**, so those fell through to `default:` → `CDRF_DODEFAULT`. The ListView then drew each item normally and **never sent the `CDDS_ITEMPREPAINT | CDDS_SUBITEM` notifications** the bar-drawing code was waiting for.

A temporary diagnostic log of `dwDrawStage` confirmed it: only `CDDS_PREPAINT` (0x1) and `CDDS_ITEMPREPAINT` (0x10001) stages ever arrived — never `CDDS_ITEMPREPAINT | CDDS_SUBITEM` (0x30001).

### Solution

In `src/app.cpp` / `src/app.h`:

1. **Deleted `src/customdraw.h`**; removed it from `CMakeLists.txt` and `app.h`. Use the SDK's `NMLVCUSTOMDRAW*` and `CDDS_*`/`CDRF_*` constants.
2. **Return the flags from `WM_NOTIFY`:** `onNotify`/`onCustomDraw` return `LRESULT`; the handler is `return onNotify(reinterpret_cast<NMHDR*>(lParam));`.
3. **Implement the full two-stage handshake in `onCustomDraw`:**
   - `CDDS_PREPAINT` → return `CDRF_NOTIFYITEMDRAW`
   - `CDDS_ITEMPREPAINT` → return `CDRF_NOTIFYSUBITEMDRAW`
   - `CDDS_ITEMPREPAINT | CDDS_SUBITEM` → for the Free % column, draw the bar and return `CDRF_SKIPDEFAULT`; for other subitems, return `CDRF_DODEFAULT`
   - default → return `CDRF_DODEFAULT`

Verified on the desktop: the Free % bar now draws.

### General lessons

- **For comctl32 v6 custom draw, the CDRF_* flags are the return value of `WM_NOTIFY`** — not a field in `NMCUSTOMDRAW`. Returning 0 disables custom draw entirely, with no error.
- **Subitem custom draw is a two-stage handshake.** You must return `CDRF_NOTIFYITEMDRAW` at `CDDS_PREPAINT` **and** `CDRF_NOTIFYSUBITEMDRAW` at `CDDS_ITEMPREPAINT`. Returning the subitem flag at the wrong stage (or skipping the `CDDS_ITEMPREPAINT` case) silently yields item-level notifications only.
- **`CDRF_NOTIFYITEMDRAW` == `CDRF_NOTIFYSUBITEMDRAW` == 0x20.** They are distinguished by the draw stage they are returned at, not by value.
- **Draw stages are the `CDDS_*` constants** (`CDDS_PREPAINT`, `CDDS_ITEMPREPAINT`, `CDDS_SUBITEM`), not `CDRF_PREPAINT*`.
- **Don't fabricate a struct layout to "match the runtime ABI."** Verify against the SDK header and the Microsoft docs first. A struct with wrong field offsets silently reads garbage (wrong row, wrong rect, wrong subitem) and is very hard to debug.
- **When custom draw "does nothing," log the `dwDrawStage` values** to see which stages are actually arriving. That immediately revealed the missing `CDDS_ITEMPREPAINT | CDDS_SUBITEM` stage here.
