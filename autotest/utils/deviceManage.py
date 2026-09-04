# encoding=utf-8

import os
import re
import threading
import time
from threading import Lock

import espressoControl
from projectConfig import installFlag
from projectConfig import rootPath
from projectConfig import uiautoFlag
from projectConfig import isDebugMode
from tests import conftest
from uiauto.uiauto import setSystemAuthorityByVIVO, setXiaoMiSystemAuthority
from utils import util1, adbCommand, configFileManage
from utils.util1 import getDeviceName
from utils.util1 import log
from utils.util1 import printLog

deviceInfoList = []
installUiautoDict = {}  # deviceId : uiauto-thread
installFailList = []
deviceLock = Lock()


class DeviceInfo():
    def __init__(self, deviceId, configFile):
        self.deviceId = deviceId
        self.configFile = configFile
        self.configInfo = configFileManage.ConfigInfo(configFile)
        self.clientPort = self.configInfo.getPort() + 30
        self.uiautoPort = self.configInfo.getPort() + 100
        self.runCaseName = None
        self.taskId = ''


def initDeviceInfos():
    devices = adbCommand.checkCellOnline()
    for device in devices:
        if isDebugMode or device in conftest.devicesFromCloud:
            configFile = configFileManage.getConfig()
            if configFile == None:
                printLog('warning:configfile nums less than devices')
                break
            adbCommand.pushConfigToDevice(device, configFile)
            deviceInfoList.append(DeviceInfo(device, configFile))


def getAllDevices():
    global installFailList
    installFailList = []
    # 跑任务前检查一下设备在线情况，去掉已经掉线的设备，加入新增的设备
    devicesOnline = adbCommand.checkCellOnline()
    devicesInwork = [device.deviceId for device in deviceInfoList]

    for device in devicesInwork:
        if device not in devicesOnline:
            for deviceInfo in deviceInfoList:
                if deviceInfo.deviceId == device:
                    configFileManage.recycleConfig(deviceInfo.configFile)
                    deviceInfoList.remove(deviceInfo)
                    printLog('%s is offline' % device)
                    break

    return deviceInfoList


# ==================================以下是跑连麦用例获取device的api========================================
def getFreeDeviceInfo(count, caseName=None, isImTest=False):
    deviceLock.acquire()
    freeDevices = []
    d = [x for x in deviceInfoList if x.runCaseName == caseName]
    configIndexFunc = lambda x: int(re.compile('(\d+)').search(x.configInfo.configFile).group(1))
    d.sort(key=configIndexFunc)
    if len(d) >= count:
        if not isImTest:
            freeDevices = d[0:count]
        else:
            # IM用例的config是配对好的1和2的A是好友，3和4，依次类推，这里保证每次至少取一个配对的设备
            for index, deviceInfo in enumerate(d):
                configIndex = configIndexFunc(deviceInfo)
                if configIndex % 2 == 1:
                    nextIndex = configIndex + 1
                    if index + 1 < len(d):
                        if nextIndex == configIndexFunc(d[index + 1]):
                            freeDevices.extend((deviceInfo, d[index + 1]))
                            break
    deviceLock.release()
    return freeDevices


def getFreeDevice(count):
    deviceLock.acquire()
    d = deviceInfoList
    configIndexFunc = lambda x: int(re.compile('(\d+)').search(x.configInfo.configFile).group(1))
    d.sort(key=configIndexFunc)
    if len(d) >= count:
        freeDevices = d[0:count]
    else:
        assert 0, '用例需要的设备数量不足！'
    deviceLock.release()
    return freeDevices


def setDeviceRunCase(freeDeviceInfos, caseName):
    deviceLock.acquire()
    freeDeviceIds = [d.deviceId for d in freeDeviceInfos]
    for deviceInfo in deviceInfoList:
        if deviceInfo.deviceId in freeDeviceIds:
            deviceInfo.runCaseName = caseName
    deviceLock.release()


