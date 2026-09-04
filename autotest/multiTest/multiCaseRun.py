# encoding=UTF-8
import inspect
import json
import os
import threading
import time
import traceback
from datetime import datetime

import requests
import wrapt
from pytz import timezone

import projectConfig
from command.common import STOP_C, FEEDBACK
from utils import util1, adbCommand, deviceManage
from utils.util1 import getDeviceName
from utils.util1 import multiLog
from .driver import Driver
from .exception import *
from .multiCaseMgr import caseInfos, feedbackData

caseDescription = {}
caseRunResult = {}

PASS = "pass"
STEP_FAILURE = "stepFailedException"
SERVER_ERROR = "httpServerError"
NET_ERROR = "networkException"
RETURN_ERROR = "returnValueException"
VERIFY_ERROR = 'verifyException'
NORMAL_ERROR = 'normalException'
APP_TIMEOUT = 'appTimeOut'  # crash也归为这一类型


def initCaseRunResult():
    global caseRunResult
    caseRunResult = {}


caseResultLock = threading.Lock()


def isCaseSuccess(caseName):
    return caseRunResult[caseName] == PASS


def getCaseRunned():
    return len(caseRunResult)


def recordCaseResult(taskId):
    from .multiCaseMgr import caseRunDeviceDict
    caseResultFile = open(util1.getLogPath(taskId, 'caseResult.txt'), 'w+')
    for case, result in caseRunResult.items():
        devices = caseRunDeviceDict[case]
        # caseResultFile.write(case + ' ' + result + ' ' + devices[0] + ' ' + devices[1] + '\n')
        caseResultFile.write(case + ' ' + result + ' ' + devices[0] + '\n')
    caseResultFile.close()


def setCaseRunResult(result, case):
    caseResultLock.acquire()
    caseRunResult[case] = result
    caseResultLock.release()


def Test(text):
    @wrapt.decorator
    def wrapper(wrapped, instance, args, kwargs):
        exceptionDeviceId = None
        multiLogFile = None
        try:
            userNum = inspect.getargspec(wrapped).defaults[0]
            setUp(userNum, wrapped.__name__, text)
            from .driver import caseMultiLog
            multiLogFile = caseMultiLog[wrapped.__name__]
            multiLog(multiLogFile, '################ begin case ################：[%s]: %s' % (wrapped.__name__, text))
            caseDescription[wrapped.__name__] = text
            wrapped(*args, **kwargs)
            setCaseRunResult(PASS, wrapped.__name__)
        except TestFailNotify as e:
            exceptionDeviceId = e.deviceId
            multiLog(multiLogFile, "################ step fail ###########################")
            multiLog(multiLogFile, '%s | %s' % (e.deviceId + ' : ' + getDeviceName(e.deviceId).strip(), e.exceptionMsg))
            multiLog(multiLogFile, traceback.format_exc())
            print("################ step fail ###########################")
            print('%s | %s' % (e.deviceId + ' : ' + getDeviceName(e.deviceId).strip(), e.exceptionMsg))
            print(traceback.format_exc())
            setCaseRunResult(STEP_FAILURE, wrapped.__name__)
        except VerifyException as e:
            multiLog(multiLogFile, "################ verify fail ###########################")
            exceptionDeviceId = e.deviceId
            multiLog(multiLogFile, '%s | %s' % (e.deviceId + ' : ' + getDeviceName(e.deviceId).strip(), e.exceptionMsg))
            multiLog(multiLogFile, traceback.format_exc())
            setCaseRunResult(VERIFY_ERROR, wrapped.__name__)
        except HTTPServerError as e:
            multiLog(multiLogFile, "################ HTTPServerError ###########################")
            exceptionDeviceId = e.deviceId
            multiLog(multiLogFile,
                     '%s | HTTPServerError' % (e.deviceId + ' : ' + getDeviceName(e.deviceId).strip(), e.exceptionMsg))
            setCaseRunResult(SERVER_ERROR, wrapped.__name__)
        except NetworkException as e:
            multiLog(multiLogFile, "################ NetworkException ###########################")
            exceptionDeviceId = e.deviceId
            multiLog(multiLogFile, '%s | NetworkException' % (e.deviceId + ' : ' + getDeviceName(e.deviceId).strip()))
            setCaseRunResult(NET_ERROR, wrapped.__name__)
        except ValueChangeError as e:
            multiLog(multiLogFile, "################ ValueChangeError ###########################")
            exceptionDeviceId = e.deviceId
            multiLog(multiLogFile, '%s | ValueChangeError' % (e.deviceId + ' : ' + getDeviceName(e.deviceId).strip()))
            setCaseRunResult(RETURN_ERROR, wrapped.__name__)
        except TimeoutException as e:
            multiLog(multiLogFile, "################ APP_TIMEOUT ###########################")
            exceptionDeviceId = e.deviceId
            multiLog(multiLogFile, '%s | APP_TIMEOUT' % (e.deviceId + ' : ' + getDeviceName(e.deviceId).strip()))
            setCaseRunResult(APP_TIMEOUT, wrapped.__name__)
        except Exception as e:
            multiLog(multiLogFile, "################ NormalException ###########################")
            multiLog(multiLogFile, traceback.format_exc())
            setCaseRunResult(NORMAL_ERROR, wrapped.__name__)
        finally:
            tearDown(wrapped.__name__, errorDevice=exceptionDeviceId)
            multiLog(multiLogFile, '################ end case ################[%s]: %s' % (wrapped.__name__, text))
            multiLog(multiLogFile, '\n\n')
            # multiLogFile.close()
            del caseMultiLog[wrapped.__name__]

    return wrapper


