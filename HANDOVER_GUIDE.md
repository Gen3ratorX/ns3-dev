# Handover Guide: SNR-Triggered Adaptive Routing (ns-3)

This guide is for someone receiving the repository who needs to build it, run
it, reproduce the results and understand what is (and is not) trustworthy.
Read section 9 (Known issues) before using any numbers.

For the scientific background and results discussion see
`SNR_ADAPTIVE_ROUTING.md`.

## 1. What this is

An experiment for the ns-3 network simulator comparing stock AODV routing with
an "SNR-adaptive" variant in a mobile ad hoc network. In the adaptive variant,
each node monitors the signal-to-noise ratio (SNR) towards its neighbours and,
when the smoothed SNR to a neighbour stays below a threshold, invalidates only
the routes that use that neighbour as next hop, so AODV finds another path
before the link breaks.

**This repository contains only our code, not ns-3 itself.** It is meant to be
overlaid on an upstream ns-3-dev checkout (section 4). The one change to
upstream code is shipped as a patch.

- Upstream base: https://gitlab.com/nsnam/ns-3-dev.git at commit `7db999bf9`
  (the patch was verified to apply cleanly there).
- Background and results discussion: `SNR_ADAPTIVE_ROUTING.md`.

## 2. Prerequisites

| Need | Notes |
|---|---|
| C++ compiler with C++23 support | Apple clang 21 (Xcode) was used; GCC 13+ on Linux should also work |
| CMake 3.20 or newer | `brew install cmake` on macOS |
| Ninja (recommended) | `brew install ninja` |
| Python 3 | Used by the `./ns3` wrapper and the scripts |
| Python packages | `pip install pandas numpy matplotlib` (only for the plotting scripts) |
| Disk / time | Full first build compiles about 300 targets; allow tens of minutes |

The NetAnim viewer is optional and is a separate application (section 8).

## 3. Repository map

| Path | Purpose |
|---|---|
| `scratch/snr-adaptive-routing.cc` | The simulation (both `conventional` and `adaptive` modes, optional NetAnim output) |
| `patches/aodv-force-link-failure.patch` | Adds public `ForceLinkFailure(Ipv4Address nextHop)` to `aodv::RoutingProtocol` (`src/aodv/model`) |
| `install.sh` | Copies the files below into an ns-3 checkout and applies the patch |
| `utils/snr-threshold-sweep.py` | Runs the threshold x seed sweep, writes `sweep_results.csv` |
| `utils/plot-snr-sweep.py` | Three overview figures from the CSV |
| `utils/plot-snr-thesis.py` | Ten individual thesis-style figures (PDF + PNG) |
| `sweep_results_spread.csv` | Sweep results, nodes spread over the area (60 runs, recommended) |
| `thesis_figures_spread/` | Individual figures for that sweep |
| `sweep_results.csv` | Legacy sweep, all nodes start at (0,0) (60 runs) |
| `thesis_figures/` | Individual figures for the legacy sweep |
| `SNR_ADAPTIVE_ROUTING.md` | Design, results and interpretation |
| `HANDOVER_GUIDE.md` | This file |

## 4. Get and build

```bash
# 1. upstream ns-3 at the tested commit
git clone https://gitlab.com/nsnam/ns-3-dev.git
cd ns-3-dev
git checkout 7db999bf9
cd ..

# 2. this repository
git clone https://github.com/Gen3ratorX/ns3-dev.git snr-adaptive
cd snr-adaptive

# 3. overlay our files onto ns-3 and apply the AODV patch
./install.sh ../ns-3-dev

# 4. build
cd ../ns-3-dev
./ns3 configure --enable-examples --enable-tests
./ns3 build snr-adaptive-routing
```

`install.sh` copies the scratch program, the `utils/` scripts and
`sweep_results.csv` into the ns-3 tree and runs `git apply` for the patch. All
commands in the rest of this guide are run **from the ns-3 directory**.

