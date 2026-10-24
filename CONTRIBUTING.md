# Contributing

FrameFlow is a Windows x64 application with a C++20 engine and a WinUI 3 interface.

1. Open an issue describing the bug or proposed change before a large contribution.
2. Build with `build.bat nopause` and run `test.bat nopause` on Windows with Visual Studio 2022
   C++ tools and .NET SDK 8 or newer.
3. Keep generated files, local profiles, logs and game paths out of commits.
4. For timing changes, include measured frametimes and CPU use. For game integration changes,
   describe the graphics API and whether a live FrameFlow session was attached.
5. Do not add anti-cheat evasion, stealth injection, or third-party code without compatible
   redistribution rights and its required notices.

By submitting a contribution, you agree that it may be distributed under the project's MIT license.
