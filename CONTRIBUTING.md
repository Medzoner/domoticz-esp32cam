# Contributing to domoticz-esp32cam

Thank you for helping make this firmware more useful and reliable. Contributions
of code, documentation, hardware test results, board definitions, and bug reports
are welcome.

By participating, you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
For usage questions, prefer [GitHub Discussions](https://github.com/Medzoner/domoticz-esp32cam/discussions).
Use GitHub Issues for reproducible defects and agreed feature work. Report
security problems according to [SECURITY.md](SECURITY.md), never in a public
issue.

## Before you start

- Search existing issues and pull requests.
- Open an issue before a large feature, architectural change, or new board port.
- Keep pull requests focused. Unrelated refactors make embedded regressions
  harder to diagnose.
- Never commit `include/secrets.h`, Wi-Fi credentials, session cookies, camera
  recordings, private addresses, or `.pio/` output.

## Development setup

The project uses [PlatformIO](https://platformio.org/) and Python 3.12 in CI.

```bash
git clone https://github.com/Medzoner/domoticz-esp32cam.git
cd domoticz-esp32cam
python -m venv .venv
source .venv/bin/activate
python -m pip install --requirement requirements-ci.txt
cp include/secrets.example.h include/secrets.h
```

Use placeholder credentials for native checks and builds. Put real credentials
only in the ignored `include/secrets.h` file.

## Architecture

Keep platform-independent behavior in `src/core/`, contracts in `src/ports/`,
and Arduino, networking, storage, and HTTP details in `src/adapters/`.
`src/main.cpp` is the composition root. Web source files live in `web/` and are
embedded at build time by `scripts/embed_web_assets.py`.

New business rules should normally have native Unity tests in
`test/test_core/`. Hardware-specific code should remain behind a port where that
is practical.

## Run the checks

Before opening a pull request, run the same essential checks as CI:

```bash
pio test --environment native
pio test --environment native-sanitized
pio test --environment native-storage --environment native-storage-sanitized
python -m unittest discover -s test/python -v
node --test test/web/recording_health.test.cjs
pio check --environment native
pio check --environment esp32cam
for environment in esp32cam esp-eye esp32s3-cam-lcd m5stack-camera; do
  pio run --environment "$environment"
done
```

For hardware-facing changes, flash every affected board you can access and add
the board, sensor, browser/client, and result to the pull request. A lack of
hardware access is not automatically a blocker, but it must be stated clearly.
Use the [reliability contract and hardware acceptance protocol](docs/RELIABILITY.md)
to distinguish build coverage from measured hardware behavior.

## Code style

- Use C++17 and the repository's `.clang-format` configuration.
- Preserve the dependency direction from adapters toward ports and core.
- Avoid dynamic allocation in hot camera and streaming paths unless it is
  bounded and justified.
- Treat memory, socket deadlines, and task stack usage as constrained resources.
- Update the README and HTTP API table for user-visible behavior.

Format C++ files with:

```bash
clang-format -i path/to/changed_file.cpp path/to/changed_file.h
```

## Commits and versions

Use [Conventional Commits](https://www.conventionalcommits.org/) for commit
messages and pull request titles:

- `fix: prevent stalled stream clients` creates a patch release;
- `feat: add a camera control` creates a minor release;
- `feat!: change the HTTP API` creates a major release;
- `docs:`, `test:`, `refactor:`, `build:`, `ci:`, and `chore:` describe changes
  that do not independently request a release.

Maintainers squash pull requests with a Conventional Commit title. Release
Please keeps a release PR and `CHANGELOG.md` current. Merging that release PR
creates the SemVer tag, GitHub Release, checksums, and firmware assets. See
[the release guide](docs/RELEASING.md).

## Pull request review

A pull request is ready when it:

1. explains the user-visible reason for the change;
2. includes focused tests or a documented reason they are not applicable;
3. passes CI for all supported boards;
4. documents hardware validation and residual risk; and
5. contains no secrets or unrelated generated files.

Maintainers may ask for changes to protect compatibility with constrained
boards and existing MotionEye, Home Assistant, OctoPrint, and Domoticz clients.
