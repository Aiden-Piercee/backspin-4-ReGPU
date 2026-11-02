# Changelog

Versions are MAJOR.MINOR.PATCH. The number lives in the `VERSION` file and is the only place to change it.
It ends up in the launcher (header and window title), the game window title, `regpu.log`, the exe, and the
release zip name. Each release has a matching git tag (`v0.1.0`) on the `ts4` branch.

- PATCH (0.1.1): fixes only.
- MINOR (0.2.0): new features or noticeable improvements.
- MAJOR (1.0.0): reserved for when the port is considered finished.

## 0.1.0

First release: the native Top Spin 4 port (no emulator), with the TS4Launcher app.

- Game runs natively through the recompiled game code and a Direct3D 12 renderer.
- Launcher with Play, Graphics and Settings tabs; drop tiles for the game (ISO, folder or zip), the title update and DLC.
- Vertex and index buffers are fetched in copy mode (direct reads caused bobble heads).
