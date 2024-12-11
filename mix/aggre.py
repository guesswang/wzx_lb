import re
import os
from collections import defaultdict

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
        if len(ip_times) == 8:  # Only consider sequences that have exactly 32 IPs
            sorted_times = sorted(ip_times.values())  # Sort the times to get the last arrival
            last_arrival_times.append(sorted_times[7] / 1e9)  # Convert to seconds
    return last_arrival_times

def calculate_avg_last_arrival_time(last_arrival_times):
    if last_arrival_times:
        avg_time = sum(last_arrival_times) / len(last_arrival_times)  # Calculate the average
    else:
        avg_time = 0
    return avg_time

def save_avg_last_arrival_times_to_file(avg_last_arrival_times, output_file='avg_last_arrival_times.txt'):
    with open(output_file, 'w') as f:
        f.write("Folder Name\tAverage Last Arrival Time (s)\n")  # File header
        for folder, avg_time in avg_last_arrival_times.items():
            f.write(f"{folder}\t{avg_time:.6f}\n")  # Write folder name and average time

# Folders to process
folders = [ 'output/halflife', 'output/conweave']

avg_last_arrival_times = {}

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
    
    avg_last_arrival_time = calculate_avg_last_arrival_time(all_last_arrival_times)
    avg_last_arrival_times[folder.split('/')[-1]] = avg_last_arrival_time  # Store the average for the folder

# Save the averages to a file
save_avg_last_arrival_times_to_file(avg_last_arrival_times, output_file='avg_last_arrival_times.txt')

print("Average last arrival times saved to 'avg_last_arrival_times.txt'.")