users = []

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
    "testcaseNameCn": "string"  # 测试用例中文名
}

caseId = 0  # 记录case的id


def setUp(userNum, caseName, caseNameCn=''):
    global caseId
    caseId += 1
    params['appid'] = 2003426465
    params['createBy'] = 'zhouyusheng'
    cst_tz = timezone('Asia/Shanghai')
    now = datetime.now().replace(tzinfo=cst_tz)
    params['createTime'] = now.strftime('%Y-%m-%d %H:%M:%S')
    params['testcaseName'] = caseName
    params['testcaseNameCn'] = caseNameCn
    params['id'] = caseId
    deviceInfos = deviceManage.getFreeDeviceInfo(userNum, caseName)
    for deviceInfo in deviceInfos:
        if not params['info']:
            params['info'] += getDeviceName(deviceInfo.deviceId).strip()
        else:
            params['info'] += ',' + getDeviceName(deviceInfo.deviceId).strip()
        driver = Driver(deviceInfo)
        driver.start()
        users.append(driver)
    time.sleep(25)


def getUsers(userNum):
    runCaseName = ''
    stacks = inspect.stack()
    for stack in stacks:
        if 'multiCase' in stack[1] and stack[3].endswith('Test'):
            runCaseName = stack[3]
    assert runCaseName != ''
    return [u for u in users if u.deviceInfo.runCaseName == runCaseName]


def tearDown(caseName, errorDevice=None):
    cst_tz = timezone('Asia/Shanghai')
    now = datetime.now().replace(tzinfo=cst_tz)
    endTime = now.strftime('%Y-%m-%d %H:%M:%S')
    caseUser = [u for u in users if u.deviceInfo.runCaseName == caseName]
    result = caseRunResult[caseName]
    feedbackUrls = []
    if result != PASS:
        params['status'] = 0
        # for user in caseUser:
        #     user.step(FEEDBACK, "反馈日志", {"feedbackData": endTime, "device": user.deviceInfo.deviceId})
        #     # 等待反馈完成
        #     time.sleep(5)
        #     logs = projectConfig.reportUrl + endTime + user.deviceInfo.deviceId
        #     feedbackUrls.append({"phone": getDeviceName(user.deviceInfo.deviceId), "logs": logs})
    else:
        params['status'] = 1

    # todo 每个用例都反馈日志，要去掉
    for user in caseUser:
        user.step(FEEDBACK, "反馈日志", {"feedbackData": endTime, "device": user.deviceInfo.deviceId})
        # 等待反馈完成
        time.sleep(5)
        logs = projectConfig.reportUrl + endTime + user.deviceInfo.deviceId
        feedbackUrls.append({"phone": getDeviceName(user.deviceInfo.deviceId), "logs": logs})

    params['logPath'] = json.dumps(feedbackUrls)

    # 结束测试，关闭日志文件和日志子进程,并从users列表中移除user
    for user in caseUser:
        try:
            if len(caseInfos) == 0:
                user.step(FEEDBACK, "反馈日志", {"feedbackData": feedbackData, "device": user.deviceInfo.deviceId})
                # 等待反馈完成
                time.sleep(5)
            user.step(STOP_C, "结束测试")
        except Exception:
            pass
        user.finish()
        users.remove(user)
    params['endTime'] = endTime
    # 数据上报
    url = projectConfig.resultReportUrl
    result = requests.post(url, json=params)
    print('数据上报结果: %s' % result.json())
    # todo 加个上报log记录


def adbScreencap(user, picTime):
    multiLog(user.multiLogFile, '%s | 开始截图：start adb screencap %s' %
             (user.deviceInfo.deviceId + ' : ' + util1.getDeviceName(user.deviceInfo.deviceId).strip(),
              user.deviceInfo.deviceId + '_' + picTime))
    adbCommand.pic(user.deviceInfo.deviceId, user.deviceInfo.deviceId + '_' + picTime)


def pullScreencap(user, picTime):
    pullCommand = "adb -s %s pull %s %s"
    deviceInfo = user.deviceInfo
    filePath = os.path.join('share', deviceInfo.taskId, deviceInfo.runCaseName)
    pullPicCommand = pullCommand % (
        deviceInfo.deviceId, ('/sdcard/espresso/%s.jpg' % (deviceInfo.deviceId + '_' + picTime)), filePath)
    os.popen(pullPicCommand).read()
