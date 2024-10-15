import re
from collections import defaultdict
import matplotlib.pyplot as plt
import numpy as np
import matplotlib.ticker as ticker

log_pattern = re.compile(r"Host with source IP: (\d+) received sequence number: (\d+) at time: (\d+) ns\.")

def process_log_file(file_path):
    sequence_times = defaultdict(dict)
    with open(file_path, 'r') as file:
        for line in file:
            match = log_pattern.match(line)
            if match:
                source_ip, sequence_number, time_ns = match.groups()
                sequence_number = int(sequence_number)
                time_ns = int(time_ns)
                sequence_times[sequence_number][source_ip] = time_ns
    return sequence_times

def extract_last_arrival_times(sequence_times):
    tenth_times = []
    for sequence_number, ip_times in sequence_times.items():
        if len(ip_times) == 8:  
            sorted_times = sorted(ip_times.values())  
            tenth_times.append(sorted_times[7] / 1e9)  
    return tenth_times

def plot_cdf(data_list, file_paths, output_file):
    plt.figure(figsize=(10, 6))
    
    for data, file_path in zip(data_list, file_paths):
        sorted_data = np.sort(data)
        cdf = np.arange(1, len(sorted_data) + 1) / len(sorted_data)
        label = file_path.split('/')[-2]
        plt.plot(sorted_data, cdf, label=label)
    
    ax = plt.gca()
    ax.xaxis.set_major_formatter(ticker.ScalarFormatter())
    ax.ticklabel_format(style='plain', axis='x')

    plt.title('Distribution of last Arrival Times')
    plt.xlabel('Time (s)')
    plt.ylabel('CDF')
    plt.legend(loc='best')
    plt.grid(True)
    
    plt.savefig(output_file)
    plt.show()

file_paths = [
    'output/ecmp/config.log',
    'output/conweave/config.log',
    'output/halflife/config.log',
    'output/drill/config.log',
    'output/wzx/config.log'
]

all_tenth_times = []

for file_path in file_paths:
    sequence_times = process_log_file(file_path)
    tenth_times = extract_last_arrival_times(sequence_times)
    all_tenth_times.append(tenth_times)

plot_cdf(all_tenth_times, file_paths, 'output_cdf.png')
