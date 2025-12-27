import platform
import subprocess


def copy_items_to_vhd():
    os_type = platform.system()
    try:
        if os_type == "Windows":
            subprocess.run([
                "powershell", "-ExecutionPolicy", "Bypass", "-File", "./build_scripts/sync_usb_vhd.ps1"
            ], check=True)
            
        elif os_type == "Linux":
            subprocess.run([
                "sudo", "bash", "./build_scripts/sync_usb_vhd.sh"
            ], check=True)
            
        print("Success: All items copied to VHD.")
    except subprocess.CalledProcessError as e:
        print(f"Error occurred: {e}")