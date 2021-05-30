import pandas as pd
from scipy import stats
import sys

def filter(s):
    input_size = len(s)
    t = stats.t(df=(input_size-1)).ppf((0.025, 0.975))
    upper_bound = s.mean() + t[1]*s.std()/(input_size**0.5)
    lower_bound = s.mean() + t[0]*s.std()/(input_size**0.5)
    ret = s[s <= upper_bound]
    ret = ret[ret >= lower_bound]
    if len(ret) > 0:
        return ret.mean()
    return s.mean()

data = []
for line in sys.stdin:
    line = line.replace('\n', '')
    if len(line) != 0:
        data.append(int(line))

result = filter(pd.Series(data))
sys.stdout.write("{:.2f}".format(result))