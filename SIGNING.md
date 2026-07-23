<!-- DRAFT: remove this banner (and the "pending" note below) once the
     SignPath Foundation application is approved and the first signed
     release ships. -->

# Code signing policy

> **Status: pending.** This policy takes effect with the first signed
> release, once the project's SignPath Foundation application is approved.
> Until then, Windows binaries are unsigned — verify downloads against the
> release's `SHA256SUMS.txt`.

## Windows binaries

Free code signing for the Windows releases is provided by
[SignPath.io](https://signpath.io), with a certificate by the
[SignPath Foundation](https://signpath.org).

Signed binaries are built from this repository by the public
[GitHub Actions release pipeline](.github/workflows/release.yml) on version
tags. Only artifacts produced by that pipeline are signed; every release is
manually approved before signing. The binaries carry product name and
version metadata matching the tagged source.

## Team and roles

Olduvai is developed by a single maintainer, who holds all signing roles:

| Role | Person |
|---|---|
| Author / Reviewer / Approver | Krzysztof Sokołowski ([@ksokolowski](https://github.com/ksokolowski)) |

Multi-factor authentication is enforced on both the source repository and
the signing service accounts.

## Privacy

Olduvai makes **no network connections** and collects **no data** of any
kind. It reads the game files you point it at, writes its settings to your
user configuration directory, its cache to your user cache directory, and
bug reports only when you press F5 — all local, nothing transmitted. There
is no telemetry, no update check, no account. The engine can be removed by
deleting the binary and those local directories (see the README's Settings
section for their locations).

## Verifying downloads

Every release ships a `SHA256SUMS.txt`; verify with:

```sh
shasum -a 256 -c SHA256SUMS.txt --ignore-missing
```

Signed Windows binaries additionally carry a digital signature verifiable
in Explorer (Properties → Digital Signatures) showing SignPath Foundation
as the publisher.
