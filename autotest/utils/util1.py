# encoding=UTF-8
import os
import platform
import re
import socket
import sys
import time
import traceback
from importlib import reload

import urllib.request
import datetime
import wrapt
import projectConfig

try:
    import xml.etree.cElementTree as ET
except ImportError:
    import xml.etree.ElementTree as ET

reload(sys)

dayTimer = lambda: time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(time.time()))
TAG = lambda taskId, device: '[%s-%s]' % (taskId, device)


def openUrl(url, logfile=None):
    socket = None
    content = None
    retryTime = 3
    while retryTime > 0:
        try:
            socket = urllib.request.urlopen(url)
            content = socket.read()
        except:
            if logfile:
                log(logfile, "except: urllib.urlopen %s error" % url)
            else:
                print("except: urllib.urlopen %s error" % url)
                traceback.print_exc()
            time.sleep(2)
        finally:
            retryTime = retryTime - 1
            if socket is not None:
                socket.close()
            if content:
                break
    return content


def getLogPath(taskId, logName, device=None):
    if not os.path.exists(projectConfig.rootPath):
        os.mkdir(projectConfig.rootPath)
    if not os.path.exists(os.path.join(projectConfig.rootPath, taskId)):
        os.mkdir(os.path.join(projectConfig.rootPath, taskId))
    if device:
        folder = os.path.join(projectConfig.rootPath, taskId, device)
        if not os.path.exists(folder):
            os.mkdir(folder)
        return os.path.join(folder, logName)
    else:
        return os.path.join(projectConfig.rootPath, taskId, logName)


# 将log写入文件
def log(fileNo, string, stringBefore='', stringAfter='\n'):
    if type(string) == list or type(string) == tuple:
        temp = ""
        for i in string:
            temp += (str(i) + " " * 2)
        string = temp
    fileNo.write(dayTimer() + " | " + stringBefore + string + stringAfter)
    fileNo.flush()


# 在控制台输出log
def printLog(string, stringBefore=''):
    print(dayTimer() + ' | ' + stringBefore + string)


def multiLog(logfile, logStr):
    logTime = lambda: time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time()))
    # 方便本地调试
    if projectConfig.isDebugMode:
        sys.stdout.write(logTime() + ' ' + logStr + '\n')
        sys.stdout.flush()
    if logfile is not None:
        log(logfile, logStr)

    print(logTime() + ' ' + logStr)


def totalLog(logfile, logStr):
    if logfile is not None:
        log(logfile, logStr)


# args：windows输入int,指第几个网卡，0开始；
#       linux输入网卡名"eth0"
def getLocalIP(ifname):
    if isWindows():
        return socket.gethostbyname_ex(socket.gethostname())[-1][ifname]
    else:
        import fcntl, struct
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        inet = fcntl.ioctl(s.fileno(), 0x8915, struct.pack('256s', ifname[:15]))
        ret = socket.inet_ntoa(inet[20:24])
        return ret


def logFuncTime():
    @wrapt.decorator
    def wrapper(wrapped, instance, args, kargs):
        begin = time.time()
        print('begin:' + wrapped.__name__)
        res = wrapped(*args, **kargs)
        print('end:%s %s' % (wrapped.__name__, str(time.time() - begin)))
        return res

    return wrapper


def isWindows():
    return platform.system() == "Windows"


def isMac():
    return platform.system().lower() == 'darwin'


def get_Mac_ip():
    '''可认为是本地调试'''
    return '127.0.0.1'
    # return socket.gethostbyname(socket.getfqdn(socket.gethostname()))


# 获取文件中第一行的时间和最后一行的时间
def getRunTime(filePath):
    file = open(filePath, encoding='GBK')
    data = file.read()
    startTime = '00:00:00'
    endTime = '00:00:00'
    if not data:
        file.close()
        return startTime, endTime

    dataLine = data.split('\n')
    for line in dataLine:
        startTime = getTime(line)
        if startTime != '00:00:00':
            break
    for line in reversed(dataLine):
        endTime = getTime(line)
        if endTime != '00:00:00':
            break
    file.close()
    return startTime, endTime


def getTime(line):
    timeRegex = '(\d{2}:\d{2}:\d{2})'
    try:
        return re.search(timeRegex, line).group()
    except:
        pass
    return '00:00:00'


def getDateTime(strtime):
    endTimes = strtime.split(':')
    return datetime.datetime(2017, 4, 1, hour=int(endTimes[0]), minute=int(endTimes[1]),
                             second=int(endTimes[2]))


# 将时间秒转换成时，分，秒格式
def durationsToStr(time):
    if time == '':
        return ''
    if "." in time:
        time = time.split(".")[0]
    duration = int(time)

    durationSecond = duration % 60
    durationMin = int(duration / 60 % 60)
    durationHour = int(duration / 60 / 60)
    durationString = ''
    if durationHour != 0:
        durationString = "%d时%d分%d秒" % (durationHour, durationMin, durationSecond)
    elif durationMin != 0:
        durationString = "%d分%d秒" % (durationMin, durationSecond)
    elif durationSecond != 0:
        durationString = "%d秒" % (durationSecond)
    else:
        print("duration is 0!")
    return durationString


def getDeviceName(device):
    deviceInfoCommand = 'adb -d -s %s shell getprop ro.product.model'
    deviceName = os.popen(deviceInfoCommand % device).read()
    if '\n' in deviceName:
        deviceName = deviceName.replace('\n', '')
    return deviceName


def getUid(configFile, user):
    return getXML(configFile, user + "/UID")


def getUids(configFile):
    users = ["UserA", "UserB", "UserC", "UserD", "UserE"]
    uids = []
    configFile = os.path.join('config', configFile)
    for user in users:
        uids.append(getUid(configFile, user))
    return uids


def getXML(configFile, node):
    tree = ET.ElementTree(file=configFile)
    for elem in tree.iterfind(node):
        return elem.text
    print("[Warning!]", node, " not found!")
