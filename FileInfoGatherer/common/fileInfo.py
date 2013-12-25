'''
Created on 29-Nov-2013

@author: kanishk
'''
import uuid

class FileInfo(object):
    '''
    classdocs
    '''
    machineID   = None 
    parentID    = 0
    myID        = 0
    createTime  = 0
    lastModifiedTime    = 0
    size                = 0
    accessType          = 0


    def __init__(self, machineID=None):
        '''
        Constructor
        '''
        self.machineID = machineID
        pass
        
    def recordFormat(self):
        return "MachineID,ParentID,myID,CreateTime,lastModifiedTime,size\n"
    
    def toString(self):
        return str(self.machineID) + "," + str(self.parentID) + "," + str(self.myID) + "," + str(self.createTime) + "," + str(self.lastModifiedTime) + "," + str(self.size) + "\n"
    
    
