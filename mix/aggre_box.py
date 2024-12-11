import re
import os
from collections import defaultdict
import numpy as np

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
    last_arrival_times = []
    for sequence_number, ip_times in sequence_times.items():
        if len(ip_times) == 16:  # Only consider sequences that have exactly 16 IPs
            sorted_times = sorted(ip_times.values())  # Sort the times to get the last arrival
            last_arrival_times.append(sorted_times[15] / 1e9)  # Convert to seconds
    return last_arrival_times

def calculate_statistics(last_arrival_times):
    if last_arrival_times:
        min_time = np.min(last_arrival_times)  # 最小值
        max_time = np.max(last_arrival_times)  # 最大值
        median_time = np.median(last_arrival_times)  # 中位数
        q1_time = np.percentile(last_arrival_times, 25)  # 下四分位数
        q3_time = np.percentile(last_arrival_times, 75)  # 上四分位数
    else:
        min_time = max_time = median_time = q1_time = q3_time = 0
    return min_time, max_time, median_time, q1_time, q3_time

def save_statistics_to_file(statistics, output_file='statistics_last_arrival_times.txt'):
    with open(output_file, 'w') as f:
        f.write("Folder Name\tMin Time (s)\tMax Time (s)\tMedian Time (s)\tQ1 Time (s)\tQ3 Time (s)\n")
        for folder, stats in statistics.items():
            f.write(f"{folder}\t{stats[0]:.6f}\t{stats[1]:.6f}\t{stats[2]:.6f}\t{stats[3]:.6f}\t{stats[4]:.6f}\n")

# Folders to process
folders = ['output/halflife','output/drill','output/wzx']

statistics = {}

# Process each folder
for folder in folders:
    all_last_arrival_times = []
    for root, dirs, files in os.walk(folder):
        for file_name in files:
            if file_name.endswith('.log'):  # Only process .log files
                file_path = os.path.join(root, file_name)
                sequence_times = process_log_file(file_path)
                last_arrival_times = extract_last_arrival_times(sequence_times)
                all_last_arrival_times.extend(last_arrival_times)
    
    # Calculate statistics for this folder
    min_time, max_time, median_time, q1_time, q3_time = calculate_statistics(all_last_arrival_times)
    statistics[folder.split('/')[-1]] = (min_time, max_time, median_time, q1_time, q3_time)

# Save the statistics to a file
save_statistics_to_file(statistics, output_file='statistics_last_arrival_times.txt')

print("Statistics for last arrival times saved to 'statistics_last_arrival_times.txt'.")
