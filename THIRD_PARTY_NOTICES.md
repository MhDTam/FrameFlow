# Third-party notices

FrameFlow's original source and assets are licensed under the repository's [MIT license](LICENSE).
Third-party components retain their own licenses.

- **NVIDIA NVAPI SDK**: the import library and headers in `third_party/nvapi/` use the terms in
  `third_party/nvapi/LICENSE.txt` (portable ZIP: `licenses/NVAPI-MIT.txt`).
- **MinHook**: the optional Present detour uses MinHook v1.3.4 under BSD-2-Clause terms.
  Its source and license are in `third_party/minhook/` (portable ZIP: `licenses/MinHook-BSD.txt`).
- **Microsoft Windows App SDK**: the portable build carries runtime files from version
  `1.7.260224002`. Its terms and notices are in `third_party/windows-app-sdk/` (portable ZIP:
  `licenses/WindowsAppSDK-LICENSE.txt` and `licenses/WindowsAppSDK-NOTICE.txt`).
- **Microsoft .NET runtime**: the portable build carries .NET 8 runtime files. Terms and notices
  for the runtime pack used for this release are in `third_party/dotnet-runtime/` (portable ZIP:
  `licenses/DotNetRuntime-LICENSE.txt` and `licenses/DotNetRuntime-NOTICE.txt`).
- **System.Drawing.Common**: package version `8.0.31` uses MIT terms. Its terms and notices are
  in `third_party/system-drawing-common/` (portable ZIP: `licenses/SystemDrawing-LICENSE.txt`
  and `licenses/SystemDrawing-NOTICE.txt`).
- **Inno Setup**: the optional Windows installer is built with Inno Setup 7.1. Its license is in
  `third_party/inno-setup/LICENSE.txt` and is included in the installed `licenses/` folder.

If you redistribute a modified build, keep the applicable notices with it.
