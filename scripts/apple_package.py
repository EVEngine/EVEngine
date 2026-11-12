#!/usr/bin/env python3
"""Assemble and verify Apple game bundles from installed EVEngine SDKs."""
from __future__ import annotations

import argparse
import datetime
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

import utf8_stdio


class PackageError(RuntimeError):
    """A consumer input or Apple tool failed before publication."""


def run(*args: str) -> bytes:
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise PackageError(f"{args[0]} failed ({result.returncode}): "
                           + result.stderr.decode("utf-8", errors="replace"))
    return result.stdout


def load_profile(path: Path, bundle_id: str, identity: str, device: str) -> dict:
    profile = plistlib.loads(run("/usr/bin/security", "cms", "-D", "-i", str(path)))
    if profile["ExpirationDate"] <= datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None):
        raise PackageError("Provisioning profile has expired; renew it in Xcode or Apple Developer.")
    entitlements = profile["Entitlements"]
    team = profile["TeamIdentifier"][0]
    prefix = profile["ApplicationIdentifierPrefix"][0]
    if not fnmatch.fnmatchcase(f"{prefix}.{bundle_id}", entitlements["application-identifier"]):
        raise PackageError("Provisioning profile does not authorize this bundle ID.")
    if not entitlements.get("get-task-allow") or not profile.get("ProvisionedDevices"):
        raise PackageError("This command requires a development provisioning profile for device testing.")
    if device and device not in profile["ProvisionedDevices"]:
        raise PackageError("Device UDID is not authorized by the provisioning profile.")
    identities = run("/usr/bin/security", "find-identity", "-v", "-p", "codesigning").decode("utf-8")
    matches = re.findall(r'\) ([0-9A-F]{40}) "([^"\n]+)"', identities)
    selected = [(fingerprint, name) for fingerprint, name in matches
                if identity.upper() == fingerprint or identity == name]
    allowed = {hashlib.sha1(cert).hexdigest().upper() for cert in profile["DeveloperCertificates"]}
    if len(selected) != 1 or selected[0][0] not in allowed:
        raise PackageError("Signing identity must uniquely match a valid profile certificate with a private key.")
    result = dict(entitlements)
    result["application-identifier"] = f"{prefix}.{bundle_id}"
    result["com.apple.developer.team-identifier"] = team
    result["keychain-access-groups"] = [f"{prefix}.{bundle_id}" if value == f"{prefix}.*" else value
                                         for value in result.get("keychain-access-groups", [])]
    if any("*" in str(value) for value in result.values()):
        raise PackageError("Profile contains unresolved wildcard entitlements; use an explicit app profile.")
    return result


def validate_paths(game: Path, sdk: Path, output: Path) -> None:
    for filename in ("main.nut", "config.nut"):
        if not (game / filename).is_file():
            raise PackageError(f"Game is missing {filename}: {game}")
    if output.exists() or output.is_symlink():
        raise PackageError(f"Output already exists; choose a new directory: {output}")
    for source in (game, sdk):
        if output == source or source in output.parents or output in source.parents:
            raise PackageError("Output must be separate from the game and SDK directories.")
    for path in game.rglob("*"):
        if path.is_symlink():
            raise PackageError(f"Game resource symlinks are not supported: {path}")


def sign_bundle(app: Path, identity: str, entitlements: Path | None = None) -> None:
    # Nested code first. Never use --deep as a substitute for signing each owner.
    nested = [path for path in app.rglob("*") if not path.is_symlink()
              and (path.suffix in (".dylib", ".framework", ".appex", ".app", ".xpc"))]
    for path in sorted(nested, key=lambda item: len(item.parts), reverse=True):
        run("/usr/bin/codesign", "--force", "--sign", identity, "--timestamp=none", str(path))
    args = ["/usr/bin/codesign", "--force", "--sign", identity, "--timestamp=none"]
    if entitlements:
        args += ["--entitlements", str(entitlements)]
    run(*args, str(app))
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", str(app))


