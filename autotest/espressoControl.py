# encoding=utf-8
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from importlib import reload

import projectConfig
from utils import taskHelper1, util1
from utils.util1 import getLogPath
from utils.util1 import log

unlockCommand = "adb -s %s shell am start -n %s/.Unlock"
clientApp = projectConfig.clientApkPattern
testApp = "client-release-androidTest-signed.apk"
uninstallCommand = "adb -s %s uninstall %s"
installConmmand = "adb -s %s install %s"
clientPackage = projectConfig.clientPackage
testPackage = projectConfig.testPackage
unlockPackage = "io.appium.unlock"
uiautomatorPackage = "com.github.uiautomator"
# mkdirCommand1 = "adb -s %s shell mkdir -p /sdcard/thundertmp/"
installCommand1 = "adb -s %s push %s /sdcard/thundertmp/%s.apk"
installCommand2 = "adb -s %s shell pm install -r /sdcard/thundertmp/%s.apk"
installDemoCommand = "adb -s %s install -r ./share/%s/" + projectConfig.clientApkName
installCommand12 = "adb -s %s install -r %s"
listCommand = "adb -s %s shell pm list packages"
unlockApp = "unlock.apk"

from uiauto.uiauto import UiAuto

reload(sys)
sys.path.append(os.path.join(os.getcwd(), "uiauto", "uiauto"))

appName = {}  # {taskId:appName}
DEFAULT = 'default'


def setAppName(taskId, name, download_folder=''):
    if not taskId in appName.keys():
        appName[taskId] = {}
    if not download_folder:
        appName[taskId][DEFAULT] = name
    else:
        appName[taskId][download_folder] = name


def getAppName(taskId, download_folder=''):
    if not (taskId in appName.keys()):
        return ""
    if download_folder:
        return appName[taskId][download_folder]
    return appName[taskId][DEFAULT]

def updateClientPackageName(appPath):
    global clientPackage
    packageName = getPackageName(appPath)
    clientPackage = packageName.decode()
    # print clientPackage


def getPackageName(appPath):
    sysstr = sys.platform
    packageName = ''
    txt = ''
    if sysstr == "win32":
        try:
            txt = subprocess.check_output([r"./tools/aapt.exe", 'dump', 'badging', appPath])
        except subprocess.CalledProcessError as e:
            raise RuntimeError("command '{}' return with error (code {}): {}".format(e.cmd, e.returncode, e.output))
    elif sysstr == "linux2":
        try:
            txt = subprocess.check_output([r"./tools/aapt", 'dump', 'badging', appPath])
        except subprocess.CalledProcessError as e:
            raise RuntimeError("command '{}' return with error (code {}): {}".format(e.cmd, e.returncode, e.output))
    elif sysstr == "darwin":
        try:
            txt = subprocess.check_output([r"./tools/mac/aapt", 'dump', 'badging', appPath])
        except subprocess.CalledProcessError as e:
            raise RuntimeError("command '{}' return with error (code {}): {}".format(e.cmd, e.returncode, e.output))
    else:
        raise Exception('未设定的系统', sysstr)
    packageName = re.compile(b'package:\s+name=\'(.+)\'\s+versionCode').search(txt).group(1)
    return packageName


def updateTestPackageName(appPath):
    global testPackage
    packageName = getPackageName(appPath)
    testPackage = packageName
    # print testPackage


def downloadAPP(url, taskId, download_folder, logfile):
    downLoadReleaseVersion = url.strip("/").split("/")[-1]
    log(logfile, "download %s" % downLoadReleaseVersion)
    print("download %s" % downLoadReleaseVersion)
    url += '' if url[-1] == '/' else '/'
    log(logfile, "downloadAPP url: %s" % url)
    print("downloadAPP url: %s" % url)

    relink = '<a href="(.*?\.apk)"'
    # relink = '"(.*?\.apk)"'

    content = util1.openUrl(url)
    appList = []
    if content:
        appList = re.findall(relink, content.decode())

    clientApp1 = ""
    for i in appList:
        temp = re.findall(clientApp, i)
        if temp:
            clientApp1 = temp[0]
            setAppName(taskId, clientApp1, download_folder)
            break
    log(logfile, "download :" + clientApp1)
    print("download :" + clientApp1)

    def checkAppExist():
        for temp in appList:
            if clientApp1 in temp:
                return True
        return False
        # if clientApp1 not in appList:
        #     log(logfile, "download %s not found in %s" % (clientApp1, downLoadReleaseVersion))
        #     print("clientApp %s not found in %s" % (clientApp1, downLoadReleaseVersion))
        #     return False

        # if testApp not in appList:
        #     log(logfile, "download %s not found in %s" % (testApp, downLoadReleaseVersion))
        #     print("testApp %s not found in %s" % (testApp, downLoadReleaseVersion))
        #     return False

    index = 0
    while (not checkAppExist()):
        time.sleep(60)
        index += 1
        if index > 3:
            logfile.close()
            return False

    clientAppUrl = url + clientApp1
    # testAppUrl = url + testApp

    clientAppPath = getLogPath(taskId, clientApp1, download_folder)
    urllib.request.urlretrieve(clientAppUrl, clientAppPath)
    # testAppPath = getLogPath(taskId, testApp, download_folder)
    # urllib.urlretrieve(testAppUrl, testAppPath)

    log(logfile, "download clientApp size:%s" % os.path.getsize(clientAppPath))
    # log(logfile, "download testApp size:%s" % os.path.getsize(testAppPath))
    updateClientPackageName(clientAppPath)
    # updateTestPackageName(testAppPath)
    # try:
    #     downloadRtxt(logfile, url, taskId, download_folder)
    # finally:
    logfile.close()
    return True


