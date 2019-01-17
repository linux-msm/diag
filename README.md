## DIAG

**DIAG** implements routing of diagnostics related messages between host and
various subsystems.

### USB Gadget

Ensure that your kernel is built with **CONFIG_CONFIGFS_FS** and **CONFIG_USB_CONFIGFS_F_FS** and that configfs is mounted in ```/sys/kernel/config```.

    G1="/sys/kernel/config/usb_gadget/g1"
    
    mkdir $G1
    mkdir $G1/strings/0x409
    mkdir $G1/functions/ffs.diag
    mkdir $G1/configs/c.1
    mkdir $G1/configs/c.1/strings/0x409
    
    echo 0xVID > $G1/idVendor
    echo 0xPID > $G1/idProduct
    echo SERIAL > $G1/strings/0x409/serialnumber
    echo MANUFACTURER > $G1/strings/0x409/manufacturer
    echo PRODUCT > $G1/strings/0x409/product
    echo "diag_dun" > $G1/configs/c.1/strings/0x409/configuration
    ln -s $G1/functions/ffs.diag $G1/configs/c.1
    
    mkdir /dev/ffs-diag
    mount -t functionfs diag /dev/ffs-diag
    
    diag-router &
    
    sleep 1
    
    echo 6a00000.dwc3 > $G1/UDC
