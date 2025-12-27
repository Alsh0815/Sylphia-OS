VHD_PATH="../usb.vhd"
SOURCE_DIR="../build/output"
MOUNT_POINT="../mnt/vhd_temp"

sudo modprobe nbd
sudo mkdir -p $MOUNT_POINT

sudo qemu-nbd -c /dev/nbd0 "$VHD_PATH"
sleep 1
sudo mount /dev/nbd0p1 $MOUNT_POINT
sudo cp -r "$SOURCE_DIR"/* $MOUNT_POINT/
sudo umount $MOUNT_POINT
sudo qemu-nbd -d /dev/nbd0
sudo rmdir $MOUNT_POINT