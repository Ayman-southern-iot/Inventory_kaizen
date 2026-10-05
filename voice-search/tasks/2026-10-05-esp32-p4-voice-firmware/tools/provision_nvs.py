#!/usr/bin/env python3
"""
Provision Wi-Fi credentials and the inventory API key into the board's NVS.

Secrets are read from .env, written to a temporary CSV OUTSIDE the repository,
turned into an NVS partition image by ESP-IDF's own nvs_partition_gen.py, flashed,
and the temporary CSV is then deleted. Nothing secret is printed, and nothing
secret is ever written inside the repo tree.

Usage:
    source ~/esp/esp-idf/export.sh
    python tools/provision_nvs.py --port /dev/cu.usbmodem1101
"""

import argparse
import csv
import os
import subprocess
import sys
import tempfile
from pathlib import Path

NAMESPACE = "inventory"

# (env var or list of alternatives, NVS key, required)
# The IMS docs name the key IMS_KEY_INVENTORY_READ, so that is preferred; the generic
# name is kept as a fallback so a mock key still works without renaming anything.
FIELDS = [
    (("WIFI_SSID",), "wifi_ssid", True),
    (("WIFI_PASSWORD",), "wifi_pass", True),
    (("INVENTORY_BASE_URL", "IMS_BASE_URL"), "api_base", False),
    (("IMS_KEY_INVENTORY_READ", "INVENTORY_API_KEY"), "api_key", False),
    (("VOICE_TOKEN",), "voice_tok", False),
    (("VOICE_TOKEN_OPENAI",), "voice_tok2", False),
]


def redact(value: str) -> str:
    """
    Reveal NOTHING of a secret -- not even a prefix.

    An earlier version of this printed value[:4] + "***", which leaked the first four
    characters of the Wi-Fi password to the terminal and the session log. Four
    characters of a password is four characters an attacker does not have to guess.
    Only the length is shown, which is enough to spot a paste error.
    """
    if not value:
        return "<empty>"
    return f"<{len(value)} chars, hidden>"


def size_to_bytes(size: str) -> int:
    """
    '64K' -> 65536. gen_esp32part.py prints human sizes, but nvs_partition_gen.py
    requires an integer byte count that is a multiple of 4096 -- and, nastily, it
    prints 'invalid literal for int()' and STILL EXITS 0 when given '64K', so a
    caller that trusts the return code silently produces no image at all.
    """
    t = str(size).strip().upper()
    mult = 1
    if t.endswith("K"):
        mult, t = 1024, t[:-1]
    elif t.endswith("M"):
        mult, t = 1024 * 1024, t[:-1]
    n = int(t, 0) * mult
    if n % 4096:
        sys.exit(f"error: NVS size {size} ({n} bytes) is not a multiple of 4096")
    return n


def load_env(path: Path) -> dict:
    if not path.exists():
        sys.exit(
            f"error: {path} not found.\n"
            f"       cp {path.parent}/.env.example {path}  and fill it in."
        )
    out = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, v = line.split("=", 1)
        out[k.strip()] = v.strip().strip('"').strip("'")
    return out


def find_partition_offset_size(project: Path, label: str):
    """Read the generated partition table to locate the NVS partition."""
    csv_path = project / "partitions.csv"
    if not csv_path.exists():
        return None, None
    # Offsets in partitions.csv are usually blank (auto-assigned), so prefer the
    # build output if it exists.
    built = project / "build" / "partition_table" / "partition-table.bin"
    if built.exists():
        gen = Path(os.environ["IDF_PATH"]) / "components" / "partition_table" / "gen_esp32part.py"
        txt = subprocess.run([sys.executable, str(gen), str(built)],
                             capture_output=True, text=True).stdout
        for line in txt.splitlines():
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 5 and parts[0] == label:
                return parts[3], parts[4]
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/cu.usbmodem1101")
    ap.add_argument("--label", default="nvs", help="NVS partition label (default: nvs)")
    ap.add_argument("--dry-run", action="store_true", help="build the image but do not flash")
    args = ap.parse_args()

    if "IDF_PATH" not in os.environ:
        sys.exit("error: IDF_PATH not set. Run: source ~/esp/esp-idf/export.sh")

    project = Path(__file__).resolve().parent.parent
    env = load_env(project / ".env")

    rows = []
    missing = []
    for names, key, required in FIELDS:
        val = ""
        chosen = names[0]
        for n in names:
            if env.get(n, ""):
                val, chosen = env[n], n
                break
        if not val:
            if required:
                missing.append(" or ".join(names))
            continue
        if len(names) > 1 and chosen != names[0]:
            print(f"  note: using {chosen} (preferred name is {names[0]})")
        rows.append((key, "data", "string", val))

    if missing:
        sys.exit("error: missing required key(s) in .env: " + ", ".join(missing))

    print(f"provisioning namespace '{NAMESPACE}' with {len(rows)} key(s):")
    for key, _, _, val in rows:
        # The SSID is broadcast over the air, so it is not a secret -- and printing
        # it is what makes "provisioned the wrong network" diagnosable.
        shown = f'"{val}"' if key in ("wifi_ssid", "api_base") else redact(val)
        print(f"    {key:10s} = {shown}")

    offset, size = find_partition_offset_size(project, args.label)
    if not offset:
        sys.exit(f"error: could not locate the '{args.label}' partition. Run 'idf.py build' first.")
    print(f"target partition '{args.label}' at {offset}, size {size}")

    gen = Path(os.environ["IDF_PATH"]) / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
    if not gen.exists():
        sys.exit(f"error: nvs_partition_gen.py not found at {gen}")

    # Temp dir OUTSIDE the repo so a secret-bearing CSV can never be committed.
    with tempfile.TemporaryDirectory(prefix="nvsprov-") as tmp:
        tmpd = Path(tmp)
        csv_path = tmpd / "nvs.csv"
        bin_path = tmpd / "nvs.bin"

        with csv_path.open("w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["key", "type", "encoding", "value"])
            w.writerow([NAMESPACE, "namespace", "", ""])
            for row in rows:
                w.writerow(row)

        size_bytes = size_to_bytes(size)
        # --outdir is required: without it the tool resolves `output` against the
        # CURRENT directory, which would drop a secret-bearing image into the repo.
        r = subprocess.run(
            [sys.executable, str(gen), "generate", "--outdir", str(tmpd),
             str(csv_path), bin_path.name, str(size_bytes)],
            capture_output=True, text=True)
        # Do NOT trust the return code -- this tool exits 0 on at least some argument
        # errors. The only reliable success signal is the file existing.
        if not bin_path.exists():
            sys.exit("error: nvs_partition_gen produced no image.\n"
                     f"  exit={r.returncode}\n  {r.stdout.strip()[-800:]}\n  {r.stderr.strip()[-800:]}")
        print(f"built NVS image ({bin_path.stat().st_size} bytes, {size_bytes}-byte partition)")

        if args.dry_run:
            print("--dry-run: not flashing")
            return

        r = subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32p4", "-p", args.port,
             "write_flash", offset, str(bin_path)])
        if r.returncode != 0:
            sys.exit("error: flashing the NVS image failed")

    print("done. Secrets are in NVS; the temporary CSV has been deleted.")


if __name__ == "__main__":
    main()
