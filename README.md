# Hades Workshop EX

I started making these changes to Hades Workshop while developing The Lost Chapters. I wanted the editor to remember my folders, let me jump through lists with two-key prefix search, and make it easier to find things in field scripts and most importantly have a dark easier to look at UI. Those small quality-of-life changes grew into Hades Workshop EX, and I’m sharing it in case it makes other FFIX modders’ work a little easier.

- Unofficial Windows x64 edition of [Hades Workshop 0.53 by Tirlititi](https://github.com/Tirlititi/Hades-Workshop/tree/f428246), with additions by Cheddarbung. Distributed under [GNU GPL v3](LICENSE).
## Latest additions — 25 November 2026

- **Automatic project backups.** Successful Save Mod operations create timestamped copies under Backups/Project Saves/<project name> beside HWEX. Retains the latest 10 snapshots per project name, numbered 1.0, 1.1 and onward. Backup errors are reported separately from the project save.
- **Vivi mascot.** A transparent, click-through Vivi follows the active editor viewport (excluding Enemies). On Fields, he stays inside the preview's bottom border without adding scroll space.
- **Animated Moguri previews.** Supported 64-pixel Moguri backgrounds play tile animation. Pictures scale down to fit the window while preserving their aspect ratio, without image scrolling.
- **Independent controls.** Play and Play SPS sit below Preview. Tile and SPS playback stay enabled while browsing fields during the session. Tile animation runs 30% slower than its nominal rate.
- **Background SPS and weather.** Supported background effects, multiple preset instances, ground splashes and rain use MDC reference tables. Character-attached effects are excluded.
- **Fallback and shutdown.** Unsupported or failed tile loads use the regular HWS preview. Accepted close actions stop preview timers and ignore queued preview work; canceled close prompts preserve playback.

This is an editor preview, not field-script execution: effects from different story scenarios may appear together, and weather/placement can be approximate. Custom BGX and separate Moguri layout overrides retain the regular preview. Moguri assets are loaded from your game installation and are not included here.

See the [full feature inventory](docs/FEATURES-2026-11-15.md) and [changelog](CHANGELOG.md).

## What’s new since 0.53

- **Interface.** Dark interface, updated splash and icon, larger controls and text, and wider lists. The dialogue list shows roughly 20 rows at once.
- **Two-key list search.** Hold the first letter and tap the second to jump to a matching name in supported lists and dropdowns. Repeat to cycle through matches. Numeric ID prefixes are skipped.
- Script editor with syntax highlighting, line numbers, automatic indentation, folding, brace matching, and variable highlighting. Press Ctrl+F to find, F3 for the next match, and Shift+F3 for the previous match.
- **Saved function labels.** Right-click a function and choose **Rename Function** to save a project-specific display label while preserving the original function ID.
- **Opcode reference.** Browse named commands first in a separate searchable window. Copy a call with argument placeholders using Ctrl+C, or double-click to insert it on its own indented line.
- **Project workspace.** Use **Open Workspace** in Modifications Manager to access editable field scripts, battle scripts, and localized dialogue. Add a field or text block directly from its editor. Pending edits are preserved when the project reopens, and variable allocations are available for reference.
- **Review Changes.** Compare before and after, choose which files to apply, and see conflicts when the same content changed in HWS. **Apply & Parse** validates the selected files before applying them to the open project. **Parse and Export** also lets you pick an export destination.
- Create, assign, and remove custom field dialogue blocks. Save them in the project and export localized message files and Memoria registrations. Add ten blank entries at once or jump to the end of a dialogue list.
- Create fields from a chosen donor, pick which components to copy, start from a built-in script template, and optionally export the new field's assets.
- Embed a custom PNG preview in a field's project data. Export selected field assets separately, including background and camera data, walkmeshes, textures, SPS, and MapConfig lighting where available. Normal Steam mod exports preserve existing field artwork.
- **Script-only field export.** In **Create Steam Mod**, choose **Export custom field scripts only** for Raw Assets. It exports scripts and their FieldScene registrations while preserving the existing backgrounds, walkmeshes, and SPS assets. HWS remembers the option between sessions.
- **Custom battle export.** Avoids duplicate background-map registrations.
- Enter custom spell casting titles and numeric animation IDs, including custom SFX.
- Reopen the last confirmed game and project at startup, with remembered paths in Open and Open Mod. The window title shows the active project name and whether it has unsaved changes.
- Open Mod shows progress while loading a `.hws` project. Import another project without changing the current project's save path. Save Mod prefills the current path, and Steam mod creation and project import support Enter on their default buttons.
- **Script compatibility.** Large switch statements load correctly, and edits in Single Language Mode preserve script routes without adding stray language tags.

- [Windows build dependencies](docs/BUILD-WINDOWS.md)
