import pandas as pd

def process_files(file_paths, output_txt_file='output.txt'):
    avg_values = []
    max_values = []
    labels = [f.split('/')[-2] for f in file_paths]
    
    for file in file_paths:
        data = pd.read_csv(file, delim_whitespace=True, header=None)
        filtered_data = data[data[1] == 127]
        values = filtered_data.iloc[:, 6] / 1e9 - 2
        if not values.empty:
            avg_value = values.mean()
            max_value = values.max()
        else:
            avg_value = 0
            max_value = 0
        avg_values.append(float(avg_value))
        max_values.append(float(max_value))
    

    with open(output_txt_file, 'w') as f:
        f.write("Label\tAverage (s)\tMax (s)\n")  
        for label, avg, max_val in zip(labels, avg_values, max_values):
            f.write(f"{label}\t{avg:.6f}\t{max_val:.6f}\n") 

    print(f"Results saved to {output_txt_file}")

file_paths = [
    'output/halflife/halflife_out_fct.txt',
    'output/conweave/conweave_out_fct.txt'
]

process_files(file_paths, output_txt_file='output/average_and_max_values.txt')
