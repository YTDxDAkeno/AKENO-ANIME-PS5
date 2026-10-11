# Testing

The repository separates fast host checks from behavior that only real PS5
hardware can prove.

## Commands

| Command | Scope |
| --- | --- |
| `make test-deps` | Fetch and verify the pinned host-only GoogleTest source. |
| `make test-unit` | Compile and run the host-native GoogleTest application tests. |
| `make test-integration` | Exercise repository scripts through subprocesses and temporary files. |
| `make test-update-check` | Check the [update check](UPDATE_CHECK.md): versions, parsing, decisions, hostile input, under sanitizers. |
| `make test` | Run every host test suite. |
| `make check` | Run linting, all host tests, and a complete folder build. |

GitHub Actions runs `make test-unit` and `make test-integration` as separate
Ubuntu steps, so every pull request executes both layers with clear failure
reporting, and then packages an installable build of the pull request
([Pull-request builds](PULL_REQUEST_BUILDS.md)). Host tests must remain deterministic, must never contact a console,
and must be safe to run in parallel with unrelated console work. The first
unit-test run downloads a pinned GoogleTest archive after verifying its
SHA-256; later runs reuse `.deps/test/`.

Run one test or suite with normal GoogleTest arguments:

```bash
make test-unit GTEST_ARGS='--gtest_filter=AssetTextTest.MissingAssetUsesFallback'
```

## Unit-test policy

Write unit tests for reusable logic with meaningful behavior: parsers, state
transitions, bounds handling, input mapping, protocol messages, resource
ownership, and error paths. Keep platform calls behind a small boundary so the
logic can compile and run on Linux without a PS5 or proprietary SDK.

The starter suite in `tests/test_demo_renderer.cpp` uses GoogleTest to validate
fallback, line-ending, truncation, and null-termination behavior for packaged
text assets. GoogleTest is a host-only development dependency: it is never
compiled into `eboot.bin`, `libc.prx`, or a PS5 package.

Do not add tests for trivial constants or one-line drawing calls merely to
increase a coverage percentage. Test observable contracts and regressions.

## Host integration tests

`tests/test_tools.py` invokes complete repository scripts with temporary input
and controlled environment variables. Use this level for metadata updates,
build orchestration, package validation, and deployment resolution. Network
operations must be mocked or use an explicit dry-run mode; host CI must never
contact a console.

Each test must clean up its files, avoid shared mutable state, and include the
failure case that would have caught the associated bug.

## Real-network acceptance (AKENO STREAM)

`RealNetwork.AuthorizedProviderToNativePlayerEndToEnd`
(`tests/unit/test_real_network.cpp`) is the one test that uses the real
internet. It is skipped unless `AKENO_NETWORK_ACCEPTANCE=1`, so `make
test-unit` stays deterministic and offline. The *Real-network acceptance*
workflow (`.github/workflows/network-acceptance.yml`) runs it on every push
to `claude/**` and `release/**` and on demand (with another PeerTube instance
or channel as inputs). It is a separate workflow and never gates a release: a
third-party outage says nothing about the build.

The test drives the real App through the controller from an empty data
folder: *Add Streaming Provider* with `video.blender.org` (the Blender
Foundation's PeerTube instance; Creative Commons open movies), detection, the
provider's home and catalogue, a series and season whose first episode plays,
AKENO's player (FFmpeg's software decoder in place of the PS5's hardware
decoder), the provider's *Native playback verified* state, subtitles where the
episode has captions, pause and resume, seeking both ways, the next and
previous episode, saved progress, a restart and the series' *Continue*. The
step table goes to the job log and summary; the report and screenshots are
uploaded as an artifact.

It found two defects the offline fixtures could not show: a PeerTube instance
answering 406 to AKENO's JSON-first detection request (CHANGELOG, rc.2
*Fixed*), and a pause during start-up reading *Buffering*.

Locally (with network access):

```bash
AKENO_NETWORK_ACCEPTANCE=1 AKENO_NETWORK_REPORT=build/network.md \
  make test-unit GTEST_ARGS='--gtest_filter=RealNetwork.*'
```

A pass here is a host result over the real network, not a console result:
hardware checklist section 15 is still required.

## PS5 integration validation

Rendering, controller input, AudioOut, mounted paths, launch/closure behavior,
and firmware compatibility require hardware validation. A passing host suite
does not prove those properties.

For a hardware milestone:

1. Build an exact candidate from a clean commit and record its digest.
2. Acquire the shared console lock only for the test window.
3. Deploy the title through the documented LAN-only procedure.
4. Capture the expected visual result and relevant logs.
5. Close the title, release the lock, and record firmware, loader, result, and
   artifact identity.
6. Commit the validation record separately from the implementation when the
   project workflow requires one.

Follow [Deployment](DEPLOYMENT.md) and the separate
[PS5 Homebrew Development Protocol](https://github.com/blackbearreloaded/ps5-homebrew-dev-protocol)
for console coordination, evidence collection, and milestone policy.

## Adding tests

- Add GoogleTest cases to C++ files under `tests/`; the `test-unit` recipe owns
  their host-only compilation.
- Add Python subprocess tests as `tests/test_*.py`; discovery is automatic.
- Preserve the GPL header on every test source.
- Run `make test`, `make lint`, and `make` before submitting a change.
- Reserve real-console claims for recorded hardware results.