# 给手机安装apk，推rTxt，开启uiautomator，跑海度用例(多端测试)
def setUpDevices(taskId, deviceInfos):
    threads = []
    for deviceInfo in deviceInfos:
        deviceInfo.runCaseName = None
        deviceInfo.taskId = taskId
        t = threading.Thread(target=installUiautoAndApk, args=(taskId, deviceInfo.deviceId, deviceInfo.uiautoPort))
        t.device = deviceInfo.deviceId
        t.start()
        time.sleep(2)
        threads.append(t)
    for t in threads:
        t.join(180)
    for t in threads:
        if t.isAlive():
            print("device %s is alive" % t.device)
            installFailList.append(t.device)


# 停掉手机的uiautomator
def tearDownDevices():
    global installFailList
    installFailList = []
    if uiautoFlag:
        for device, uiauto in installUiautoDict.items():
            print("stop device:" + device)
            uiauto.stop()
    installUiautoDict.clear()


def installUiautoAndApk(taskId, device, uiautoPort, apkFolder=''):
    pylog = open(os.path.join(rootPath, taskId, apkFolder, 'py_%s_%s.txt' % (device, getDeviceName(device))), 'w+')

    if uiautoFlag:
        log(pylog, 'install uiauto')
        print('install uiauto')
        uiauto = espressoControl.startUiauto(device, uiautoPort, pylog)
        installUiautoDict[device] = uiauto
    espressoControl.unlock(device, pylog)

    if installFlag:
        log(pylog, 'install apk')
        adbCommand.pushRTxt(taskId, device, apkFolder)
        if not isDebugMode:
            if not espressoControl.installApk(device, pylog, taskId, apkFolder):
                log(pylog, '%s install apk failed' % getDeviceName(device))
                # print('%s install apk failed' % getDeviceName(device))
                installFailList.append(device)
                if uiautoFlag:
                    uiauto = installUiautoDict[device]
                    uiauto.stop()
                    del installUiautoDict[device]
                pylog.close()
                return False
        # 针对vivo手机 运行任务之前 把摄像头权限拿到
        # 需处理的设备序列号见 projectConfig.uiautoVivoDevices
        if device in projectConfig.uiautoVivoDevices:
            setSystemAuthorityByVIVO(installUiautoDict[device])
        # 针对小米手机 运行任务之前 把摄像头权限拿到
        # 需处理的设备序列号见 projectConfig.uiautoXiaomiDevices
        if device in projectConfig.uiautoXiaomiDevices:
            setXiaoMiSystemAuthority(installUiautoDict[device])

    stopUiautoAfterInstall = False
    path = util1.getLogPath(taskId, 'TestConfig.txt')
    if os.path.exists(path):
        f = open(path, 'r+')
        ls = f.readlines()
        for e in ls:
            lower = e.lower().strip()
            if lower.startswith("stopuiautoafterinstall") and lower.endswith('true'):
                stopUiautoAfterInstall = True
                break
        f.close()

    if stopUiautoAfterInstall:
        uiauto = installUiautoDict[device]
        uiauto.stop()
        uiauto.join()
        del installUiautoDict[device]
        log(pylog, 'stop uiauto after install %s ' % getDeviceName(device))
        print('stop uiauto after install %s ' % getDeviceName(device))

    # if hiidoFlag:
    #     runSetHiidoSite(apkFolder, device, pylog, taskId)
    pylog.close()
    return True


def threadFun(i):
    for t in range(i):
        time.sleep(3)
        print(i)


if __name__ == "__main__":
    print("start")
    # th = threading.Thread(target=threadFun, args=(5,))
    # th.device = "ssddfd"
    # th.start()
    # for j in range(10):
    #     time.sleep(3)
    #     print th.device
    taskId = '2002'
    apkFolder = r'E:\Projects\PyTestSrc\EspressoTestSrc\share\2002'
    device = '85GBBMD22743'
    pylog = open(os.path.join(rootPath, taskId, apkFolder, 'py_%s_%s.txt' % (device, getDeviceName(device))), 'w+')
    espressoControl.updateTestPackageName(
        r'E:\Projects\PyTestSrc\EspressoTestSrc\share\2002\client-release-androidTest-signed.apk')
    espressoControl.updateClientPackageName(
        r'E:\Projects\PyTestSrc\EspressoTestSrc\share\2002\soda_client-1.0.0-SNAPSHOT-1-official.apk')
