NAME='netlink'
import os
if 'USE_INCLUDED_DIAG' in os.environ:
    CFLAGS=['-Iinclude']
GCC_LIST=['netlink']
