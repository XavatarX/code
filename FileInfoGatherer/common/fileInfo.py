'''
Created on 29-Nov-2013

@author: kanishk
'''
import uuid

class FileInfo(object):
    '''
    classdocs
    '''
    machineID   = uuid.uuid1() 
    parentID    = 0
    myID        = 0
    createTime  =   0l
    lastModifiedTime    = 0l
    size                = 0l


    def __init__(self):
        '''
        Constructor
        '''
        pass
    def recordFormat(self):
        return "MachineID,ParentID,myID,CreateTime,lastModifiedTime,size\n"
    
    def toString(self):
        return str(self.machineID) + "," + str(self.parentID) + "," + str(self.myID) + "," + str(self.createTime) + "," + str(self.lastModifiedTime) + "," + str(self.size) + "\n"
    
    