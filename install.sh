#!/usr/bin/env bash
# Copy this project's files into an upstream ns-3-dev checkout and apply the AODV patch.
# Usage: ./install.sh /path/to/ns-3-dev
set -euo pipefail
NS3="${1:?usage: ./install.sh /path/to/ns-3-dev}"
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -x "$NS3/ns3" ] || { echo "error: $NS3 does not look like an ns-3 checkout (no ./ns3)"; exit 1; }

cp "$HERE/scratch/snr-adaptive-routing.cc" "$NS3/scratch/"
cp "$HERE"/utils/*.py "$NS3/utils/"
cp "$HERE"/sweep_results*.csv "$NS3/"

if (cd "$NS3" && git apply --check "$HERE/patches/aodv-force-link-failure.patch" 2>/dev/null); then
  (cd "$NS3" && git apply "$HERE/patches/aodv-force-link-failure.patch")
  echo "AODV patch applied."
elif (cd "$NS3" && git apply --check --reverse "$HERE/patches/aodv-force-link-failure.patch" 2>/dev/null); then
  echo "AODV patch already applied."
else
  echo "error: AODV patch does not apply to this checkout (use upstream commit 7db999bf9 or close to it)"; exit 1
fi
echo "Done. Next: cd $NS3 && ./ns3 configure --enable-examples --enable-tests && ./ns3 build snr-adaptive-routing"
