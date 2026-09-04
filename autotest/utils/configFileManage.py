# encoding=utf-8


import glob
import os
import queue
import re

from projectConfig import configPath
from utils import xmlUtil
from utils.util1 import printLog

try:
    import xml.etree.cElementTree as ET
except ImportError:
    import xml.etree.ElementTree as ET

configFiles = queue.Queue()


def getXMLNodeValue(configFile, node):
    tree = ET.ElementTree(file=configFile)
    for elem in tree.iterfind(node):
        return elem.text
    print("[Warning!]", node, " not found!")


class ConfigInfo():
    def __init__(self, configFile):
        self.configFile = configFile

    def getPort(self):
        return int(self.getXml('Common/localHttpServerPort'))

    def getUserAStartLiveId(self):
        return int(self.getXml('UserA/startLiveId'))

    def getUserANickName(self):
        return self.getXml('UserA/nickName')

    def getUserAYYID(self):
        return self.getXml('UserA/yyID')

    def getUserAUID(self):
        return self.getXml('UserA/UID')

    def getMyUID(self):
        return self.getXml('UserA/UID')

    def getAnotherUID(self):
        return self.getXml('UserB/UID')

    def getUserBNickName(self):
        return self.getXml('UserB/nickName')

    def getUserAPassword(self):
        return self.getXml('UserA/userPassword')

    def getUserAUserName(self):
        return self.getXml('UserA/userName')

    def getUserBUserName(self):
        return self.getXml('UserB/userName')

    def getUserBUID(self):
        return self.getXml('UserB/UID')

    def getUserBPassword(self):
        return self.getXml('UserB/userPassword')

    def getUserAMultiGroupName(self):
        return self.getXml('UserA/GroupName_multi')

    def getUserCYYID(self):
        return self.getXml('UserC/yyID')

    def getUserCNickName(self):
        return self.getXml('UserC/nickName')

    def getUserCUserName(self):
        return self.getXml('UserC/userName')

    def getUserCPassword(self):
        return self.getXml('UserC/userPassword')

    def getUserCGroup3Name(self):
        return self.getXml('UserC/groupName_3')

    def getUserCGroup3ID(self):
        return self.getXml('UserC/groupID_3')

    def getUserDUserName(self):
        return self.getXml('UserD/userName')

    def getUserDPassword(self):
        return self.getXml('UserD/userPassword')

    def getUserDMultiGroup2Name(self):
        return self.getXml('UserD/GroupName_multi2')

    def getUserDMultiGroup2ID(self):
        return self.getXml('UserD/GroupID_multi2')

    def getXml(self, node):
        return getXMLNodeValue(os.path.join(configPath, self.configFile), node)


def initConfig():
    configs = glob.glob1(configPath, 'config*.xml')
    # 反序取config，保证测试4套长uid账号
    configs.sort(key=lambda x: int(re.compile('(\d+)').search(x).group(1)), reverse=True)
    for config in configs:
        configFiles.put(config)
    printLog('get configfile: %s' % (','.join(configs)))


def initConfigWithParam(appId, userAId, userBId, userCId, channelId1, channelId2):
    xmlUtil.generateConfig('9092', appId, userAId, userBId, channelId1, channelId2, 'config/config_1.xml')
    xmlUtil.generateConfig('9093', appId, userBId, userAId, channelId1, channelId2, 'config/config_2.xml')
    # 第三台设备
    xmlUtil.generateConfig('9094', appId, userCId, userAId, channelId1, channelId2, 'config/config_3.xml')
    configs = glob.glob1(configPath, 'config*.xml')
    configs.sort(key=lambda x: int(re.compile('(\d+)').search(x).group(1)))
    for config in configs:
        configFiles.put(config)
    printLog('get configfile: %s' % (','.join(configs)))


def getConfig():
    config = None
    if not configFiles.empty():
        config = configFiles.get()
    return config


def recycleConfig(configFile):
    configFiles.put(configFile)


if __name__ == '__main__':
    initConfig()
    print(getConfig())
    print(getConfig())
    configInfo = ConfigInfo(getConfig())
    print(configInfo.getUserAUserName())
