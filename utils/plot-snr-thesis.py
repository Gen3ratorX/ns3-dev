import os
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

src = sys.argv[1] if len(sys.argv) > 1 else "sweep_results.csv"
outdir = sys.argv[2] if len(sys.argv) > 2 else "thesis_figures"
os.makedirs(outdir, exist_ok=True)

plt.rcParams.update({
    "font.family": "serif",
    "font.size": 12,
    "axes.labelsize": 13,
    "legend.fontsize": 11,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "savefig.bbox": "tight",
})
BLUE, RED = "#1f4e9c", "#b22222"

df = pd.read_csv(src)
base = df[df.protocol == "conventional"].set_index("seed")
adap = df[df.protocol == "adaptive"]
thr = sorted(adap.threshold.unique())
g = adap.groupby("threshold")

metrics = {
    "pdr": "Packet delivery ratio (%)",
    "loss": "Packet loss ratio (%)",
    "delay_ms": "Average end-to-end delay (ms)",
    "throughput_mbps": "Aggregate throughput (Mbps)",
}
fname = {"pdr": "pdr", "loss": "packet_loss", "delay_ms": "delay", "throughput_mbps": "throughput"}


def save(fig, name):
    fig.savefig(f"{outdir}/{name}.pdf")
    fig.savefig(f"{outdir}/{name}.png", dpi=300)
    plt.close(fig)


for col, label in metrics.items():
    fig, ax = plt.subplots(figsize=(6, 4))
    m, s = g[col].mean(), g[col].std()
    bm, bs = base[col].mean(), base[col].std()
    ax.axhspan(bm - bs, bm + bs, color=RED, alpha=0.12, label="Conventional AODV (+/- 1 SD)")
    ax.axhline(bm, color=RED, linestyle="--", linewidth=1.5, label="Conventional AODV (mean)")
    ax.errorbar(m.index, m.values, yerr=s.values, marker="o", capsize=3, color=BLUE,
                linewidth=1.6, label="SNR-adaptive (mean +/- 1 SD)")
    ax.set_xlabel("SNR threshold (dB)")
    ax.set_ylabel(label)
    ax.set_xticks(thr)
    ax.legend(frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.18))
    save(fig, f"fig_{fname[col]}_vs_threshold")

fig, ax = plt.subplots(figsize=(6, 4))
m, s = g["reroutes"].mean(), g["reroutes"].std()
ax.errorbar(m.index, m.values, yerr=s.values, marker="o", capsize=3, color=BLUE, linewidth=1.6)
ax.set_xlabel("SNR threshold (dB)")
ax.set_ylabel("SNR-triggered reroutes per run")
ax.set_xticks(thr)
save(fig, "fig_reroutes_vs_threshold")

for col, label in metrics.items():
    fig, ax = plt.subplots(figsize=(6, 4))
    means, cis = [], []
    for t in thr:
        d = (adap[adap.threshold == t].set_index("seed")[col] - base[col]).dropna()
        means.append(d.mean())
        cis.append(1.96 * d.std(ddof=1) / np.sqrt(len(d)))
        ax.scatter([t] * len(d), d.values, color=BLUE, alpha=0.3, s=16, zorder=2)
    ax.axhline(0, color=RED, linestyle="--", linewidth=1.5, label="No difference")
    ax.errorbar(thr, means, yerr=cis, marker="o", capsize=3, color=BLUE, linewidth=1.6,
                zorder=3, label="Mean paired difference (95% CI)")
    ax.set_xlabel("SNR threshold (dB)")
    ax.set_ylabel(f"Adaptive minus conventional:\n{label}")
    ax.set_xticks(thr)
    ax.legend(frameon=False)
    save(fig, f"fig_{fname[col]}_paired_difference")

fig, ax = plt.subplots(figsize=(6.4, 4))
sc = ax.scatter(adap.reroutes, adap.pdr, c=adap.threshold, cmap="viridis", s=36)
ax.axhline(base.pdr.mean(), color=RED, linestyle="--", linewidth=1.5, label="Conventional AODV (mean)")
ax.set_xlabel("SNR-triggered reroutes per run")
ax.set_ylabel(metrics["pdr"])
ax.legend(frameon=False)
fig.colorbar(sc, ax=ax, label="SNR threshold (dB)")
save(fig, "fig_reroutes_vs_pdr")

print("wrote", len(os.listdir(outdir)), "files to", outdir)
