# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

> **Versioning note.** `plato-engine-block-c` is a single-header C99 library
> with no package manifest, so versioning is git-tag-only. This is the first
> tagged release. The header comment and `PLATO_PROTOCOL.md` refer to a
> "TernARY v1.1" revision — that is a feature/protocol identifier, not a
> release tag.

## [0.1.0] - 2026-07-06

### Added
- Initial reference implementation of the Plato Engine Block as a single-header C99 library (`include/plato_engine.h`): a sensor→history→alarm engine with per-sensor ring-buffer history, threshold alarms (`>`/`<`/`>=`/`<=`/`==`) with post-fire cooldown, a line-delimited text command protocol via `plato_handle_command()`, and a POSIX `poll()`-based TCP server (`src/server.c`) with subscriber tick broadcasts. Ships demo sensors (`src/sensors_dummy.c`), an interactive stdin daemon (`src/main.c`), a `Makefile`, MIT license, unit tests (`tests/test_engine.c`, `tests/test_history.c`, `tests/test_protocol.c`), and the `minimal`, `alarm_demo`, and `multi_sensor` examples. ([1f640ce])
- Documentation set: `PLATO_PROTOCOL.md` (command protocol), plus `DEVELOPER_GUIDE.md`, `TUTORIAL.md`, and `PLUG_AND_PLAY.md` templates. ([185c0c8])
- TernARY upgrade to the engine: a ternary-continuous shim mapping conviction `[0,1]` to trit gates `{-1,0,+1}` (`plato_trit_t`, `plato_conviction_to_trit`, `plato_trit_to_conviction`, `plato_normalise_conviction`, `plato_is_lemminal`); a `PLATO_VETO` severity that overrides and blocks actuator writes while firing (`plato_veto_active`, `plato_veto_source`); symmetry monitoring via cross-correlation between sensor pairs (`plato_alarm_mode_t` with `PLATO_ALARM_STANDARD`/`PLATO_ALARM_SYMMETRY`, `plato_add_symmetry_pair`, `plato_add_symmetry_alarm`, `plato_check_symmetry`); new tunable constants (`PLATO_LEMINAL_LOW`/`_HIGH`, `PLATO_SYMMETRY_WINDOW`/`_THRESHOLD`, `PLATO_MAX_SYMMETRY_PAIRS`); the `examples/symmetry_demo.c` example; and `TernARY_UPGRADE.md`. ([313667c])
- `AGENT.md` (ensign identity doc) and `memory/JOURNAL.md` (duty log). ([a888dc7], [c8a338e])
- GitHub Actions CI workflow, initially as a placeholder ([e7b6c75]) and then made to actually run the suite ([f70b768]) — see Fixed.
- TCP client example (`examples/client.c`) that connects to `plato_server` on `:7070` and exchanges welcome/help/tick/history/quit messages, exiting `0` only on success; wired into the `Makefile` `EXAMPLES` target and into CI. ([f70b768])

### Changed
- Brought `PLATO_PROTOCOL.md` in line with what the code actually emits: documented `symmetry list`, `veto`, the `VETO` severity, the veto-blocked actuator response, the symmetry-mode `alarm list` form, the extra `tick` output lines (veto / per-pair symmetry / `SYM` marker), and the `err server full` connect-reject; bumped the protocol identifier to v1.1. ([f70b768])
- Rewrote `README.md` for the production-readiness pass. ([1953624])
- Removed the SuperInstance/Plato ecosystem framing from `README.md` and `AGENT.md`. ([bf4290f])

### Fixed
- CI gap: the original workflow (`e7b6c75`) only ran `echo "No CI configured — customize per project requirements"` and built/verified nothing. CI now builds and runs the full `make test` suite, builds all binaries and examples, and runs a real client→server integration smoke test (retrying the connection for up to ~5s before failing). ([f70b768], superseding [e7b6c75])
- Protocol/code drift: the TCP server closed the connection on `quit` without sending the spec'd `bye` response — undetected because the unit tests exercised `plato_handle_command()` directly rather than over the wire. The server now sends `bye` before closing, matching the spec. ([f70b768])

[Unreleased]: https://github.com/purplepincher/plato-engine-block-c/commits/HEAD

[1f640ce]: https://github.com/purplepincher/plato-engine-block-c/commit/1f640ce
[185c0c8]: https://github.com/purplepincher/plato-engine-block-c/commit/185c0c8
[313667c]: https://github.com/purplepincher/plato-engine-block-c/commit/313667c
[a888dc7]: https://github.com/purplepincher/plato-engine-block-c/commit/a888dc7
[c8a338e]: https://github.com/purplepincher/plato-engine-block-c/commit/c8a338e
[e7b6c75]: https://github.com/purplepincher/plato-engine-block-c/commit/e7b6c75
[f70b768]: https://github.com/purplepincher/plato-engine-block-c/commit/f70b768
[bf4290f]: https://github.com/purplepincher/plato-engine-block-c/commit/bf4290f
[1953624]: https://github.com/purplepincher/plato-engine-block-c/commit/1953624
