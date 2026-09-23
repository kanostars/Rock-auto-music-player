# Interception library

Upstream: https://github.com/oblitum/Interception
Revision: `39eecbbc46a52e0402f783b872ef62b0254a896a` (see upstream-commit.json).

The unmodified library sources are built as a separate `interception.dll`, loaded at runtime by the application. The official LGPL 3.0 text is included in LICENSE.txt. Upstream describes non-commercial usage under LGPL and offers separate commercial licensing; consult its licensing terms before distribution.

The kernel driver is not bundled or installed by this project. Keyboard discovery and output require an already installed, working Interception driver. The application opens a context without installing interception filters; physical keyboard input is not captured.
