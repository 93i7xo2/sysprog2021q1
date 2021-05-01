import subprocess
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
import os
from tqdm import tqdm

def filter(arr, runs):
    t = stats.t(df=(runs-1)).ppf((0.025, 0.975))
    upper_bound = arr.mean() + t[1]*arr.std(0)/(runs**0.5)
    lower_bound = arr.mean() + t[0]*arr.std(0)/(runs**0.5)
    output = []
    for ele in arr:
        if ele <= upper_bound and ele >= lower_bound:
            output.append(ele)
    return np.array(output).mean()


if __name__ == "__main__":
    target_cpu = os.cpu_count()-1
    runs = 100

    xs=[]
    string=[]

    for i in tqdm(range(runs)):
        ret = subprocess.run(
            f'sudo taskset -c {target_cpu} ./xs_benchmark>>./xs.txt', shell=True)
        ret = subprocess.run(
            f'sudo taskset -c {target_cpu} ./string_benchmark>>./string.txt', shell=True)

    df = pd.read_csv('xs.txt', delimiter=' ', header=None)
    df.columns = ['time']
    xs = df.time[:runs]
    df = pd.read_csv('string.txt', delimiter=' ', header=None)
    df.columns = ['time']
    string = df.time[:runs]

    with open("data.txt", "w") as f:
        f.write(f"0 xs {filter(xs, runs)}\n1 string {filter(string, runs)}")