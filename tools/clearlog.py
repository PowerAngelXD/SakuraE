import os
import re


def clean_build_logs(build_dir="build"):
    log_pattern = re.compile(r"^log-\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+\.txt$")

    if not os.path.exists(build_dir):
        print(f"Error: Folder: '{build_dir}' isn't existed")
        return

    count = 0
    print(f"Removing {build_dir} log files ...")

    for filename in os.listdir(build_dir):
        if log_pattern.match(filename):
            file_path = os.path.join(build_dir, filename)
            try:
                os.remove(file_path)
                print(f"Removed: {filename}")
                count += 1
            except Exception as e:
                print(f"Remove: {filename} failed: {e}")

    print("---")
    print(f"Clear complete! Total: {count}")


if __name__ == "__main__":
    clean_build_logs("build")
