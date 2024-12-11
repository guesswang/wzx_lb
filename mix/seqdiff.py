import os
import re
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

folders = ['output/ecmp', 'output/letflow', 'output/drill', 'output/wzx']

folder_names = []
avg_max_diffs = []

for folder in folders:
    avg_max_diff = process_folder(folder)
    folder_names.append(folder.split('/')[-1])
    avg_max_diffs.append(avg_max_diff)

plt.figure(figsize=(10, 6))
plt.bar(folder_names, avg_max_diffs) 
plt.ylabel('Average Max Time Difference (s)')
plt.tight_layout()

plt.savefig('avg_max_diff_bar_chart.png')
plt.show()
