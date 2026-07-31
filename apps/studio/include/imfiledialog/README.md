# imfd — a portable ImGui file dialog

A self-contained, cross-platform **file open / save / folder-pick dialog** for
[Dear ImGui](https://github.com/ocornut/imgui). One code path on Linux, macOS
and Windows — it deliberately does **not** call the native OS picker, so it
looks and behaves identically everywhere and themes itself from your ImGui
style.

It is intentionally *not* a clone of Explorer / Finder / GTK chooser. The
identity is its own: a quick-access rail, clickable breadcrumb chips, a sortable
detail list (with a grid view and a fuzzy filter), and **custom vector icons
drawn with `ImDrawList`** — so there is no icon-font dependency.

![layout](https://example.invalid)  <!-- rail | breadcrumbs / list | footer -->

```
+- Open MCDF container ----------------------------------------+
| Places  |  ^ ✎ +   / > home > docs > contracts               |
|  Home   |  ------------------------------------------------  |
|  Desktop|  [ Filter… (fuzzy) ]  12   [≣][▦][hidden][⟳]        |
|  Docs   |  Name                 Size      Modified            |
|  ...    |  📁 drafts              —       2026-06-01 18:20    |
| Drives  |  📄 report.mcdf       2.1 MB    2026-06-01 18:21    |
|  /      |  📄 contract.mcdf     1.9 MB    2026-06-01 18:25    |
| Bookmrk |  ------------------------------------------------   |
|  ★ docs |  File: [ report.mcdf         ]  [ *.mcdf ▾ ]        |
| +Bookmk |                              [  Open  ] [ Cancel ]  |
+--------------------------------------------------------------+
```

## Dependencies

- Dear ImGui ≥ 1.89 (uses tables + `ImDrawList`; no backend assumptions).
- A C++17 compiler with `<filesystem>`. Compiles cleanly under C++17/20/23
  (MCDF builds it at **C++23**, the project standard).

Nothing else. No FontAwesome, no extra fonts, no platform UI toolkit.

## Drop into another project

Copy the `imfiledialog/` folder and add **one** translation unit + the include
root to your build:

```cmake
target_sources(my_app PRIVATE path/to/imfiledialog/imfiledialog.cpp)
target_include_directories(my_app PRIVATE path/to)   # so "imfiledialog/imfiledialog.h" resolves
# my_app already links your ImGui target
```

In MCDF this is done for you: the `mcdf::imfd` CMake target (see the sibling
`CMakeLists.txt`) compiles the unit and links `imgui::imgui`.

(`imfiledialog_util.h` is header-only and pulled in automatically. It carries
the UI-free helpers — encoding, fuzzy match, size/time formatting — and has no
ImGui dependency, so it is independently reusable and unit-testable.)

## Usage

Immediate-mode: open once, then call `Draw()` every frame while it is open.

```cpp
#include "imfiledialog/imfiledialog.h"

imfd::FileDialog dlg;            // keep one instance alive (holds bookmarks)

// On a button press:
imfd::Config cfg;
cfg.title   = "Open MCDF container";
cfg.mode    = imfd::Mode::OpenFile;          // OpenFile | OpenFiles | SaveFile | PickFolder
cfg.filters = { {"MCDF container", {".mcdf"}},
                {"All files", {"*"}} };
dlg.Open(cfg);

// Every frame (inside your ImGui frame):
switch (dlg.Draw()) {
    case imfd::Result::Picked:
        for (const std::string& path : dlg.Selected())   // UTF-8, absolute
            open_container(path);
        break;
    case imfd::Result::Cancelled: /* user dismissed */ break;
    case imfd::Result::None:      /* still open / idle */ break;
}
```

### Modes

| Mode         | Behaviour                                                        |
|--------------|-----------------------------------------------------------------|
| `OpenFile`   | Pick one existing file.                                         |
| `OpenFiles`  | Pick several (Ctrl-click toggles, Shift-click ranges).         |
| `SaveFile`   | Editable filename; overwrite-guarded when `confirm_overwrite`. |
| `PickFolder` | Only folders are shown; returns the chosen (or current) dir.    |

### Config knobs

- `start_dir` — where to open (UTF-8). Empty → last-visited, else `$HOME`.
- `default_name` — pre-filled name for `SaveFile` (e.g. `"untitled.mcdf"`).
- `filters` — the format dropdown. Extensions may be `".mcdf"` or `"mcdf"`; an
  empty list or `"*"` matches everything. Empty `filters` ⇒ a single
  "All Files (\*)".
- `show_hidden`, `confirm_overwrite`, `allow_create_dir`, `size`.

### Persistence

Bookmarks and the last directory live on the `FileDialog` instance:

```cpp
dlg.Bookmarks();        // std::vector<std::string>& — serialize to persist
dlg.LastDirectory();    // UTF-8 path of the folder last shown
```

Seed `Bookmarks()` before the first `Open()` to restore a user's pinned folders.

## Notes

- **Encoding.** All paths in/out are UTF-8 (what ImGui renders). On Windows the
  widget round-trips through `std::filesystem::path::u8string()`, so non-ASCII
  names are correct — never `path::string()`, which is the native narrow code
  page there.
- **Theming.** Colors come from `ImGui::GetStyle()` (`Text`, `CheckMark`,
  `Header`, `PopupBg`…), so the dialog follows whatever theme the host sets.
- **Large folders.** The list view is clipped (`ImGuiListClipper`), so
  directories with thousands of entries scroll smoothly.

## Files

| File                    | Role                                              |
|-------------------------|---------------------------------------------------|
| `imfiledialog.h`        | Public API (`Config`, `Mode`, `Filter`, `FileDialog`). |
| `imfiledialog.cpp`      | Implementation + custom vector glyphs. Needs ImGui.    |
| `imfiledialog_util.h`   | UI-free helpers (encoding / fuzzy / format). No ImGui. |
