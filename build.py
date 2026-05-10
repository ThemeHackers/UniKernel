import subprocess
import shutil
import os
import sys

def build():
 
    spec_file = "UniAccelHost.spec"
    dist_folder = "dist"
    exe_name = "UniAccelHost.exe"
    source_exe = os.path.join(dist_folder, exe_name)
    destination_exe = exe_name

    print(f">>> Starting build process for {exe_name}...")

  
    try:
        subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", spec_file], check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error: PyInstaller failed with exit code {e.returncode}")
        return
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        return

   
    if os.path.exists(source_exe):
        print(f">>> Moving {source_exe} to current directory...")
        try:
           
            if os.path.exists(destination_exe):
                os.remove(destination_exe)
            
            shutil.move(source_exe, destination_exe)
            print(f"SUCCESS: {destination_exe} is now in the root folder.")
        except Exception as e:
            print(f"Error moving file: {e}")
    else:
        print(f"Error: Could not find built executable at {source_exe}")

if __name__ == "__main__":
    build()
