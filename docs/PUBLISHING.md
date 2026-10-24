# Publishing FrameFlow

The repository is prepared for a GitHub release. The current version is `1.0.0`.

1. Create a public GitHub repository. FrameFlow's original source is covered by the root MIT
   license; keep `LICENSE` and `THIRD_PARTY_NOTICES.md` in the repository and release ZIP.
2. Authenticate GitHub CLI (`gh auth login`) and configure your Git author name and email.
3. Review the staged source files, then create the initial commit and push `main` to your chosen
   GitHub repository. Do not upload `bin/`, `build/`, `dist/`, `logs/`, local game paths or profiles.
4. Wait for the Windows build and tests to pass. To publish the release, push a `v1.0.0` tag.
   The workflow checks that the tag matches the native and UI versions, builds both release ZIPs,
   runs the tests, and attaches ZIPs and `.sha256` files to a GitHub release.

To build the same release locally on Windows:

```bat
build.bat nopause
test.bat nopause
package-portable.bat nopause -SkipBuild
pwsh ./tools/package-portable.ps1 -SkipBuild -Lite
```

Use `dist\FrameFlow-v1.0.0-Lite.zip` if the x64 .NET 8 Runtime is installed. The smaller Lite
package includes Windows App SDK Runtime 1.7 but does not include .NET. Otherwise use the
standalone `dist\FrameFlow-v1.0.0-Portable.zip`. Extract either ZIP and open `FrameFlow.exe`.
The DX11/DX12 hook is experimental; verify its active state and frame statistics in the UI.
