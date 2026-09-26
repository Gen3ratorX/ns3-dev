import subprocess, csv, sys
from concurrent.futures import ThreadPoolExecutor
args = [a for a in sys.argv[1:] if not a.startswith('--out=')]
out = next((a[6:] for a in sys.argv[1:] if a.startswith('--out=')), 'sweep_results.csv')
extra = ' '.join(args)
seeds = [1,2,3,4,5]
thresholds = list(range(10,21))
def run(args):
    proto, thr, seed = args
    cmd = f'snr-adaptive-routing --nNodes=30 --simTime=40 --protocol={proto} --snrThresholdDb={thr} --RngRun={seed} {extra}'
    r = subprocess.run(['./ns3','run','--no-build',cmd],capture_output=True,text=True)
    line = r.stdout.strip().splitlines()[-1].split(',')
    return (proto, thr, seed, r.stderr.count('[reroute]'), *map(float,line[2:]))
jobs = [('conventional',0,s) for s in seeds] + [('adaptive',t,s) for t in thresholds for s in seeds]
with open(out,'w',newline='') as f, ThreadPoolExecutor(6) as ex:
    w = csv.writer(f); w.writerow(['protocol','threshold','seed','reroutes','throughput_mbps','pdr','delay_ms','loss'])
    for row in ex.map(run, jobs):
        w.writerow(row); f.flush()
