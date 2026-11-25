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
