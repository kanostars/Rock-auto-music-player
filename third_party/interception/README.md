# Interception library

Upstream: https://github.com/oblitum/Interception
Revision: `39eecbbc46a52e0402f783b872ef62b0254a896a` (see upstream-commit.json).

The unmodified library sources are built as a separate `interception.dll`, loaded at runtime by the application. The official LGPL 3.0 text is included in LICENSE.txt. Upstream describes non-commercial usage under LGPL and offers separate commercial licensing; consult its licensing terms before distribution.

The kernel driver is not bundled. The Windows installer offers an unchecked, optional download-and-install step when driver components or filters are missing. It downloads the official [v1.0.1 archive](https://github.com/oblitum/Interception/releases/tag/v1.0.1), verifies SHA-256 `ad038963d6413055765128b0b931f6e765147c9916dba79e65d872b261f9af10`, and runs the official command-line installer with Windows administrator consent. Successful installation prompts for a restart; uninstalling the application leaves the driver installed. Portable users install the driver separately.

Keyboard discovery and output require a working Interception driver after restarting Windows. The application itself opens a context without installing interception filters; physical keyboard input is not captured by that context.
