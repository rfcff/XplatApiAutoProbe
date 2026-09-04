# encoding=utf-8

import glob
import os
import re
import time

import espressoControl
import projectConfig
from utils.util1 import getDeviceName
from utils.util1 import log
from utils.util1 import printLog

adbRebootCommand = "adb -s %s reboot"

pullCommand = "adb -s %s pull /sdcard/espresso %s"

adbPushConfig = 'adb -s %s push config/%s /sdcard/espressoConfig/config.xml'
pushRtxt = "adb -s %s push %s /sdcard/espressoConfig/%s"

removeFolderCommand = "adb -s %s shell rm -rf %s"

espressoPicPath = '/sdcard/espresso'



pullAnrLogCommand = "adb -s %s pull /data/anr %s"
zipCommand = "tar -zcvf %s %s"

mkdirCommand = "adb -s %s shell mkdir -p /sdcard/espresso/"

# 清除device日志缓存
clearLogcatCammand = "adb -s %s shell logcat -c"
clearMainLogcatCommand = "adb -s %s shell logcat -b main -c"
clearSystemLogcatCommand = "adb -s %s shell logcat -b system -c"


def restart_adb():
    retry = 3
    while retry > 0:
        os.popen("adb kill-server").read()
        time.sleep(1)
        if 'successfully' in os.popen("adb start-server").read():
            return True
        retry -= 1
    else:
        return False


def checkCellOnline():
    devices = re.findall("(\S+)\s*device\n", os.popen('adb devices').read())
    return devices


def pushConfigToDevice(device, config):
    if projectConfig.pushConfigFile:
        printLog('push %s to %s' % (config, device))
        print(os.popen(adbPushConfig % (device, config)).read())


def pushRTxt(taskId, device, apkFolder=''):
    rTxtPath = os.path.join(projectConfig.rootPath, taskId, apkFolder)
    txts = glob.glob1(rTxtPath, '*R.txt')
    if not txts:
        return
        # txts = projectConfig.rTxt
    for r in txts:
        os.popen(pushRtxt % (device, os.path.join(rTxtPath, r), r)).read()


# 清系统日志缓存
def clearDeviceLogCache(device):
    os.popen(clearMainLogcatCommand % device)
    os.popen(clearSystemLogcatCommand % device)
    os.popen(clearLogcatCammand % device)


# 拉取运行日志
def pullMobileLog(taskId, device, apkFolder=''):
    yymobileLogPath = projectConfig.appLogPath
    pullLogCommand = "adb -s %s pull " + yymobileLogPath + " %s"
    pylog = os.path.join(projectConfig.rootPath, taskId, apkFolder, 'py_%s_%s.txt' % (device, getDeviceName(device)))
    logFolder = os.path.join(projectConfig.rootPath, taskId, apkFolder, device)
    mobileLog = os.path.join(logFolder, 'mobileLog')
    if not os.path.exists(mobileLog):
        os.makedirs(mobileLog)
    pylogFile = open(pylog, 'a+')
    log(pylogFile, 'pull app log: ' + os.popen(pullLogCommand % (device, mobileLog)).read())
    log(pylogFile, 'pull anr log:' + os.popen(pullAnrLogCommand % (device, mobileLog)).read())
    deleteDeviceFolder(device, yymobileLogPath)
    pylogFile.close()


# 拉取monkey测试各个app的日志
def pullAppLog(taskId, device, packageName):
    appLog = os.path.join(projectConfig.rootPath, taskId, device, 'appLog')
    if not os.path.exists(appLog):
        os.makedirs(appLog)
    # pull app log：包名 -> 设备日志路径的映射见 projectConfig.appLogPathMap
    pullCommandFormat = 'adb -s %s pull %s %s'
    appLogPath = projectConfig.appLogPathMap.get(packageName)
    pullCommand = pullCommandFormat % (device, appLogPath, appLog) if appLogPath else None
    if pullCommand:
        os.popen(pullCommand)
    # pull anr log
    os.popen(pullAnrLogCommand % (device, appLog))


# 拉取手机的截图
def pullMobilePic(taskId, device, apkFolder=''):
    pylog = os.path.join(projectConfig.rootPath, taskId, apkFolder, 'py_%s_%s.txt' % (device, getDeviceName(device)))
    resultPath = os.path.join(projectConfig.rootPath, taskId, apkFolder, device, 'result')
    if not os.path.exists(resultPath):
        os.makedirs(resultPath)
    pylogFile = open(pylog, 'a+')
    log(pylogFile, 'pull pic: ' + os.popen(pullCommand % (device, resultPath)).read())
    # 删除手机里的截图
    deleteDeviceFolder(device, espressoPicPath)
    pylogFile.close()


# 删除手机里某个文件夹
def deleteDeviceFolder(device, folder):
    os.popen(removeFolderCommand % (device, folder))


# adb 截图
def pic(device, name):
    os.popen(mkdirCommand % device)
    screencapCommand = "adb -s %s shell /system/bin/screencap -p /sdcard/espresso/%s.jpg"
    startTime = time.time()
    os.popen(screencapCommand % (device, name)).read()
    cost = time.time() - startTime
    if cost > 10:
        printLog('%s -----pic time out %d sencond!-----' % (device, cost))


# 强制停止app
def forceStopApp(device):
    stopCommand = "adb -s %s shell am force-stop " + espressoControl.clientPackage
    os.popen(stopCommand % device)


# 强制停止app
def stopApp(device, packageName):
    stopCommand = 'adb -s %s shell am force-stop %s' % (device, packageName)
    os.popen(stopCommand)


TestRunner = projectConfig.testRunner
multiTestRunner = projectConfig.testRunner


def getTestRunner(device):
    regex = "instrumentation:(.*)\s+\(target=(.*)\)"
    listInstrumentCommand = "adb -s %s shell pm list instrumentation" % device
    testRunners = os.popen(listInstrumentCommand).read().split('\n')
    for testRunner in testRunners:
        if espressoControl.clientPackage in testRunner:
            match = re.compile(regex).match(testRunner)
            if match:
                global multiTestRunner
                multiTestRunner = match.group(1)
                return match.group(1)
    return TestRunner


if __name__ == '__main__':
    resultPath = os.path.join('share', '2001')
    device = '6d407e2b'
    testRunner = getTestRunner(device)
    pullMobilePic(resultPath, device)
    # pic('14dc2c34', '1')
    # mkdir(device)
