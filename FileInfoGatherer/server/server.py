
import socket 
import thread
import sys
import os
sys.path.append("../common")
import pickle
import fileInfo
import uuid
PATH="/mnt/data/tmp/"


def sthread (threadsname, clientsock):
    fh = clientsock.makefile("rw")
    # The first thing sent by the remote thing is the machine uuid
    # After that the standard FileInfo follows.
    machineID = pickle.load(fh)
    file = open(PATH+machineID.__str__(), 'a')
    fileattrs= fileInfo.FileInfo()
    file.write(fileattrs.recordFormat())
    
    while (True):
        fileattrs = pickle.load(fh)
        file.write(fileattrs.toString())
        pass
    
        

if __name__ == '__main__':
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0, None)
    sock.bind((socket.gethostbyname("localhost"), 1710))
    sock.listen(10)
    
    while (1):
        (clientsock, address) = sock.accept()
        print "Found connection for "
        thread.start_new_thread(sthread, ("Thread-1", clientsock))
    pass