Doing it by hand instead: copy the files as listed in section 3, then
`git apply patches/aodv-force-link-failure.patch` from the ns-3 root.

The ns-3 clone contains the full upstream history, so it is large.

## 5. Run a single simulation

Each run prints one CSV line on stdout:
`protocol,nodes,throughput_Mbps,PDR_percent,delay_ms,loss_percent`.
Progress and debug output go to stderr.

Baseline (plain AODV):

```bash
./ns3 run "snr-adaptive-routing --nNodes=30 --simTime=40 --protocol=conventional --RngRun=1"
```

Adaptive:

```bash
./ns3 run "snr-adaptive-routing --nNodes=30 --simTime=40 --protocol=adaptive --snrThresholdDb=15 --RngRun=1" 2>&1 | grep -E "reroute|adaptive,"
```

`[reroute]` lines show each early route invalidation, for example:

```
[reroute] t=2.5s next-hop 10.0.0.9 smoothed SNR 4.9dB below threshold for 3 windows, invalidating its routes
```

Using the same `--RngRun` for both modes gives the same node movement and
traffic, which is what makes paired comparison valid.

### Command-line options

| Option | Default | Meaning |
|---|---|---|
| `--protocol` | `adaptive` | `conventional` or `adaptive` |
| `--nNodes` | 40 | Number of nodes |
| `--simTime` | 100 | Simulation length in seconds |
| `--snrThresholdDb` | 8 | SNR below which a link counts as degraded |
| `--snrAlpha` | 0.3 | EWMA weight of the newest SNR sample |
| `--lowWindows` | 3 | Consecutive low 1 s windows required before rerouting |
| `--RngRun` | 1 | Random seed (built-in ns-3 option) |
| `--animFile` | empty | Path of a NetAnim XML to write (empty = off) |
| `--spreadStart` | 0 | 1 = start nodes at random positions (0 = all nodes start at (0,0), the legacy behaviour) |
| `--aodvQueueLen` | 1000 | AODV request queue length (ns-3 default 64 can livelock, see known issues) |

List all options with `./ns3 run "snr-adaptive-routing --PrintHelp"`.

## 6. Reproduce the sweep and the figures

Run from the ns-3 root, after building:

```bash
# recommended: nodes spread over the area
python3 utils/snr-threshold-sweep.py --spreadStart=1 --out=sweep_results_spread.csv
python3 utils/plot-snr-thesis.py sweep_results_spread.csv thesis_figures_spread

# legacy: all nodes start at (0,0)
python3 utils/snr-threshold-sweep.py
python3 utils/plot-snr-thesis.py
python3 utils/plot-snr-sweep.py sweep_results.csv .
```

- Runs conventional AODV once per seed and adaptive at every threshold from 10
  to 20 dB, for seeds 1 to 5 (60 runs, 30 nodes, 40 s each), six in parallel.
  Takes about 1 to 2 minutes.
- Any other arguments you pass are forwarded to the simulation, and
  `--out=<file>` sets the output CSV (default `sweep_results.csv`).
- Uses `./ns3 run --no-build`, so build first; it will not rebuild for you.

CSV columns: `protocol, threshold, seed, reroutes, throughput_mbps, pdr,
delay_ms, loss`. Conventional rows have `threshold = 0` and `reroutes = 0`.

`plot-snr-thesis.py [csv] [outdir]` writes the ten individual figures (PDF and
300 dpi PNG). `plot-snr-sweep.py [csv] [outdir]` writes three overview PNGs.

To change the sweep (seeds, thresholds, node count, duration) edit the
`seeds`, `thresholds` and command string at the top of
`utils/snr-threshold-sweep.py`. If you change the number of seeds, regenerate
the figures.

### Figures produced by `plot-snr-thesis.py`

