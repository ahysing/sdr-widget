import usb.core
import struct

dev = usb.core.find(idVendor=0x1234,
                    idProduct=0x5678)

data = dev.read(
    0x83,
    64,
    timeout=2000
)


version, packets = struct.unpack(
    "<II",
    data[0:8]
)