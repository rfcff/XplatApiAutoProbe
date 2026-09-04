# encoding=UTF-8

import json
import time
import urllib
from datetime import datetime

import requests
from pytz import timezone

from command.common import STOP_C, SDK_VERSION, FEEDBACK
from multiTest.driver import Driver
from multiTest.multiCaseRun import PASS
from tests import conftest
from utils import configFileManage, deviceManage
from utils.util1 import getDeviceName
import projectConfig
import uiautomator2 as u2


def setupModule():
    # 初始化configFile和device
    configFileManage.initConfigWithParam(conftest.appid, conftest.uid1, conftest.uid2, conftest.uid3, conftest.channel1,
                                         conftest.channel2)
    deviceManage.initDeviceInfos()

    # 获取device
    deviceInfos = deviceManage.getAllDevices()

    # 注册watcher
    for device in deviceInfos:
        watcher(device.deviceId)

    # 获取device
    assert len(deviceInfos), "no device on line"

    print('get device: %s to run task:' % ','.join([deviceInfo.deviceId for deviceInfo in deviceInfos]))


def watcher(remoteConnectAddress):
    d = u2.connect_usb(remoteConnectAddress)
    d.watcher.when("允许").click()
    d.watcher.when("总是允许").click()
    d.watcher.when("立即开始").click()
    d.watcher.when("START NOW").click()
    d.watcher.start()


# 测试用例结果上报需要的参数
params = {
    "appid": 0,  # appid
    "createBy": "string",  # 创建人
    "createTime": "2020-06-09T16:37:13.663Z",  # 创建时间
    "endTime": "2020-06-09T16:37:13.663Z",  # 结束时间
    "id": 0,  # id 自增
    "info": "",  # 手机信息
    "logPath": "string",  # 日志下载链接
    "scriptContent": "string",  # 脚本内容
    "status": 0,  # 测试结果 0 -> 失败，1 ->成功
    "testcaseName": "string",  # 测试用例英文名
    "testcaseNameCn": "string",  # 测试用例中文名
    "sdkVer": ""  # sdk版本
}

caseId = 0  # 记录case的id

users = []  # 记录正在跑用例的设备


def getUsers():
    return users


def setUp(userNum, caseName, caseNameCn='', createBy='zhouyusheng'):
    global caseId
    global params
    caseId += 1
    params['appid'] = conftest.appid
    params['createBy'] = createBy
    cst_tz = timezone('Asia/Shanghai')
    now = datetime.now().replace(tzinfo=cst_tz)
    params['createTime'] = now.strftime('%Y-%m-%d %H:%M:%S')
    params['testcaseName'] = caseName
    params['testcaseNameCn'] = caseNameCn
    params['id'] = caseId
    deviceInfos = deviceManage.getFreeDevice(userNum)
    for deviceInfo in deviceInfos:
        if not params['info']:
            params['info'] += getDeviceName(deviceInfo.deviceId).strip()
        else:
            params['info'] += ',' + getDeviceName(deviceInfo.deviceId).strip()
        driver = Driver(deviceInfo)
        driver.start()
        users.append(driver)


def tearDown(result):
    global params
    cst_tz = timezone('Asia/Shanghai')
    now = datetime.now().replace(tzinfo=cst_tz)
    endTime = now.strftime('%Y-%m-%d %H:%M:%S')
    caseUser = getUsers()
    feedbackUrls = []  # 反馈url
    reportUrls = []  # 数据上报格式的反馈url
    if result != PASS:
        params['status'] = 0
    else:
        params['status'] = 1

    for user in caseUser:
        user.step(FEEDBACK, "反馈日志", {"feedbackData": endTime, "device": user.deviceInfo.deviceId})
        # 等待反馈完成
        time.sleep(5)
        logs = projectConfig.reportUrl + endTime + user.deviceInfo.deviceId
        reportUrls.append({"phone": getDeviceName(user.deviceInfo.deviceId), "logs": logs})
        feedbackUrls.append(logs)
        sdkVersion = user.step(SDK_VERSION)
        # sdk版本上报需要统一编码格式，将括号等特殊符号进行编码处理
        sdkVersion = urllib.parse.quote(sdkVersion)
        if (sdkVersion is None):
            sdkVersion = ''
        if sdkVersion not in params['sdkVer']:
            if not params['sdkVer']:
                params['sdkVer'] += sdkVersion
            else:
                params['sdkVer'] += ',' + sdkVersion

    params['logPath'] = json.dumps(reportUrls)

    # 结束测试，关闭日志文件和日志子进程,并从users列表中移除user
    for user in caseUser:
        try:
            user.step(STOP_C, "结束测试")
        except Exception:
            pass
        user.finish()
    users.clear()
    params['endTime'] = endTime
    from projectConfig import isDebugMode
    if not isDebugMode:
        # 数据上报
        url = projectConfig.resultReportUrl
        result = requests.post(url, json=params)
        print('数据上报结果: %s' % result.json())
    return feedbackUrls
