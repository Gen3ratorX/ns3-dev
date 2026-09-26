# SNR-Triggered Adaptive Routing for Ad Hoc Networks (ns-3)

## 1. What this project is

An ns-3 experiment that asks: **if a mobile ad hoc node reroutes as soon as the
signal to its next hop degrades, does the network perform better than plain
AODV, which only reroutes after a link has actually broken?**

- **Conventional:** stock AODV. A link is declared broken only when the MAC
  layer fails to deliver frames (ACK failures) or hello messages stop arriving.
- **Adaptive:** AODV plus an SNR monitor on every node. When the smoothed SNR
  towards a specific neighbour stays below a threshold, the routes that use
  that neighbour as next hop are invalidated early, so AODV finds another path
  before the link dies.

Nodes move for real (random waypoint mobility), so links genuinely degrade and
break during a run.

## 2. Files

This repository contains only project files, to be overlaid on an upstream
ns-3-dev checkout (see `HANDOVER_GUIDE.md` for setup).

| File | Purpose |
|---|---|
| `patches/aodv-force-link-failure.patch` | Adds public `ForceLinkFailure(nextHop)` to `aodv::RoutingProtocol` |
| `scratch/snr-adaptive-routing.cc` | The simulation (both modes) |
| `utils/snr-threshold-sweep.py` | Runs a threshold x seed sweep and writes `sweep_results.csv` |
| `utils/plot-snr-sweep.py`, `utils/plot-snr-thesis.py` | Overview and individual thesis figures |

## 3. How it works

### 3.1 Scenario (`scratch/snr-adaptive-routing.cc`)

- 802.11g ad hoc, 6 Mbps constant rate, 20 dBm transmit power.
- `RangePropagationLossModel` with a 300 m maximum range.
- Random waypoint mobility in a 600 x 600 m area, speed 1-5 m/s, 1 s pause.
- AODV routing with hello messages enabled.
- Up to 10 UDP CBR flows (16 kbps, 512 byte packets) between node `f` and node
  `f + N/2`.
- FlowMonitor collects throughput, packet delivery ratio (PDR), delay and loss.
- One CSV line is printed at the end:
  `protocol,nodes,throughput_Mbps,PDR%,delay_ms,loss%`.

### 3.2 The AODV hook (`ForceLinkFailure`)

AODV already has a private function, `SendRerrWhenBreaksLinkToNextHop`, that it
calls when the MAC layer reports a failed link. It looks up every destination
whose next hop is the failed neighbour, marks those routes invalid, and sends
route errors (RERR) to the precursors.

`ForceLinkFailure(nextHop)` is a thin public wrapper around it. This lets the
SNR monitor trigger exactly the same behaviour, but only for the one degrading
neighbour. Routes through other next hops are left alone.

### 3.3 The SNR monitor (`SnrMonitorApp`)

One instance per node, created only in `adaptive` mode.

1. **Measure.** The PHY `MonitorSnifferRx` trace fires for every received
   frame. The monitor peeks the 802.11 MAC header, reads the transmitter
   address (Addr2), maps it to the neighbour's IP with a MAC-to-IP table built
   at setup, and records `signal - noise` in dB.
2. **Smooth.** Each neighbour keeps an exponentially weighted moving average
   (EWMA) of SNR: `ewma = alpha * sample + (1 - alpha) * ewma`.
3. **Check once per second.** For each neighbour heard in that window, count
   consecutive windows where the smoothed SNR is below the threshold.
4. **Trigger.** After `lowWindows` consecutive low windows, call
   `ForceLinkFailure(neighbour)` and reset that neighbour's counter.
   Neighbours not heard in a window are skipped, not counted as low.

Each trigger prints a line such as:

```
[reroute] t=2.5s next-hop 10.0.0.9 smoothed SNR 4.9dB below threshold for 3 windows, invalidating its routes
```

### 3.4 Design history (why it looks like this)

1. **Original version:** on any low SNR the whole interface was taken down for
   50 ms. That invalidated every route through the node, not just the bad one.
2. **Bug found:** SNR was stored under `Ipv4Address(nodeId + 1)`, which is the
   receiving node's own id used as a raw 32-bit address, not the sender's
   address. Per-neighbour tracking never actually worked. Fixed by reading the
   real transmitter MAC from each frame.
3. **Per-next-hop invalidation:** replaced the interface bounce with
   `ForceLinkFailure` on the specific neighbour.
4. **Flapping:** using a single latest sample against an 8 dB threshold gave
   2,125 reroutes in a 30-node, 40 s run. Added EWMA smoothing and the
   consecutive-window requirement, which cut that to 14 at the defaults.

### 3.5 Tunable options

| Option | Default | Meaning |
|---|---|---|
| `--protocol` | `adaptive` | `conventional` or `adaptive` |
| `--nNodes` | 40 | Number of nodes |
| `--simTime` | 100 | Simulation length in seconds |
| `--snrThresholdDb` | 8 | SNR below which a link counts as degraded |
| `--snrAlpha` | 0.3 | EWMA weight of the newest sample |
| `--lowWindows` | 3 | Consecutive low 1 s windows before rerouting |
| `--RngRun` | 1 | Random seed (built-in ns-3 option) |

