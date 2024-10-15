import os 
import re
import numpy as np
from collections import defaultdict
import matplotlib.pyplot as plt

log_pattern = re.compile(r"Host with source IP: (\d+) received sequence number: (\d+) at time: (\d+) ns\.")

sequence_times = defaultdict(dict)

def process_log_file(file_path):
    with open(file_path, 'r') as file:
        for line in file:
            match = log_pattern.match(line)
            if match:
                source_ip, sequence_number, time_ns = match.groups()
                sequence_number = int(sequence_number)
                time_ns = int(time_ns)
                
                sequence_times[sequence_number][source_ip] = time_ns

def calculate_max_diff_average():
    max_diffs = []
    for sequence_number, ip_times in sequence_times.items():
        if len(ip_times) > 1: 
            times = list(ip_times.values())
            max_diff = max(times) - min(times)
            max_diffs.append(max_diff / 1e9)  
     
    if max_diffs:
        return sum(max_diffs) / len(max_diffs)
    else:
        return 0 

def process_folder(folder_path):
    global sequence_times
    sequence_times.clear()
    for root, dirs, files in os.walk(folder_path):
        for file_name in files:
            if file_name.endswith('.log'):
                file_path = os.path.join(root, file_name)
                process_log_file(file_path)
    
    avg_max_diff = calculate_max_diff_average()
    return avg_max_diff  

# Folders for each number of workers
folders_by_workers = {
    8: ['output/8worker/conweave', 'output/8worker/halflife', 'output/8worker/drill', 'output/8worker/wzx'],
    12: ['output/12worker/conweave', 'output/12worker/halflife', 'output/12worker/drill', 'output/12worker/wzx'],
    16: ['output/16worker/conweave', 'output/16worker/halflife', 'output/16worker/drill', 'output/16worker/wzx']
}

folder_names = ['conweave', 'halflife', 'drill', 'wzx']
avg_max_diffs_by_workers = {}

# Process each folder group for different worker numbers
for workers, folders in folders_by_workers.items():
    avg_max_diffs = []
    for folder in folders:
        avg_max_diff = process_folder(folder)
        avg_max_diffs.append(avg_max_diff)
    avg_max_diffs_by_workers[workers] = avg_max_diffs

# Plotting
bar_width = 0.2
fig, ax = plt.subplots(figsize=(10, 6))

# Number of workers and their labels
worker_labels = [8, 12, 16]

# Create an array to position the bars for different methods
index = np.arange(len(worker_labels))

# For each method, plot the bars side by side
for i, folder_name in enumerate(folder_names):
    avg_max_diffs = [avg_max_diffs_by_workers[workers][i] for workers in worker_labels]
    ax.bar(index + i * bar_width, avg_max_diffs, bar_width, label=folder_name)

ax.set_xlabel('No. of Workers')
ax.set_ylabel('Average Max Time Difference (s)')
ax.set_xticks(index + bar_width)
ax.set_xticklabels(worker_labels)
ax.legend()

plt.tight_layout()
plt.savefig('avg_max_diff_bar_chart.png')
plt.show()
