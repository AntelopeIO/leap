import ctypes
import os

libc = ctypes.CDLL(None)

libc.unshare.argtypes = [ctypes.c_int]
libc.__errno_location.restype = ctypes.POINTER(ctypes.c_int)

# Aliased here because the API for retrieving errno varies across platforms
get_errno_loc = libc.__errno_location

# Extracted from linux-source-4.15.0/include/uapi/linux/sched.h
# The values of these flags in the kernel are extremely unlikely to change, so they're copied here.
# There are more than 20 CLONE_ constants.  If support for a large number of them is desirable,
# run h2py.py on sched.h and import * from the resulting SCHED.py.
# https://github.com/PexMor/unshare/blob/master/h2py.py
CLONE_NEWUSER = 0x10000000
CLONE_NEWNET  = 0x40000000

def unshare(flags):
    '''Disassociate part of execution context
    
    Call libc unshare with flags.  If unprivileged, first unshare user namespace
    and map current user to root in the new namespace.
    '''
    uid = os.getuid()
    if uid != 0 or os.getenv('GITHUB_ACTIONS'):
        uidmap = f'0 {uid} 1'
        rc = libc.unshare(CLONE_NEWUSER)
        if rc == -1:
            raise Exception(f'unshare() error code: {get_errno_loc().contents.value}')
        with open('/proc/self/uid_map', 'w') as f:
            f.write(uidmap)
    rc = libc.unshare(flags)
    if rc == -1:
        raise Exception(f'unshare() error code: {get_errno_loc().contents.value}')
