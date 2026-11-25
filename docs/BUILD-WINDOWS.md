# Build Hades Workshop EX on Windows

The FBX SDK is a build dependency for the model tools. Obtain the SDK directly from [Autodesk](https://aps.autodesk.com/developer/overview/fbx-sdk). HWEX uses the header and libraries from that installation.

- Use Visual Studio with the Desktop development with C++ workload and the v143 toolset.
- Use wxWidgets 3.3.4 for Windows x64, including the Styled Text Control.
- Use Autodesk FBX SDK 2020.0.1 or a compatible 2020.x SDK with the VS 2017 x64 release libraries.
- Build Release x64 from a Visual Studio developer shell. Point the two properties at your local dependency directories.

```powershell
msbuild Hades-Workshop.vcxproj /p:Configuration=Release /p:Platform=x64 '/p:WxWidgetsRoot=C:\deps\wx334' '/p:FbxSdkRoot=C:\deps\FBX'
```

A prebuilt copy uses the runtime files supplied with that build. Running it does not require installing the developer SDK.

Hades Workshop EX is based on Hades Workshop 0.53 by Tirlititi and was modified by Cheddarbung in November 2026. See the [GNU GPL v3 license](../LICENSE), [wxWidgets license](https://www.wxwidgets.org/about/licence/), and [Squish copyright and license notice](../Source/Squish/squish.cpp).

## Runtime packaging

Run ./Publish-HWS.ps1 -StageOnly to build and validate a staged Release x64 package. Run ./Publish-HWS.ps1 with HWEX closed to publish it. The default layout is Source beneath the runtime folder with sibling BuildDependencies/wxWidgets and BuildDependencies/FBX folders; paths can also be supplied as script parameters. Publishing preserves configuration and workspaces and backs up replaced runtime files.

Manual packages must place share/FieldViewerSpsPresets.tsv, share/FieldViewerSpsGround.tsv and share/FieldViewerWeather.tsv beside the executable for background SPS/weather previews, along with ScriptAPI.txt and the matching wxWidgets runtime DLLs. The publisher copies these automatically. Game and Moguri assets are obtained separately.