def build(args: argparse.Namespace) -> Path:
    if sys.platform != "darwin":
        raise PackageError("Apple bundle assembly requires macOS and Xcode command line tools.")
    game, sdk, output = (Path(value).expanduser().resolve() for value in (args.game, args.sdk, args.output))
    validate_paths(game, sdk, output)
    expected = "macosx" if args.platform == "macos" else "ios"
    if (sdk / "share/eve/TARGET_PLATFORM").read_text(encoding="utf-8").strip() != expected:
        raise PackageError(f"Expected a {expected} SDK.")
    if not re.fullmatch(r"[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+", args.bundle_id):
        raise PackageError("Bundle ID must be a reverse-DNS identifier.")
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", args.version):
        raise PackageError("Version must contain one to three numeric components.")
    if not args.name or args.name in (".", "..") or any(c in args.name for c in '/\\\x00'):
        raise PackageError("App name must be a single filename.")
    entitlements = None
    if args.platform == "ios":
        if args.simulator:
            if args.profile or args.sign_identity or args.unsigned or args.device:
                raise PackageError("Simulator apps use ad-hoc signing; device signing options are invalid.")
        elif args.unsigned:
            if args.profile or args.sign_identity or args.device:
                raise PackageError("Unsigned assembly cannot sign, install or launch on a device.")
        else:
            if not args.profile or not args.sign_identity:
                raise PackageError("iOS needs --sign-identity and --profile, or explicit --unsigned for assembly only.")
            entitlements = load_profile(Path(args.profile), args.bundle_id, args.sign_identity, args.device)
    elif args.profile or args.simulator or args.unsigned or args.device:
        raise PackageError("Profile, simulator, unsigned and device options are iOS-only.")
    elif args.sign_identity:
        raise PackageError("macOS builds currently support local ad-hoc signing; Developer ID distribution is separate.")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".eve-apple-", dir=output.parent) as temporary:
        stage = Path(temporary) / "result"
        stage.mkdir()
        app = stage / f"{args.name}.app"
        if args.platform == "macos":
            executable_dir = app / "Contents/MacOS"
            executable_dir.mkdir(parents=True)
            resources = app / "Contents/Resources"
            resources.mkdir()
            shutil.copy2(sdk / "bin/eve", executable_dir / "eve")
            libraries = app / "Contents/Frameworks"
            libraries.mkdir()
            for library in (sdk / "lib").iterdir():
                if library.suffix == ".dylib":
                    shutil.copy2(library, libraries / library.name)
            for required in ("libvulkan.1.dylib", "libMoltenVK.dylib"):
                if not (libraries / required).is_file():
                    raise PackageError(f"SDK is missing runtime library {required}.")
            manifest = json.loads((sdk / "lib/MoltenVK_icd.json").read_text(encoding="utf-8"))
            manifest["ICD"]["library_path"] = "../Frameworks/libMoltenVK.dylib"
            (resources / "MoltenVK_icd.json").write_text(json.dumps(manifest), encoding="utf-8")
            run("/usr/bin/install_name_tool", "-add_rpath", "@loader_path/../Frameworks",
                str(executable_dir / "eve"))
            plist_path = app / "Contents/Info.plist"
            info = {"CFBundleExecutable": "eve", "CFBundlePackageType": "APPL",
                    "NSHighResolutionCapable": True, "LSMinimumSystemVersion": "13.3"}
            game_target = resources
        else:
            shutil.copytree(sdk / "bin/eve.app", app, symlinks=True)
            plist_path = app / "Info.plist"
            info = plistlib.loads(plist_path.read_bytes())
            executable_dir = app
            binary = app / info["CFBundleExecutable"]
            platform = run("/usr/bin/xcrun", "vtool", "-show-build", str(binary)).decode("utf-8")
            required = "IOSSIMULATOR" if args.simulator else "IOS"
            if not re.search(rf"platform\s+{required}\s", platform):
                raise PackageError(f"SDK executable is not built for {required}; device and simulator SDKs differ.")
            if (app / "game").exists():
                shutil.rmtree(app / "game")
            for archive in list(app.glob("*.eve")) + list(app.glob("*.love")):
                archive.unlink()
            (app / "embedded.mobileprovision").unlink(missing_ok=True)
            game_target = app / "game"
        for signature in app.rglob("_CodeSignature"):
            shutil.rmtree(signature)
        shutil.copytree(game, game_target, dirs_exist_ok=True)
        executable = executable_dir / info["CFBundleExecutable"]
        executable.chmod(executable.stat().st_mode | 0o111)
        info.update(CFBundleIdentifier=args.bundle_id, CFBundleName=args.name,
                    CFBundleDisplayName=args.name, CFBundleShortVersionString=args.version,
                    CFBundleVersion=args.version)
        plist_path.write_bytes(plistlib.dumps(info))
        if args.platform == "ios" and args.unsigned:
            # A copied shell may have an old embedded executable signature.
            run("/usr/bin/codesign", "--remove-signature", str(executable))
        else:
            entitlement_path = None
            if entitlements is not None:
                shutil.copy2(args.profile, app / "embedded.mobileprovision")
                entitlement_path = Path(temporary) / "entitlements.plist"
                entitlement_path.write_bytes(plistlib.dumps(entitlements))
            sign_bundle(app, args.sign_identity or "-", entitlement_path)
        if args.platform == "ios" and not args.simulator and not args.unsigned:
            with zipfile.ZipFile(stage / f"{args.name}.ipa", "w", zipfile.ZIP_DEFLATED) as archive:
                for path in sorted(app.rglob("*")):
                    if path.is_symlink():
                        raise PackageError("IPA resources must not contain symlinks.")
                    if path.is_file():
                        archive.write(path, Path("Payload") / app.name / path.relative_to(app))
        os.replace(stage, output)
    published = output / app.name
    print(f"Built {'UNSIGNED assembly only' if args.unsigned else 'verified signed app'} -> {published}")
    if args.device:
        run("/usr/bin/xcrun", "devicectl", "device", "install", "app", "--device", args.device, str(published))
        run("/usr/bin/xcrun", "devicectl", "device", "process", "launch", "--device", args.device, args.bundle_id)
    return published


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("macos", "ios"))
    parser.add_argument("game")
    parser.add_argument("--sdk", required=True)
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("--name", default="Game")
    parser.add_argument("--bundle-id", default="com.evengine.game")
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--sign-identity", default="")
    parser.add_argument("--profile", default="")
    parser.add_argument("--device", default="", help="Provisioned device UDID; install and launch after signing")
    parser.add_argument("--unsigned", action="store_true")
    parser.add_argument("--simulator", action="store_true")
    try:
        build(parser.parse_args())
        return 0
    except (PackageError, OSError, ValueError, KeyError) as error:
        print(f"eve build: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