| File | Shows |
|---|---|
| `fig_pdr_vs_threshold` | Delivery ratio against threshold |
| `fig_packet_loss_vs_threshold` | Loss against threshold |
| `fig_delay_vs_threshold` | End-to-end delay against threshold |
| `fig_throughput_vs_threshold` | Throughput against threshold |
| `fig_reroutes_vs_threshold` | Reroutes per run against threshold |
| `fig_*_paired_difference` (4 files) | Adaptive minus conventional on the same seed, mean with 95% CI, individual seeds as dots |
| `fig_reroutes_vs_pdr` | Every run: reroutes against delivery ratio |

In the absolute plots the red dashed line and band are conventional AODV. In
the paired plots, a confidence interval that crosses zero means no clear
difference.

## 7. How the code works (short version)

1. **Scenario.** 802.11g ad hoc, 6 Mbps constant rate, 20 dBm transmit power,
   log-distance path loss plus a 300 m range cutoff, random waypoint mobility
   at 1-5 m/s in a 600 x 600 m area, AODV with hellos, up to 10 UDP CBR flows
   (16 kbps, 512 byte packets) from node `f` to node `f + N/2`. FlowMonitor
   collects the metrics.
2. **AODV hook.** `ForceLinkFailure(nextHop)` wraps AODV's existing private
   `SendRerrWhenBreaksLinkToNextHop`: it invalidates routes whose next hop is
   that neighbour and sends RERR to precursors. Other routes are untouched.
3. **SNR monitor (`SnrMonitorApp`, one per node, adaptive mode only).**
   - The PHY `MonitorSnifferRx` trace gives `signal - noise` for every received
     frame. The transmitter MAC address is read from the 802.11 header and
     mapped to a neighbour IP through a table built at setup.
   - Per-neighbour SNR is smoothed with an EWMA.
   - Every second, a neighbour whose smoothed SNR is below the threshold gets
     its counter incremented (reset to 0 otherwise; silent neighbours are
     skipped). At `lowWindows` consecutive low windows, `ForceLinkFailure` is
     called for that neighbour and the counter resets.

## 8. NetAnim animation (optional)

Generate an animation file:

```bash
./ns3 run "snr-adaptive-routing --nNodes=30 --simTime=40 --protocol=adaptive --snrThresholdDb=15 --RngRun=1 --animFile=snr-adaptive.xml"
```

Colours in the animation: blue = node, green = flow source, orange = flow
sink, red flash (0.5 s) = SNR-triggered reroute on that node. Packet tracing is
disabled to keep the file small (about 280 KB for 30 nodes, 40 s).

Viewing requires the NetAnim application, which is **not** part of this
repository or of a normal ns-3 build. It is distributed separately (Qt based)
from the ns-3 project. The XML file itself is a plain text file and has been
checked to contain the node positions and colour events for the full run, but
playback in the viewer has not been tested on the original machine.

## 9. Known issues (read before trusting results)

1. **Legacy default: all nodes start at (0,0).** The mobility setup gives
   `RandomWaypointMobilityModel` a `PositionAllocator`, but that attribute only
   chooses where nodes walk *to*; the starting position needs
   `mobility.SetPositionAllocator(...)`, which the original code lacked. Nodes
   therefore started in the corner and formed a dense cluster (no node more
   than about 106 m from the origin after 25 s). This is now selectable:
   `--spreadStart=1` starts nodes at random positions over the area. The
   default is still 0 (legacy) so that `sweep_results.csv` and
   `thesis_figures/` reproduce exactly. **Use `--spreadStart=1` for the
   intended scenario.**
