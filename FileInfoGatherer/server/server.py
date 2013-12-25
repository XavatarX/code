
import socket 
import threading
import sys
import os
sys.path.append("../common")
import pickle
import fileInfo
import uuid
import pdb

PATH="/mnt/data/tmp/"


def sthread (threadsname, clientsock):
    fh = clientsock.makefile("rb")
    # The first thing sent by the remote thing is the machine uuid
    # After that the standard FileInfo follows.
    machineID = pickle.load(fh)
    file = open(PATH+machineID.__str__(), 'a')
    fileattrs= fileInfo.FileInfo()
    file.write(fileattrs.recordFormat())
    
    while (True):
        try:
            fileattrs = pickle.load(fh)
        except  EOFError:
            file.close()
            break
        file.write(fileattrs.toString())
        print ( "\n" + fileattrs.toString())
    
        

if __name__ == '__main__':
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0, None)
    sock.bind((socket.gethostbyname("localhost"), 1710))
    sock.listen(10)
    
    while (1):
        (clientsock, address) = sock.accept()
        print ("Connection accepted")
        t = threading.Timer(0, sthread,("Thread-1", clientsock))
        t.start()
        #thread.start_new_thread(sthread, ("Thread-1", clientsock))
