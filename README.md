# SNR-Triggered Adaptive Routing for Ad Hoc Networks (ns-3)

An [ns-3](https://www.nsnam.org/) experiment comparing stock **AODV** routing
with an **SNR-adaptive** variant in a mobile ad hoc network.

In the adaptive variant every node measures the signal-to-noise ratio (SNR)
towards its neighbours. When the smoothed SNR to a neighbour stays below a
threshold, only the routes that use that neighbour as next hop are invalidated,
so AODV looks for another path before the link actually breaks.

This repository contains **only our code**. It is overlaid on an upstream
ns-3-dev checkout; the single change to upstream code is shipped as a patch.

## Quick start

Requirements: a C++23 compiler, CMake 3.20+, Ninja (recommended), Python 3.
The plotting scripts also need `pip install pandas numpy matplotlib`.

```bash
# 1. upstream ns-3 at the tested commit
git clone https://gitlab.com/nsnam/ns-3-dev.git
git -C ns-3-dev checkout 7db999bf9

# 2. this repository
git clone https://github.com/Gen3ratorX/ns3-dev.git snr-adaptive

# 3. copy our files into ns-3 and apply the AODV patch
./snr-adaptive/install.sh ./ns-3-dev

# 4. build
cd ns-3-dev
./ns3 configure --enable-examples --enable-tests
./ns3 build snr-adaptive-routing
```

## Run it

Each run prints one CSV line:
`protocol,nodes,throughput_Mbps,PDR_percent,delay_ms,loss_percent`.

```bash
# plain AODV
./ns3 run "snr-adaptive-routing --nNodes=30 --simTime=40 --protocol=conventional --RngRun=1"

# SNR-adaptive (reroute events are printed to stderr)
./ns3 run "snr-adaptive-routing --nNodes=30 --simTime=40 --protocol=adaptive --snrThresholdDb=15 --RngRun=1" 2>&1 | grep -E "reroute|adaptive,"
```

Use the same `--RngRun` for both to get identical mobility and traffic.

Threshold sweep (10-20 dB, seeds 1-5, about 1-2 minutes) and figures:

```bash
python3 utils/snr-threshold-sweep.py           # writes sweep_results.csv
python3 utils/plot-snr-sweep.py sweep_results.csv .   # overview figures
python3 utils/plot-snr-thesis.py               # individual figures in thesis_figures/
```

Optional NetAnim output: add `--animFile=snr-adaptive.xml` to any run (the
NetAnim viewer is a separate application).

## Options

| Option | Default | Meaning |
|---|---|---|
| `--protocol` | `adaptive` | `conventional` or `adaptive` |
| `--nNodes` | 40 | Number of nodes |
| `--simTime` | 100 | Simulation length (s) |
| `--snrThresholdDb` | 8 | SNR below which a link counts as degraded |
| `--snrAlpha` | 0.3 | EWMA weight of the newest SNR sample |
| `--lowWindows` | 3 | Consecutive low 1 s windows before rerouting |
| `--RngRun` | 1 | Random seed |
| `--animFile` | empty | NetAnim XML output path |

## What is in this repository

| Path | Purpose |
|---|---|
| `scratch/snr-adaptive-routing.cc` | The simulation |
| `patches/aodv-force-link-failure.patch` | Adds `aodv::RoutingProtocol::ForceLinkFailure(nextHop)` |
| `install.sh` | Overlays our files onto an ns-3 checkout and applies the patch |
| `utils/` | Sweep and plotting scripts |
| `sweep_results.csv` | Results of the last sweep (60 runs) |
| `thesis_figures/` | Individual figures (PDF and PNG) |
| `SNR_ADAPTIVE_ROUTING.md` | Design, results and interpretation |
| `HANDOVER_GUIDE.md` | Full setup, usage, troubleshooting and known issues |

## Status and known issues

- With the current data the adaptive protocol shows **no clear improvement**
  over plain AODV (5 seeds, 30 nodes, 40 s runs).
- **All nodes currently start at (0,0)** because no starting-position allocator
  is set, so the results describe a dense cluster, not a 600 x 600 m spread.
  Adding the start-position line makes stock AODV stall at about 18-19 s of
  simulated time (cause not yet found), so it has not been applied.

Read `HANDOVER_GUIDE.md`, section 9, before relying on any numbers.

## License

Upstream ns-3 is GPL-2.0-only, and the AODV patch is derived from ns-3 code, so
it carries the same license. No license has been chosen yet for the remaining
original files in this repository.
