'''
Created on 02-Dec-2013

@author: kanishk
'''
import socket 
import thread
import sys
import os
sys.path.append("../common")
import pickle
import fileInfo
import uuid


if __name__ == '__main__':
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM, 0, None)
    sock.connect((socket.gethostbyname("localhost"), 1710))
    file = sock.makefile("rw")
    pickle.dump(uuid.uuid1(), file)
    x = fileInfo.FileInfo()
    x.machineID = uuid.uuid1()
    x.parentID = 121
    x.myID = 112
    pickle.dump(x, file)
                 
    pass