sharePath = projectConfig.sharePath
monkeyApkFolder = 'monkey_apk'
remoteIP = projectConfig.remoteIP
# 共享目录访问凭据：从环境变量读取，仓库内不保存
user = os.environ.get("XPROBE_SHARE_USER", "")
password = os.environ.get("XPROBE_SHARE_PASS", "")


def installApk(device, logfile, taskId, apkFolder=''):
    if apkFolder == taskHelper1.keyTargetVersion:
        log(logfile, 'installApk testing update %s' % apkFolder)
        pass
    else:
        log(logfile, "installApk device: %s start." % device)
        log(logfile, ("installApk uninstall %s:" % clientPackage) + os.popen(uninstallCommand % (device, clientPackage)).read())
    log(logfile, "installApk install Client start.")
    print("installApk install Client start.")

    folder = getLogPath(taskId, getAppName(taskId, apkFolder), apkFolder)
    log(logfile, "push app:" + folder + ' ' + os.popen(installCommand1 % (device, folder, clientPackage)).read())
    time.sleep(3)
    installResult = os.popen(installDemoCommand % (device, taskId)).read()
    log(logfile, "install app:" + installResult)
    print("install app:" + installResult)
    if 'success' not in installResult.lower():
        return False
    log(logfile, "installApk device: %s end." % device)
    print("installApk device: %s end." % device)
    return waitForInstallCompeleted(device, logfile)


def installTargetApk(device, logfile, taskId, packageName, apk):
    # 卸载app
    log(logfile, 'uninstall app %s: %s' % (packageName, os.popen(uninstallCommand % (device, packageName)).read()))
    # 安装app
    appPath = os.path.join('share', taskId, apk)
    log(logfile, 'install app %s' % apk)
    installResult = os.system(installConmmand % (device, appPath))
    if installResult == 0:
        log(logfile, 'install succeed!')
        return True
    else:
        log(logfile, 'install fail!')
        return False


def startUiauto(device, port, logfile):
    log(logfile, "startTest startUiautoMonitor")
    uiauto = UiAuto(device, port)
    uiauto.setDaemon(True)
    uiauto.start()
    time.sleep(10)
    log(logfile, "startUiautoMonitor success")
    return uiauto


def unlock(device, logfile, simple=False):
    if not simple:
        listInfo = os.popen(listCommand % device).read()
        # log(logfile,listInfo)

        if unlockPackage not in listInfo:
            log(logfile, "install unlock.apk")
            log(logfile, os.popen(installCommand12 % (device, unlockApp)).read())
    log(logfile, "do unlock")
    log(logfile, os.popen(unlockCommand % (device, unlockPackage)).read())


def waitForInstallCompeleted(device, logfile):
    timeout = 60
    while timeout > 0:
        fd = os.popen(listCommand % device)
        output = fd.read()
        fd.close()
        if clientPackage in output:
            return True
        log(logfile, "test app installing is not finish,wait 10 seconds.")
        time.sleep(10)
        timeout -= 10
    return False


# 下载app
def downloadAppWithHttp(url, taskId, apkFolder=""):
    logfile = open(getLogPath(taskId, 'py_all.txt'), 'a+')
    if projectConfig.installFlag:
        try:
            return downloadAPP(url, taskId, apkFolder, logfile)
        except (urllib.error.URLError, urllib.error.HTTPError) as e:
            print("downloadAppWithHttp", e)
            log(logfile, str(e) + " pls check URL!")
            return False
        finally:
            logfile.close()
    return True


def getAppPid(device, timeout=60):
    pid = None
    while timeout > 0:
        pid = getPid(device)
        if pid:
            return pid
        time.sleep(1)
        timeout -= 1
    return pid


def getPid(device):
    psCommand = "adb -s %s shell ps |" + ("findstr" if util1.isWindows() else "grep") + " %s"
    lines = os.popen(psCommand % (device, clientPackage)).readlines()
    for l in lines:
        if l.split(' ')[-1].strip() == clientPackage:
            for index, value in enumerate(l.split(' ')):
                if index > 0 and value != ' ' and value != '':
                    return value
    return None


if __name__ == '__main__':
    # print getRTxt('http://172.25.37.30:8000/dwbuild/mobile/android/soda/soda-android_develop/20180117-310-r1968445/')
    # print getRTxt('http://repo.yypm.com/dwbuild/mobile/android/entmobile/entmobile-android_7.4.0_startup_feature/20180118-56722-r718032/')
    # updateClientPackageName(r'E:\Projects\PyTestSrc\EspressoTestSrc\share\2002\client-release-androidTest-signed.apk')
    # print testPackage
    # updateClientPackageName(
    #     r'E:\Projects\PyTestSrc\EspressoTestSrc\share\2002\soda_client-1.0.0-SNAPSHOT-1-official.apk')
    # print clientPackage
    # print(const.AppConfig)
    pass
