import pandas as pd
import matplotlib.pyplot as plt

def process_files(file_paths):
    avg_values = []
    max_values = []
    labels = [f.split('/')[-2] for f in file_paths]
    
    for file in file_paths:
        data = pd.read_csv(file, delim_whitespace=True, header=None)
        filtered_data = data[data[1] == 127]
        values = filtered_data.iloc[:, 6] / 1e9
        if not values.empty:
            avg_value = values.mean()
            max_value = values.max()
        else:
            avg_value = 0
            max_value = 0
        avg_values.append(float(avg_value))
        max_values.append(float(max_value))

    bar_width = 0.35
    index = range(len(avg_values))

    plt.figure(figsize=(10, 6))
    plt.bar(index, avg_values, bar_width, label='avg fct')
    plt.bar([i + bar_width for i in index], max_values, bar_width, label='99th fct')
    plt.ylabel('Time (s)')
    plt.xticks([i + bar_width / 2 for i in index], labels)
    plt.gca().set_xlabel('')
    plt.legend()
    plt.tight_layout()
    plt.savefig('average_and_max_values_bar_chart.png')
    plt.show()

file_paths = [
    'output/ecmp/ecmp_out_fct.txt',
    'output/conweave/conweave_out_fct.txt',
    'output/letflow/letflow_out_fct.txt',
    'output/halflife/halflife_out_fct.txt',
    'output/drill/drill_out_fct.txt',
    'output/wzx/wzx_out_fct.txt'
]

process_files(file_paths)
