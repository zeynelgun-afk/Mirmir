# Direct Flatpak distribution

The repository includes an AI-assisted **upstream direct-download Flatpak**
repackager. It is not a Flathub submission manifest, and this project does not
automate a Flathub submission or review interaction.

## Flathub boundary

The [Flathub requirements](https://docs.flathub.org/docs/for-app-authors/requirements#generative-ai-policy)
checked on 2026-10-27 require disclosure of known AI-generated material, prohibit
AI-generated/assisted Flathub manifests, and prohibit AI agents from opening or
automating submission PRs and review interactions. Application code,
documentation and this upstream packaging substantially include AI-generated
material. Unreal Engine and third-party assets have separate provenance and
licenses.

A human maintainer considering Flathub must review the current requirements,
prepare an independent compliant manifest, examine distribution licenses,
disclose AI involvement accurately and maintain the submission personally.
Human preparation does not guarantee acceptance. The direct bundle is not listed,
verified or reviewed by Flathub. Using a runtime from Flathub does not list the
application there.

## Build

First prepare a completed Linux Shipping game, private CPython/PyQt runtime and
license directory using the [installer tooling](../packaging/README.md). Then:

```bash
python packaging/flatpak/build-bundle.py \
  --stage /absolute/path/to/Mirmir --version 0.5.0
```

The script rejects external symlinks and absolute package-internal manifest paths,
checks the real ELF game binary and stages existing files under `/app`. It runs
`flatpak build-finish`, `build-export` and `build-bundle`. This repackaging step does
not need an SDK or flatpak-builder because it does not compile the game. The
Shipping game must already have been built separately. See the [Flatpak command
reference](https://docs.flatpak.org/en/latest/flatpak-command-reference.html).

Outputs appear under `.local/dist/flatpak/<version>-<timestamp>/`: the `.flatpak`,
`SHA256SUMS`, `result.json`, local OSTree repository and build log. Bundles are
currently unsigned; SHA256 checks transfer integrity, not publisher identity.

## Install and run

```bash
flatpak install --user ./Mirmir-0.5.0-x86_64.flatpak
flatpak run io.github.zeynelgun_afk.Mirmir
flatpak run io.github.zeynelgun_afk.Mirmir --stop
flatpak uninstall --user io.github.zeynelgun_afk.Mirmir
```

The `org.freedesktop.Platform/x86_64/25.08` runtime and GPU extension are required.
The bundle does not contain the runtime; installation may download a missing
runtime. The application itself has no network permission. See the [single-file
bundle documentation](https://docs.flatpak.org/en/latest/single-file-bundles.html).

## Sandbox permissions

Permissions are limited to X11, shared IPC, DRI/GPU, PulseAudio and communication
with `org.kde.StatusNotifierWatcher`. There is no host/home filesystem grant,
unrestricted session/system bus or network grant. X11 supports desktop placement
through XWayland; it is not a strong screen-isolation boundary. Flatpak's
PulseAudio permission also permits input, but the application does not use the
microphone. The actual permission scope is described in the [sandbox permission
guide](https://docs.flatpak.org/en/latest/sandbox-permissions.html).

Application data stays under `~/.var/app/<application-id>/`; live frames use its
own private runtime subtree. The ordinary source/portable Mirmir save and lease
are not shared. No other application, runtime or desktop settings are modified.

## Verification

```bash
python packaging/flatpak/verify-sandbox.py --require-props
```

The verifier launches only this installed application ID in a fresh test
namespace, disables cursor interest and never moves or clicks the real pointer.
Because Flatpak reinitializes XDG base variables, a validated 32-character
`MIRMIR_VERIFY_NAMESPACE` is applied by the launcher inside the sandbox.

Checks include the real Shipping PID/session, PNG CRC/dimensions/alpha, habitat
frames, engine-confirmed Feed/Play, petting without cancelling the game, Rest and
normal shutdown. Raw producer PNGs are retained unchanged. Current protocol also
pairs the depth-tested visible-cat input mask with its main frame. Desktop
mapping, own-window taskbar hints and input-region checks are distinct from
application-internal readiness.

If the user's session is locked, the verifier does not unlock it or alter KWin.
An explicit `--allow-locked-session` runs render/IPC/shutdown checks while keeping
`actual_desktop_overlay_verified=false` and recording visibility as unverified.
That option does not establish visible desktop operation, focus, real clicks or
speaker output. A successful package build alone establishes none of these.

Artifacts remain under `.local/flatpak-verification/<test-id>/`. Cleanup targets
only the created Flatpak instance; the ordinary desktop cat is not stopped.
The result record separately states what was built, imported, rendered and
actually mapped. Fresh-distribution installations and real Windows behavior
remain separate verification tasks.
