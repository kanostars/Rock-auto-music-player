# Vendored dependencies

Midifile: https://github.com/craigsapp/midifile

Pinned revision: `98917df5b1bf0d6e8d4c0e5fff86d6b05343e793`.
The upstream sources are unmodified; see `midifile/LICENSE.txt` for the BSD-2-Clause license.
Our CMake configuration builds only the five library translation units needed for SMF reading/writing.

Qt 6.8.3 MinGW x64 is downloaded separately into `.deps/Qt` and dynamically linked.
Qt is not vendored into source control. See Qt's distributed license files in the SDK.

miniaudio 0.11.23: https://github.com/mackron/miniaudio/tree/0.11.23

`miniaudio/miniaudio.h` and `miniaudio/LICENSE` are unmodified upstream files.
Header SHA-256: `7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6`.
The project chooses the MIT-0 license option. `implementation.cpp` is our build wrapper.
Only the device and WAV decoding APIs are used; the library is linked statically,
with no additional audio DLL, SoundFont, or driver installation required.

Handpan recordings are separate user-provided assets; see `assets/handpan/README.md`.
The miniaudio license does not license those recordings.
