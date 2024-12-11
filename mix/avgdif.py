import os 
import re
import csv
from collections import defaultdict

log_pattern = re.compile(r"Host with source IP: (\d+) received sequence number: (\d+) at time: (\d+) ns\.")

# 记录每个sequence_number及其对应的IP地址和时间戳
sequence_times = defaultdict(lambda: defaultdict(float))

def process_log_file(file_path):
    with open(file_path, 'r') as file:
        for line in file:
            match = log_pattern.match(line)
            if match:
                source_ip, sequence_number, time_ns = match.groups()
                sequence_number = int(sequence_number) / 1000  # 将序列号转换为毫秒
                time_ns = int(time_ns) / 1e6  # 将时间戳转换为秒

                # 检查该sequence_number是否已经存储过当前的source_ip
                if source_ip in sequence_times[sequence_number]:
                    continue  # 如果已经存在，则跳过此条日志

                # 存储时间戳
                sequence_times[sequence_number][source_ip] = time_ns

def calculate_max_diff_for_full_sequences(required_count):
    max_diffs = []
    for sequence_number, ip_times in sequence_times.items():
        if len(ip_times) == required_count:  # 只有当这个序列号有足够多的IP地址时才计算
            times = list(ip_times.values())
            max_diff = max(times) - min(times)  # 计算最大时间差
            max_diffs.append(max_diff) 
    return max_diffs

def process_folder(folder_path, required_count):
    global sequence_times
    sequence_times.clear()
    for root, dirs, files in os.walk(folder_path):
        for file_name in files:
            if file_name.endswith('.log'):
                file_path = os.path.join(root, file_name)
                process_log_file(file_path)
    
    max_diffs = calculate_max_diff_for_full_sequences(required_count)
    total_sum = sum(max_diffs)
    count = len(max_diffs)
    avg_max_diff = total_sum / count if count > 0 else 0
    return total_sum, count, avg_max_diff

folders = [
    'output/halflife', 'output/conweave'
]

required_count = 8

results = []

for folder in folders:
    total_sum, count, avg_max_diff = process_folder(folder, required_count)
    results.append({
        'Folder': folder,
        'Sum(Max_Diffs)': total_sum,
        'Count(Max_Diffs)': count,
        'Avg_Max_Diff': avg_max_diff
    })

# Save results to a CSV file
output_file = 'folder_max_diffs_summary.csv'
with open(output_file, 'w', newline='') as csvfile:
    csvwriter = csv.DictWriter(csvfile, fieldnames=['Folder', 'Sum(Max_Diffs)', 'Count(Max_Diffs)', 'Avg_Max_Diff'])
    csvwriter.writeheader()
    csvwriter.writerows(results)

print(f"Results saved to {output_file}")
