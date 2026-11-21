# Cheddar's Workshop

An unofficial Windows x64 build of Hades Workshop 0.53, originally developed by [Tirlititi](https://github.com/Tirlititi/Hades-Workshop) (upstream commit f428246).

> **Modified by Cheddarbung — November 2026. Based on Hades Workshop 0.53 by Tirlititi. Distributed under GNU GPL v3.** See [LICENSE](LICENSE).

## Appearance and navigation

- **Dark Windows UI.** Charcoal panels, gray tabs, lighter gray text inputs, and off-white text. The build uses wxWidgets 3.3.4 for Windows dark mode.
- **Larger layout.** Wider sidebar lists show more names and IDs. Larger controls and fonts make the editor easier to read at 1440p. Main tabs and script text get additional sizing.
- **Two-key list search.** Hold the first letter and tap the second to jump to matching names in single-select lists and non-editable dropdowns. For example, hold **M** and tap **E** to cycle through names beginning with **Me**. Numeric prefixes such as `0600 :` are skipped.

## Scripts and dialogue

- **Find in Script.** Use **Ctrl+F**, **F3**, and **Shift+F3** for find, next, and previous in the displayed function. Search supports case matching and wraparound.
- **Saved function labels.** Right-click a function and choose **Rename Function**. The label is stored with the project's local script data and changes how the function is displayed in the list. Script identities and game bytecode use their original identifiers.
- **Custom field text blocks.** Create an empty named block with its own ID, edit its dialogue, and assign it from the field's Text Block dropdown. HWS saves the block in the `.hws` project and exports the localized `.mes` files and Memoria `MessageFile` registrations.
- **Dialogue list actions.** **Add 10** appends blank entries in one step. **Go to End** jumps to the final entry.

## Custom fields and projects

- **Embedded field previews.** **Change Preview** embeds a selected PNG in a custom field's `.hws` project data. It follows the project when it is renamed or saved under a new filename. The preview image affects the editor display only.
- **Selective field creation and asset export.** Right-click a field to choose **Create New Field...** or **Export Assets**. Creation includes a donor selector, component choices, a built-in script template, and an optional asset export folder. Explicit asset export includes BGX/cameras, BGI, PNG, SPS, and MapConfig lighting where available. Normal project exports preserve field visual assets.
- **Project open and save.** HWS remembers the project folder, prefills the save path for the current project, and displays the project filename and unsaved marker in the title bar.
- **Standalone executable.** The editor features above run without the external QoL companion. This build still uses Memoria for the game's mod loading.

## Project Workspace

HWS creates `CodexWorkspace` beside its executable at launch. When you open a saved Steam `.hws`, it exports modified field and battle scripts into `Fields/` and `Battles/` in a project subfolder, plus every field dialogue block in `TextBlocks/<language>/`. New custom text blocks appear in the same folder as soon as you create them in HWS. Future opens refresh the workspace and preserve pending external edits. Each saved project path, game installation, and language mode gets its own subfolder.

Use **Open Workspace** in Modifications Manager to get the folder path. Edit the requested files using `AGENTS.md` and `opcodes.md` as references. Keep their `#HW` markers and function declarations intact. To include a new or unmodified field without reopening the project, click **Add to Workspace** beside **Function List** in that field's script editor. Parse any pending editor text first and click **OK** to keep the editor's changes.

For dialogue, keep the existing headers and IDs. Append each new message using the next local ID and a native HWS start tag. For example, if ID 41 is the last entry, append `#HW text 42` on its own line, then `[STRT=40,1]I heard footsteps.` on the following line. Select the text and script files together if a field script uses the new dialogue.

Back in Modifications Manager, choose **Review Changes**, select the files to inspect, and check the ones you want to apply. **Apply & Parse** compiles the selected changes on temporary copies. If any selected file fails validation, HWS leaves the open project intact. If the same script changed in HWS after it was exported, resolve the conflict before applying. After a successful apply, inspect the changes in HWS before choosing **Save Mod** or exporting. Recovery snapshots sit beside the internal baselines in `.hws-baseline/`.

## Native workspace and import updates

- Field and text panels expose Add to Workspace and Review Changes; pending external edits refresh the review button without restarting HWS.
- Workspace metadata exposes variable allocations and refreshes during parse and save. Imports preserve pending external changes and report conflicts instead of silently overwriting them.
- Script loading, batch entry handling, and workspace parsing include bounds and sizing fixes. Custom battle exports avoid duplicate background registrations.
- Export dialogs accommodate the larger UI font, and Create Steam Mod uses a readable cyan warning. Its OK button and Open Mod's enabled Import button receive initial keyboard focus and are the default Enter actions.

## Spell editing

- Custom casting titles can be entered from the Casting Name dropdown. Animation dialogs accept numeric SFX IDs, including custom effects, and reopen independently for each edit.

## Building on Windows

The supported configuration for this build is **Release | x64**, with Visual Studio 2022 C++ tools, wxWidgets 3.3.4 Windows x64 headers and DLL development libraries, and Autodesk FBX SDK 2020.0.1 with its VS 2017 x64 release libraries. The build has also been verified with FBX SDK 2020.1.

From a Visual Studio developer shell:

```powershell
msbuild Hades-Workshop.vcxproj /p:Configuration=Release /p:Platform=x64 '/p:WxWidgetsRoot=C:\deps\wx334' '/p:FbxSdkRoot=C:\deps\FBX'
```

The wxWidgets folder should contain `include` and `lib\vc14x_x64_dll`. The FBX folder should contain `include` and `lib\vs2017\x64\release`. The build produces x64/Release/HadesWorkshop.exe. Place the matching wxWidgets 3.3.4 x64 runtime DLLs beside it to run the editor. Build dependencies and game assets are obtained separately.

## Third-party notices

- The wxWidgets submodule carries the [wxWindows Library Licence](https://www.wxwidgets.org/about/licence/).
- The bundled Squish sources retain Simon Brown's copyright and license notices in Source/Squish.
- The inherited Source/fbxsdk.h carries Autodesk's copyright and refers to the FBX SDK license agreement. Obtain the SDK separately from [Autodesk](https://forge.autodesk.com/developer/overview/fbx-sdk) and consult its terms before redistributing SDK material or a build linked with it.

For the installation-local source layout, place this repository in `Source/` below the runtime directory and dependencies in sibling `BuildDependencies/wxWidgets` and `BuildDependencies/FBX` folders. Run `./Publish-HWS.ps1 -StageOnly` to build and validate the runtime package; run `./Publish-HWS.ps1` with HWS closed to publish it. The publisher preserves configuration and workspaces and backs up replaced runtime files. Dependency paths and the installation destination can also be supplied as script parameters.
