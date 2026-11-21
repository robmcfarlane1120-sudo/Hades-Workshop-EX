# Changelog

## Unreleased

- Added native workspace buttons for field scripts and dialogue, automatic pending-change refresh, and variable-allocation metadata refreshed on parse and save.
- Hardened script loading, batch entry imports, and workspace parse sizing; custom battle exports avoid duplicate background registration.
- Replaced the old custom-field export controls with selective field creation and right-click Export Assets, including MapConfig lighting. Normal exports preserve field visual assets.
- Added a built-in field script template, guarded custom text-block removal, and Parse and Export with a destination folder in Review Changes.
- Added typed custom casting names and numeric animation IDs; fixed reopening animation dialogs.
- Updated the application icon and opening artwork for Cheddar's Workshop.
- Made OK in Create Steam Mod and enabled Import in Open Mod the initially focused default buttons for Enter.
- Added a validated Windows build/publish script and installation-local source/dependency paths, preserving configuration and workspace files during deployment.

- Added a project script workspace in Modifications Manager for modified field and battle scripts, with diff review and staged batch parsing. The field script editor can add a field without reopening the project.
- Updated the script editor with syntax highlighting, line numbers, indentation guides, brace matching, folding, and variable occurrence highlighting. Find and opcode insertion use the new editor while parsing and project serialization still use the existing HWS code.
- The Windows build now uses the wxWidgets Styled Text Control and needs the matching wxWidgets STC runtime DLL.

## v1.0.1

- Fixed an argument count overflow in the script bytecode reader for large switches. The `.hws` format is unchanged.
- Fixed stray language tags in Single Language Mode and stopped unchanged script text from being marked as needing Parse.
- Prefilled Rename Function with the current name or alias.
- Enlarged the Function List, moved unnamed opcodes below named functions, and made the list an independent window. Right-click or press Ctrl+C to copy a zero-filled call. Double-click inserts a call on its own indented line.
- Open and Open Mod remember the last FF9 launcher and `.hws` path. Both loads require confirmation in their dialogs.
- Open Mod and Save Mod use fresh dialogs. Importing another `.hws` preserves the active project's save path, and project import reports progress.

- Added dated modification notices, clarified Tirlititi's upstream credit, and documented third-party licenses. The original GPL v3 license is unchanged.
- Fixed the Visual Studio 2022 Release x64 defaults so the project uses the v143 toolset and builds `x64/Release/HadesWorkshop.exe` inside the repository.
- Ignored local Visual Studio cache files, publish output folders, and the publish build log.