2. **ns-3 AODV request-queue livelock (worked around).** With spread-out nodes
   and the ns-3 default `MaxQueueLen` of 64, the simulation stopped advancing
   at about 18-19 s of simulated time, in conventional mode too (seeds 1 to 6),
   so it is not caused by our code. Cause, found with a debugger stack of the
   stuck process and confirmed by experiment:
   - AODV parks packets without a route in `RequestQueue` (max 64).
   - When full, `Enqueue` drops the oldest entry; the drop callback calls
     `Ipv4L3Protocol::RouteInputError`, which now sends an ICMP net-unreachable
     to the packet's source (`SendIcmpNoRoute`).
   - For a locally originated packet the source is the node's own address, for
     which AODV has no valid route, so the ICMP packet is sent through the
     loopback device with a deferred-route tag.
   - On loopback receive it is deferred into the still-full queue, which drops
     the oldest entry again, generating another ICMP message, and so on, all at
     one simulation time.
   With `MaxQueueLen` 1000 or 100000 the run completes with identical results,
   so the queue never fills. The simulation therefore sets the queue length to
   1000 (`--aodvQueueLen`). This is a workaround: the ns-3 behaviour itself is
   not patched. A proper fix would avoid sending ICMP errors for locally
   originated packets (or to a local address) and/or erase the dropped entry
   before invoking the drop callback in `RequestQueue::Enqueue`.
3. **The spread layout is sparse.** With `--spreadStart=1` plain AODV delivers
   only about 13% of packets (per-seed 2% to 26%), so the network is mostly
   disconnected and delays are large and variable. Gains from adaptive routing
   are real in relative terms but small in absolute terms. A denser layout
   (smaller area or more nodes) would be a fairer test.
4. **Small sample.** 5 seeds and 40 s runs give wide error bars, and 11
   thresholds were examined, so treat isolated "significant" points with
   caution. In the spread sweep the effect is consistent across thresholds of
   13 dB and up, which is stronger evidence than any single point.
5. **Debug output.** The simulation prints one stderr line per reroute; at high
   thresholds that is thousands of lines per run. Redirect stderr if needed.
6. **Upstream default for scratch.** `scratch/` is git-ignored upstream, so new
   scratch files must be added with `git add -f` when working inside ns-3.

## 10. Troubleshooting

**macOS build error: "`<cstring>` tried including `<string.h>` but didn't find
libc++'s `<string.h>` header".** Stale CMake cache entries for libxml2/sqlite3
can point at an old Command Line Tools SDK that clashes with the current Xcode
SDK (seen after an Xcode update). On a fresh clone this should not happen. If
it does, check `cmake-cache/CMakeCache.txt` for `LIBXML2_INCLUDE_DIR` and
`SQLite3_INCLUDE_DIR` entries that contain `/Library/Developer/CommandLineTools`
and point them at the Xcode SDK (`xcrun --sdk macosx --show-sdk-path`), then
reconfigure and rebuild. Also make sure `CMAKE_OSX_SYSROOT` is set to that SDK
path.

**`timeout: command not found`.** macOS has no `timeout`. Just run the command
without it.

**Sweep script produces an empty or partial CSV.** It writes rows as runs
finish; if you run the plotting script while it is still going you will see
only some thresholds. Wait until the CSV has 61 lines (header plus 60 runs).

**Sweep fails immediately.** The script calls `./ns3 run --no-build`, so the
target must already be built (`./ns3 build snr-adaptive-routing`) and the
script must be run from the repository root.

**Missing Python module.** `pip install pandas numpy matplotlib`.

## 11. Suggested next steps

1. Try a denser scenario with `--spreadStart=1` (smaller area or more nodes) so
   the baseline delivery ratio is not so low, and rerun the sweep.
2. Increase seeds (20 or more) and simulation length; rerun the figures.
3. Try higher node speeds and different node counts.
4. Add routing-overhead measurements (RREQ and RERR counts).
5. Consider upstreaming a fix for the AODV/ICMP livelock (issue 2 above).
6. Test with a fading model in addition to log-distance loss.

## 12. Git notes

- This repository holds only project files; ns-3 upstream lives at
  https://gitlab.com/nsnam/ns-3-dev.git.
- `scratch/` is git-ignored in upstream ns-3, so when working inside an ns-3
  checkout add scratch files with `git add -f`.
- The full development history including the complete ns-3 tree existed on the
  branch `snr-adaptive-routing` of the original local checkout.