## 4. How to run it

Build (from the repo root):

```bash
./ns3 configure --enable-examples --enable-tests
./ns3 build snr-adaptive-routing
```

Single runs on the same seed:

```bash
./ns3 run "snr-adaptive-routing --nNodes=30 --simTime=40 --protocol=conventional --RngRun=1"
./ns3 run "snr-adaptive-routing --nNodes=30 --simTime=40 --protocol=adaptive --snrThresholdDb=15 --RngRun=1" 2>&1 | grep -E "reroute|adaptive,"
```

Full sweep (thresholds 10-20 dB, seeds 1-5, plus a conventional baseline per
seed; about 1-2 minutes) and plots:

```bash
python3 utils/snr-threshold-sweep.py
python3 utils/plot-snr-sweep.py sweep_results.csv .
```

Outputs: `sweep_results.csv`, `snr_sweep_overview.png`, `snr_sweep_paired.png`,
`snr_sweep_boxplots.png`.

Note for macOS: the build initially failed because stale CMake cache entries
pinned libxml2 and sqlite3 to an old CommandLineTools SDK, which clashed with
the newer Xcode SDK. Re-running configure against the current SDK fixed it. A
fresh checkout should not hit this.

## 5. Results

Setup: 30 nodes, 40 s per run, 5 seeds per point. The same seed gives the same
mobility and traffic in both modes, so runs can be compared in pairs.

| Config | Reroutes per run | PDR % | Delay ms | Throughput Mbps |
|---|---|---|---|---|
| Conventional AODV | 0 | 83.9 +/- 4.8 | 115.1 +/- 13.5 | 0.156 |
| Adaptive, 10 dB | 54 | 83.8 +/- 4.7 | 115.6 +/- 12.1 | 0.154 |
| Adaptive, 15 dB | 1241 | 84.6 +/- 4.7 | 101.2 +/- 12.7 | 0.163 |
| Adaptive, 20 dB | 2896 | 84.0 +/- 5.3 | 106.1 +/- 21.3 | 0.160 |

(mean +/- standard deviation across seeds; other thresholds behave similarly,
see `sweep_results.csv`.)

What the figures show:

- **Reroute count rises steadily with threshold**, from about 50 at 10 dB to
  about 2,900 at 20 dB. The trigger works.
- **Delivery, loss, delay and throughput are mostly indistinguishable from
  conventional AODV.** Adaptive means sit inside the baseline's spread, and the
  paired per-seed differences mostly have confidence intervals that include
  zero. Loss is simply the mirror of PDR.
- **Reroute cost vs delivery:** runs with about 3,000 reroutes deliver about as
  well as runs with about 50.
- **One possible exception:** at 15 dB, delay is about 14 ms lower and
  throughput about 0.007 Mbps higher, with intervals that stay clear of zero.
  This is weak evidence: 11 thresholds and 4 metrics were examined with only 5
  seeds, so a couple of "significant" points can appear by chance, and
  neighbouring thresholds do not confirm it.
- **Defaults do nothing here.** With the default 8 dB threshold, smoothing and
  3-window rule, the adaptive results were identical to conventional to every
  digit in an early test. The few reroutes that fired evidently hit neighbours
  that were not next hops on any active route.

## 6. Interpretation and limitations

- **Propagation model.** The channel uses log-distance path loss (from
  `YansWifiChannelHelper::Default()`) plus a hard cut at 300 m from
  `RangePropagationLossModel`, so SNR falls gradually with distance but there
  is no fading. Whether adding fading (for example Nakagami) would give the
  SNR trigger more useful lead time is untested.
- **Starting positions (bug).** All nodes start at (0,0) because no starting
  position allocator is set, so the results below describe a dense cluster,
  not a 600 x 600 m spread. Fixing it exposes a separate stall in stock
  AODV; see `HANDOVER_GUIDE.md`, section 9.
- **Mobility.** Speeds of 1-5 m/s are slow. Faster nodes make links degrade
  and break more abruptly, which is where early rerouting should matter more.
- **AODV is already reactive** through MAC-layer failure detection, so it may
  catch most breaks about as fast as the SNR trigger does.
- **Small sample.** 5 seeds and 40 s runs give wide error bars. Conclusions
  about small effects need more seeds.
- **SNR source.** The monitor uses frames it happens to overhear, so a
  neighbour that sends little is measured rarely; it is skipped when unheard.
- **No RERR damping.** Early invalidation can itself cause extra route
  discoveries, which costs overhead that is not measured separately here.

## 7. Suggested next steps

1. Switch to a log-distance plus fading propagation model and rerun the sweep.
2. Raise node speed (for example 10-20 m/s) and vary node count.
3. Run 20 or more seeds, at least at 15 dB versus the baseline, to test the
   possible delay and throughput gain.
4. Measure routing overhead (RREQ/RERR counts) alongside PDR and delay.
5. Add an animation (NetAnim is already built in this tree) to show nodes
   moving and routes switching.
