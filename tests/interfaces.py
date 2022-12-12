import fcntl
import socket
import struct

from ctypes import *

# Extracted from linux-source-4.15.0/include/uapi/linux/sockios.h and
#                linux-source-4.15.0/include/uapi/linux/if.h
# The values of these flags in the kernel are extremely unlikely to change, so they're copied here.
# There are more than 100 relevant constants.  If support for a large number of them is desirable,
# run h2py.py on sockios.h and if.h and import * from the resulting SOCKIOS.py and IF.py.
# https://github.com/PexMor/unshare/blob/master/h2py.py
IFF_UP       = 0x1
IFF_LOOPBACK = 0x8

IFNAMSIZ = 16

SIOCGIFFLAGS = 0x00008913
SIOCSIFFLAGS = 0x00008914

class SockAddr_Gen(Structure):
    _fields_ = [
        ("sa_family", c_uint16),
        ("sa_data", (c_uint8 * 22))
    ]

# AF_INET / IPv4
class IPv4(Structure):
    _pack_ = 1
    _fields_ = [
        ("s_addr", c_uint32),
    ]

class SockAddr_IPv4(Structure):
    _pack_ = 1
    _fields_ = [
        ("sin_family", c_ushort),
        ("sin_port", c_ushort),
        ("sin_addr", IPv4),
        ("sin_zero", (c_uint8 * 16)),  # padding
    ]

class SockAddr (Union):
    _pack_ = 1
    _fields_ = [
        ('gen', SockAddr_Gen),
        ('in4', SockAddr_IPv4)
    ]

class InterfaceMap(Structure):
    _pack_ = 1
    _fields_ = [
        ('mem_start', c_ulong),
        ('mem_end', c_ulong),
        ('base_addr', c_ushort),
        ('irq', c_ubyte),
        ('dma', c_ubyte),
        ('port', c_ubyte)
    ]

class InterfaceData(Union):
    _pack_ = 1
    _fields_ = [
        ('ifr_addr', SockAddr),
        ('ifr_dstaddr', SockAddr),
        ('ifr_broadaddr', SockAddr),
        ('ifr_netmask', SockAddr),
        ('ifr_hwaddr', SockAddr),
        ('ifr_flags', c_short),
        ('ifr_ifindex', c_int),
        ('ifr_ifqlen', c_int),
        ('ifr_metric', c_int),
        ('ifr_mtu', c_int),
        ('ifr_map', InterfaceMap),
        ('ifr_slave', (c_ubyte * IFNAMSIZ)),
        ('ifr_newname', (c_ubyte * IFNAMSIZ)),
        ('InterfaceData', c_void_p)
    ]


class InterfaceReq(Structure):
    _pack_ = 1
    _fields_ = [
        ('ifr_name', (c_ubyte * IFNAMSIZ)),
        ('data', InterfaceData)
    ]

def getInterfaceFlags(name: str):
    _name = (c_ubyte * IFNAMSIZ)(*bytearray(name, 'utf-8'))
    ifr = InterfaceReq()
    ifr.ifr_name = _name
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)
    fcntl.ioctl(sock, SIOCGIFFLAGS, ifr)
    return ifr.data.ifr_flags

def setInterfaceUp(name: str):
    _name = (c_ubyte * IFNAMSIZ)(*bytearray(name, 'utf-8'))
    ifr = InterfaceReq()
    ifr.ifr_name = _name
    ifr.data.ifr_flags = IFF_UP
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, 0)
    fcntl.ioctl(sock, SIOCSIFFLAGS, ifr)
