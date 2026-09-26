import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

src = sys.argv[1] if len(sys.argv) > 1 else "sweep_results.csv"
outdir = sys.argv[2] if len(sys.argv) > 2 else "."
df = pd.read_csv(src)
base = df[df.protocol == "conventional"].set_index("seed")
adap = df[df.protocol == "adaptive"]
thr = sorted(adap.threshold.unique())
nseeds = adap.seed.nunique()

metrics = [
    ("pdr", "Packet delivery ratio (%)"),
    ("loss", "Packet loss (%)"),
    ("delay_ms", "Average delay (ms)"),
    ("throughput_mbps", "Throughput (Mbps)"),
]

# 1. Absolute metrics vs threshold, with reroute count
fig, axes = plt.subplots(3, 2, figsize=(12, 11))
g = adap.groupby("threshold")
for ax, (col, title) in zip(axes.flatten(), metrics + [("reroutes", "Reroutes per run")]):
    m, s = g[col].mean(), g[col].std()
    ax.errorbar(m.index, m.values, yerr=s.values, marker="o", capsize=3,
                color="tab:blue", label="Adaptive (mean +/- std)")
    if col != "reroutes":
        bm, bs = base[col].mean(), base[col].std()
        ax.axhline(bm, color="tab:red", linestyle="--", label="Conventional AODV")
        ax.axhspan(bm - bs, bm + bs, color="tab:red", alpha=0.12)
    ax.set_title(title); ax.set_xlabel("SNR threshold (dB)"); ax.grid(alpha=0.3); ax.legend(fontsize=8)
# tradeoff: reroutes vs PDR
ax = axes[2, 1]
sc = ax.scatter(adap.reroutes, adap.pdr, c=adap.threshold, cmap="viridis", s=30)
ax.axhline(base.pdr.mean(), color="tab:red", linestyle="--", label="Conventional mean")
ax.set_xlabel("Reroutes per run"); ax.set_ylabel("PDR (%)"); ax.set_title("Reroute cost vs delivery (each dot = one run)")
ax.grid(alpha=0.3); ax.legend(fontsize=8); fig.colorbar(sc, ax=ax, label="threshold (dB)")
axes[2, 0].set_visible(True)
fig.suptitle(f"SNR-triggered adaptive routing vs conventional AODV ({nseeds} seeds per point)")
fig.tight_layout(); fig.savefig(f"{outdir}/snr_sweep_overview.png", dpi=150)

# 2. Paired difference vs conventional (same seed => same mobility/traffic)
fig, axes = plt.subplots(2, 2, figsize=(12, 8))
for ax, (col, title) in zip(axes.flatten(), metrics):
    means, cis = [], []
    for t in thr:
        a = adap[adap.threshold == t].set_index("seed")[col]
        d = (a - base[col]).dropna()
        means.append(d.mean()); cis.append(1.96 * d.std(ddof=1) / np.sqrt(len(d)))
        ax.scatter([t] * len(d), d.values, color="tab:blue", alpha=0.3, s=14)
    ax.errorbar(thr, means, yerr=cis, marker="o", capsize=3, color="tab:blue", label="Mean diff, 95% CI")
    ax.axhline(0, color="tab:red", linestyle="--", label="No difference")
    ax.set_title(f"{title}: adaptive minus conventional")
    ax.set_xlabel("SNR threshold (dB)"); ax.grid(alpha=0.3); ax.legend(fontsize=8)
fig.suptitle("Paired per-seed difference (dots = individual seeds); CI crossing 0 = no significant effect")
fig.tight_layout(); fig.savefig(f"{outdir}/snr_sweep_paired.png", dpi=150)

# 3. Distributions per threshold
fig, axes = plt.subplots(2, 2, figsize=(12, 8))
for ax, (col, title) in zip(axes.flatten(), metrics):
    data = [adap[adap.threshold == t][col].values for t in thr]
    ax.boxplot(data, tick_labels=[str(t) for t in thr], patch_artist=True,
               boxprops=dict(facecolor="tab:blue", alpha=0.35))
    ax.axhline(base[col].mean(), color="tab:red", linestyle="--", label="Conventional mean")
    ax.set_title(title); ax.set_xlabel("SNR threshold (dB)"); ax.grid(alpha=0.3); ax.legend(fontsize=8)
fig.suptitle("Distribution across seeds")
fig.tight_layout(); fig.savefig(f"{outdir}/snr_sweep_boxplots.png", dpi=150)
print("saved 3 figures to", outdir)
