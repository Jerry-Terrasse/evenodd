import argparse
import os
import shutil
import random
import subprocess
import time

def write_data(p, input_path, file):
    start_time = time.time()
    subprocess.run(['./evenodd', 'write', str(p), input_path, file])
    end_time = time.time()
    return end_time - start_time

def read_data(p, file, output_path):
    start_time = time.time()
    subprocess.run(['./evenodd', 'read', str(p), file, output_path])
    end_time = time.time()
    return end_time - start_time

def repair_data(p, idx0, idx1=None):
    args = ['./evenodd', 'repair', str(p), str(idx0)]
    if idx1 is not None:
        args.append(str(idx1))
    start_time = time.time()
    subprocess.run(args)
    end_time = time.time()
    return end_time - start_time

def generate_files(num_files, max_size=128*1024*1024*1024):  # max size: 128GiB
    files = []
    for _ in range(num_files):
        file_size = random.randint(1, max_size)
        file = f"file_{_}.dat"
        with open(file, 'wb') as f:
            f.write(os.urandom(file_size))
        files.append(file)
    return files

def backup_disk_folders():
    backup_folder = 'backup_disk'
    if os.path.exists(backup_folder):
        raise Exception(f"{backup_folder} already exists.")
    os.makedirs(backup_folder)
    
    for folder in os.listdir():
        if folder.startswith('disk_'):
            shutil.copytree(folder, os.path.join(backup_folder, folder))

def delete_random_disk_folders(num_folders):
    disk_folders = [folder for folder in os.listdir() if folder.startswith('disk_')]
    folders_to_delete = random.sample(disk_folders, k=num_folders)
    for folder in folders_to_delete:
        shutil.move(folder, f"deleted_{folder}")
    return folders_to_delete

def restore_disk_folders(delete_folders):
    for folder in delete_folders:
        shutil.move(f"deleted_{folder}", folder)

def compare_folders(folder1, folder2):
    res = os.system(f"diff -r {folder1} {folder2}")
    return res == 0

def compare_files(file1, file2):
    res = os.system(f"diff {file1} {file2}")
    return res == 0

def test_evenodd(p, input_path, output_path):
    # Step 1: Generate files
    files = generate_files(1, 1024)  # assuming max number of files

    # Step 2: Write data
    tW_total = 0
    for file in files:
        tW_total += write_data(p, os.path.join(input_path, file), file)

    # Step 3: Backup disk_* folders
    # backup_disk_folders()

    # Step 4: Random read data
    tNR_total = 0
    for file in random.sample(files, k=len(files)//2):  # assuming we read half the files randomly
        out = os.path.join(output_path, file)
        tNR_total += read_data(p, file, out)
        assert compare_files(os.path.join(input_path, file), out)

    # Step 5 and 6: Random delete and read
    tBR_total = 0
    for _ in range(7):
        num_to_delete = random.randint(1, 2)
        delete_folders = delete_random_disk_folders(num_to_delete)
        for file in random.sample(files, k=len(files)//4):  # assuming we read a quarter of the files
            out = os.path.join(output_path, file)
            tBR_total += read_data(p, file, out)
            assert compare_files(os.path.join(input_path, file), out)
        restore_disk_folders(delete_folders)

    # Step 7: Random delete and repair
    tF_total = 0
    for _ in range(7):
        num_to_delete = random.randint(1, 2)
        delete_folders = delete_random_disk_folders(num_to_delete)
        if len(delete_folders) == 1:
            tF_total += repair_data(p, delete_folders[0])
            assert compare_folders(delete_folders[0], f"deleted_{delete_folders[0]}")
        else:
            tF_total += repair_data(p, delete_folders[0], delete_folders[1])
            assert compare_folders(delete_folders[0], f"deleted_{delete_folders[0]}")
            assert compare_folders(delete_folders[1], f"deleted_{delete_folders[1]}")
        # restore_disk_folders(delete_folders)

    # Final verdict
    backup_folder = 'backup_disk'
    for folder in os.listdir(backup_folder):
        if not compare_folders(folder, os.path.join(backup_folder, folder)):
            print(f"Test failed. {folder} is not consistent.")
            return

    print("Test passed!")

def main():
    parser = argparse.ArgumentParser(description='Evaluate evenodd storage tool.')
    parser.add_argument('--p', type=int, help='EVENODD parameter p, a prime number.')
    parser.add_argument('--file', type=str, help='Identifier of the input file in the simulated filesystem.')
    parser.add_argument('--output_path', type=str, help='Path to store the output file.')
    
    args = parser.parse_args()

    test_evenodd(args.p, 'input_data', 'output_data')

if __name__ == '__main__':
    main()