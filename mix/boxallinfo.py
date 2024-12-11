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
        if len(ip_times) == 16:  # Only consider sequences that have exactly 16 IPs
            sorted_times = sorted(ip_times.values())  # Sort the times to get the last arrival
            last_arrival_times.append(sorted_times[15] / 1e9 - 2)  # Convert to seconds
    return last_arrival_times

def save_last_arrival_times_to_file(last_arrival_times, output_file):
    with open(output_file, 'w') as f:
        for last_time in last_arrival_times:
            f.write(f"{last_time:.6f}\n")  # Write only the last arrival times in seconds

# Folders to process
folders = ['output/ecmp','output/conweave',#'output/letflow',
	   'output/halflife', 'output/drill', 'output/wzx']  # Example folders

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
    
    # Generate output file name based on the folder name
    output_file = f"{folder.split('/')[-1]}_last_arrival_times.txt"
    
    # Save the last arrival times to the specific file for the folder
    save_last_arrival_times_to_file(all_last_arrival_times, output_file)

    print(f"Last arrival times for {folder} saved to '{output_file}'.")
