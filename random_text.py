import os
import random
import string

# Define parameters
folder_name = 'random_text_files'
num_files = 2000
num_lines = 2000
line_length = 80  # Average line length

# Create the folder if it doesn't exist
os.makedirs(folder_name, exist_ok=True)

def generate_random_line(length):
    """Generates a random line of the specified length."""
    return ''.join(random.choices(string.ascii_letters + string.digits + ' ', k=length))

# Generate the files with random text
for i in range(num_files):
    file_path = os.path.join(folder_name, f'file_{i+1}.txt')
    with open(file_path, 'w') as f:
        for _ in range(num_lines):
            f.write(generate_random_line(line_length) + '\n')

print(f"{num_files} files with {num_lines} lines each of random text generated in '{folder_name}'")
