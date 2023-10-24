import argparse
import os
import shutil
import random
import subprocess
import time
import uuid
from loguru import logger

def write_data(p, input_path, file):
    logger.debug(f"executing ./evenodd write {p} {input_path} {file}")
    start_time = time.time()
    p = subprocess.run(['./evenodd', 'write', str(p), input_path, file])
    assert p.returncode == 0
    end_time = time.time()
    return end_time - start_time

def read_data(p, file, output_path):
    logger.debug(f"executing ./evenodd read {p} {file} {output_path}")
    start_time = time.time()
    p = subprocess.run(['./evenodd', 'read', str(p), file, output_path])
    assert p.returncode == 0
    end_time = time.time()
    return end_time - start_time

def repair_data(p, idx0, idx1=None):
    args = ['./evenodd', 'repair', str(p), str(idx0)]
    if idx1 is not None:
        args.append(str(idx1))
    logger.debug(f"executing {' '.join(args)}")
    start_time = time.time()
    p = subprocess.run(args)
    assert p.returncode == 0
    end_time = time.time()
    return end_time - start_time

def generate_files(input_path, num_files, max_size=128*1024*1024*1024, readable: bool = False):  # max size: 128GiB
    files = []
    for _ in range(num_files):
        file_size = random.randint(1, max_size)
        file = f"file_{_}.dat"
        with open(os.path.join(input_path, file), 'wb') as f:
            if readable:
                lines = [f"{i}: evenodd is easy to use;" for i in range(file_size // 25 + 1)]
                content = '\n'.join(lines).encode()[:file_size]
            else:
                content = os.urandom(file_size)
            f.write(content)
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
        target = f"deleted_{folder}"
        if os.path.exists(target):
            shutil.move(target, f'trash/{folder}_{uuid.uuid4()}')
        shutil.move(folder, target)
    return folders_to_delete

def restore_disk_folders(delete_folders):
    for folder in delete_folders:
        if os.path.exists(folder):
            shutil.move(folder, f'trash/{folder}_{uuid.uuid4()}')
        shutil.move(f"deleted_{folder}", folder)

def compare_folders(folder1, folder2):
    res = os.system(f"diff -r {folder1} {folder2}")
    return res == 0

def compare_files(file1, file2):
    res = os.system(f"diff {file1} {file2}")
    return res == 0

@logger.catch
def test_evenodd(p, input_path, output_path):
    # Step 1: Generate files
    logger.info(f"{'='*28} Generating files {'='*28}")
    files = generate_files(input_path, 100, 102400, readable=False)  # assuming max number of files
    logger.info(f"Generated {len(files)} files")

    # Step 2: Write data
    logger.info(f"{'='*28} Writing data {'='*28}")
    tW_total = 0
    for file in files:
        ipt = os.path.join(input_path, file)
        logger.info(f"Write: {ipt} -> {file}")
        tW_total += write_data(p, ipt, file)
    logger.success(f"Total write time: {tW_total}")

    # Step 3: Backup disk_* folders
    # backup_disk_folders()

    # Step 4: Random read data
    logger.info(f"{'='*28} Reading data {'='*28}")
    tNR_total = 0
    for file in random.sample(files, k=len(files)//2):  # assuming we read half the files randomly
        out = os.path.join(output_path, file)
        logger.info(f"Read: {file} -> {out}")
        tNR_total += read_data(p, file, out)
        assert compare_files(os.path.join(input_path, file), out)
    logger.success(f"Total read time: {tNR_total}")

    # Step 5 and 6: Random delete and read
    logger.info(f"{'='*28} Random delete and read {'='*28}")
    tBR_total = 0
    for _ in range(7):
        num_to_delete = random.randint(1, 2)
        delete_folders = delete_random_disk_folders(num_to_delete)
        logger.info(f"Delete: {delete_folders}")
        for file in random.sample(files, k=len(files)//4):  # assuming we read a quarter of the files
            out = os.path.join(output_path, file)
            logger.info(f"Read: {file} -> {out}")
            tBR_total += read_data(p, file, out)
            assert compare_files(os.path.join(input_path, file), out)
        restore_disk_folders(delete_folders)
    logger.success(f"Total read time: {tBR_total}")

    logger.success('Read test passed!')
    return

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
    parser.add_argument('--p', type=int, help='EVENODD parameter p, a prime number.', default=5)
    parser.add_argument('--file', type=str, help='Identifier of the input file in the simulated filesystem.')
    parser.add_argument('--output_path', type=str, help='Path to store the output file.')
    
    args = parser.parse_args()

    os.system('./clean')
    test_evenodd(args.p, 'input_data', 'output_data')

if __name__ == '__main__':
    main()