# Publishing FrameFlow

The repository is prepared for a GitHub release. The current version is `1.0.1`.

1. Create a public GitHub repository. FrameFlow's original source is covered by the root MIT
   license; keep `LICENSE` and `THIRD_PARTY_NOTICES.md` in the repository and release ZIP.
2. Authenticate GitHub CLI (`gh auth login`) and configure your Git author name and email.
3. Review the staged source files, then create the initial commit and push `main` to your chosen
   GitHub repository. Do not upload `bin/`, `build/`, `dist/`, `logs/`, local game paths or profiles.
4. Wait for the Windows build and tests to pass. To publish the release, push a `v1.0.1` tag.
   The workflow checks that the tag matches the native and UI versions, builds both release ZIPs
   and a Setup EXE, runs the tests, and attaches installers and `.sha256` files to a GitHub release.

To build the same release locally on Windows:

```bat
build.bat nopause
test.bat nopause
package-portable.bat nopause -SkipBuild
pwsh ./tools/package-portable.ps1 -SkipBuild -Lite
pwsh ./tools/package-installer.ps1 -Version 1.0.1 -IsccPath C:\path\to\InnoSetup7\ISCC.exe
```

Use `dist\FrameFlow-v1.0.1-Lite.zip` if the x64 .NET 8 Runtime is installed. The smaller Lite
package includes Windows App SDK Runtime 1.7 but does not include .NET. Otherwise use the
standalone `dist\FrameFlow-v1.0.1-Portable.zip`. Extract either ZIP and open `FrameFlow.exe`.
The per-user `dist\FrameFlow-v1.0.1-Setup.exe` includes both runtimes and needs no manual extraction.
It is not code-signed. Inno Setup 7.1 is downloaded from its official release and verified by
SHA-256 in the tag workflow; the compiler is not committed to this repository.
The DX11/DX12 hook is experimental; verify its active state and frame statistics in the UI.
