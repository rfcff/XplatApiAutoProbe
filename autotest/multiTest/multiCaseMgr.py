# encoding=utf8


import importlib
import inspect
import os
from datetime import datetime
from functools import cmp_to_key

import projectConfig
from utils import util1

caseSubffix = [ 'openAndCloseMaiTest', 'chatWithFriendImTest']


imCaseSubfixt = 'ImTest'
imCaseInfos = []        # IM相关case
caseInfos = []         # 连麦相关case
caseInfoCopys = []

#反馈日志内容（时间戳）
feedbackData = datetime.now().strftime("%H:%M:%S")

caseRunDeviceDict = {}  # {caseName:[deviceId]}


# case相关信息：用例名，模块名，测试手机数，测试方法，是否在运行，重试次数
class CaseInfo():
    def __init__(self, caseName, caseClass, caseUserNum, caseMethoObj):
        self.caseName = caseName
        self.caseClass = caseClass
        self.caseUserNum = caseUserNum
        self.caseMethodObj = caseMethoObj
        self.caseRetryTime = projectConfig.lianMaiRetryTime

def initTestMethod(taskId):
    global imCaseInfos, caseInfos, caseInfoCopys,caseRunDeviceDict
    del imCaseInfos[:]
    del caseInfos[:]
    del caseInfoCopys[:]
    caseRunDeviceDict.clear()

    # 获取所有的测试用例
    topModule = 'multiTest.multiCase'
    modulelist = []
    dir = os.path.join(os.getcwd(), topModule.replace('.', os.sep))
    for f in os.listdir(dir):
        if os.path.isdir(os.path.join(dir, f)):
            modulelist.extend(['.'.join((topModule, f, x.replace('.py', ''))) for x in os.listdir(os.path.join(dir, f))
                               if x.endswith('py') and 'init' not in x and 'SDK' in x])
    for module in modulelist:
        getTestMethod(module)
    caseInfos.sort(key=cmp_to_key(lambda x, y: x.caseUserNum - y.caseUserNum), reverse=True)
    caseInfoCopys.extend(caseInfos)
    caseInfoCopys.extend(imCaseInfos)

    # 记录测试用例的信息到文件:share/tastId/caseNames.txt
    logCaseInfo(taskId)

def getTestMethod(module):
    classModule = importlib.import_module(module)
    for name, obj in classModule.__dict__.items():
        if projectConfig.isDebugMode:
            # 本地调试用（只跑在caseSubffix里面的用例）
            if name in caseSubffix and inspect.isfunction(obj) and 'userNum' in inspect.getargspec(obj).args:
                caseUserNum = inspect.getargspec(obj).defaults[0]
                case = CaseInfo(name, module[module.rindex('.') + 1:], caseUserNum, obj)
                if name.endswith(imCaseSubfixt):
                    imCaseInfos.append(case)
                else:
                    caseInfos.append(case)
        else:
            if name.endswith('Test') and inspect.isfunction(obj) and 'userNum' in inspect.getargspec(obj).args:
                moduleName = module[module.rindex('.') + 1:]
                caseUserNum = inspect.getargspec(obj).defaults[0]
                case = CaseInfo(name, moduleName, caseUserNum, obj)
                if name.endswith(imCaseSubfixt):
                    imCaseInfos.append(case)
                else:
                    caseInfos.append(case)

def logCaseInfo(taskId):
    caseNameLog = open(util1.getLogPath(taskId, "caseNames.txt"), 'w')
    for caseInfo in caseInfoCopys:
        caseNameLog.write(caseInfo.caseClass + '.' + caseInfo.caseName + '\n')
    caseNameLog.close()

def getCaseTotalNum():
    return len(caseInfoCopys)


def getCase():
    # 先跑IM，im需要账号配对
    if len(imCaseInfos) > 0:
        return imCaseInfos.pop()
    if len(caseInfos) > 0:
        return caseInfos.pop()
    return None


def putBackCase(caseInfo):
    if caseInfo.caseName.endswith(imCaseSubfixt):
        imCaseInfos.insert(0, caseInfo)
    else:
        caseInfos.insert(0, caseInfo)



def recordCaseRunDevice(caseName,runDevice):
    if not caseName in caseRunDeviceDict.keys():
        caseRunDeviceDict[caseName] = []
    for deviceInfo in runDevice:
        caseRunDeviceDict[caseName].append(deviceInfo.deviceId)

