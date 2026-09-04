# encoding=UTF-8
import json
import string
import traceback
import urllib.parse
import urllib.request

import projectConfig
from projectConfig import SERVER_ADDRESS
from projectConfig import TEST_SUIT
from projectConfig import enum
from utils.util1 import log
from utils.util1 import openUrl

keyConfig = 'config'
keyTaskType = 'taskType'
keyTaskId = 'taskId'
keyAppUrl = 'appUrl'
keyEmails = 'emails'
keyPriority = 'priority'
keyMobileNum = 'mobilenum'
keyTestsuit = 'testplan'
keyPlatform = 'platform'
keyOnlySendToMe = 'onlySendToMe'
keyDecomposable = 'decomposable'  # 任务拆分 1拆分，0不可拆分

# 升级任务特有的参数
keyStartVersion = 'start_version'
keyTargetVersion = 'target_version'

# monkey测试特有的参数
keyPackageName = 'package_name'
keyEventInterval = 'event_interval'
keyEventCount = 'event_count'
keyOmitCrash = 'omit_crash'
keyOmitOverTime = 'omit_overtime'
keyLogLevel = 'log_level'
keyOtherParams = 'other_params'
keySeed = 'seed'

TAG = lambda name: '[%s]' % name
testId = ['137806']
lianMaiPlatform = 'Android-LianMai'
normalPlatform = 'Android'

task_type_update = "EntMobileUpdateTest"
task_type_autotest = "EntMobileTest"
task_type_soda_autotest = "SodaVideoTest"
task_type_data_report = "EntMobileDataUploadTest"
task_type_monkeytest = "MonkeyTest"

TaskState = enum(
    Ready=0,
    Running=1,
    Finish=2,
    Complete=3,
    Cancel=4,
)


# 获取自动化测试任务，覆盖安装任务，手Y数据上报的任务
def get_tasks():
    tasks = []
    tasks.extend(get_ready_tasks(task_type_autotest))
    tasks.extend(get_ready_tasks(task_type_soda_autotest))
    tasks.extend(get_ready_tasks(task_type_update))
    tasks.extend(get_ready_tasks(task_type_data_report))
    return tasks


# 获取monkey测试的任务
def get_monkey_test_task():
    tasks = []
    tasks.extend(get_ready_tasks(task_type_monkeytest))
    return tasks


def get_ready_tasks(tasktype='EntMobileTest', platform=[normalPlatform, lianMaiPlatform]):
    url = SERVER_ADDRESS + 'api/task/getReadyTasks?task_type=' + tasktype
    taskidlist = []
    tasks = None
    try:
        content = openUrl(url)
        if content:
            tasks = json.loads(content)
        else:
            print("content is None")
    except Exception as e:
        print("conten:%s=" % content)
        traceback.print_exc()
    if tasks:
        for task in tasks:
            if task['task_config'] != None and task['task_config']['platform'] in platform:
                buildPath = ''
                if task.has_key('build_path'):
                    buildPath = task['build_path'].strip()
                    if buildPath.startswith('\\'):
                        buildPath = 'http:' + buildPath.replace('\\', '/')
                taskidlist.append({keyTaskType: task['task_type'],
                                   keyTaskId: task['id'],
                                   keyAppUrl: buildPath,
                                   keyEmails: task['emails'].split(';'),
                                   keyPriority: task[keyPriority],
                                   keyDecomposable: task[keyDecomposable],
                                   keyTestsuit: task[keyTestsuit],
                                   keyMobileNum: task['task_config']['mobilenum'] if task['task_config'].has_key(
                                       'mobilenum') else '',
                                   keyPlatform: task['task_config']['platform'] if task['task_config'].has_key(
                                       'platform') else '',
                                   keyOnlySendToMe: task['task_config']['onlySendToMe'] if task['task_config'].has_key(
                                       'onlySendToMe') else 'false',
                                   keyStartVersion: task['task_config'][
                                       keyStartVersion].strip() if tasktype == task_type_update else '',
                                   keyTargetVersion: task['task_config'][
                                       keyTargetVersion].strip() if tasktype == task_type_update else '',
                                   keyPackageName: task['task_config'][keyPackageName] if task['task_config'].has_key(
                                       keyPackageName) else '',
                                   keyEventInterval: task['task_config'][keyEventInterval] if task[
                                       'task_config'].has_key(keyEventInterval) else 0,
                                   keyEventCount: task['task_config'][keyEventCount] if task['task_config'].has_key(
                                       keyEventCount) else 0,
                                   keyOmitCrash: task['task_config'][keyOmitCrash] if task['task_config'].has_key(
                                       keyOmitCrash) else 'false',
                                   keyOmitOverTime: task['task_config'][keyOmitOverTime] if task['task_config'].has_key(
                                       keyOmitOverTime) else 'false',
                                   keyLogLevel: task['task_config'][keyLogLevel] if task['task_config'].has_key(
                                       keyLogLevel) else '',
                                   keyOtherParams: task['task_config'][keyOtherParams] if task['task_config'].has_key(
                                       keyOtherParams) else '',
                                   keySeed: task['task_config'][keySeed] if task['task_config'].has_key(keySeed) else ''
                                   })
    return taskidlist


# for test
isGenerated = False


def generateDebugTasks():
    global isGenerated
    if isGenerated:
        return

    url = projectConfig.appBuildUrl

    taskidlist = []

    taskidlist.append({keyTaskType: task_type_autotest,
                       keyTaskId: '2021',
                       keyAppUrl: url,
                       keyEmails: projectConfig.mailto,
                       keyMobileNum: 'All',
                       keyPriority: '0',
                       keyDecomposable: '1',
                       keyOnlySendToMe: 'true',
                       keyTestsuit: TEST_SUIT.BVT_P0_SUIT,
                       keyPlatform: "Android"})

    isGenerated = True
    return taskidlist


def execute_task(logfile, taskid):
    url = SERVER_ADDRESS + 'api/task/execute?id=' + str(taskid)
    content = openUrl(url, logfile)
    if content and b'success' in content:
        log(logfile, 'execute task success', TAG(taskid))
        print('execute task success %s:' % TAG(taskid))
        return True
    # log(logfile, ('execute task failed %s:' % TAG(taskid) + content))
    log(logfile, ('execute task failed %s:' % TAG(taskid)))
    print('execute task failed %s:' % TAG(taskid))
    return False


def kill_task(logfile, taskid):
    url = SERVER_ADDRESS + 'api/task/kill?id=' + str(taskid)
    content = openUrl(url, logfile)
    if content and b'success' in content:
        log(logfile, 'kill task success', TAG(taskid))
        return True
    log(logfile, ('kill task failed %s' % TAG(taskid) + content.decode()))
    return False


def complete_task(logfile, taskid, total, passnum, failnum, blocknum, result_dic):
    url = SERVER_ADDRESS + 'api/task/complete?id=' + str(taskid)
    result_str = json.dumps(result_dic)
    url = url + (
            '&total=%d&passed=%d&failed=%d&blocked=%d&results=%s' % (total, passnum, failnum, blocknum, result_str))
    content = openUrl(url, logfile)
    if content and b'success' in content:
        log(logfile, 'complete task success', TAG(taskid))
        return True
    log(logfile, ('complete task failed %s' % TAG(taskid) + content.decode()))
    return False


def update_progress(logfile, taskId, progress):
    log(logfile, '[%s]update progress: %s' % (taskId, progress))
    url = SERVER_ADDRESS + ('api/task/upgrade?id=%s&percentage=%d' % (str(taskId), progress))
    result = openUrl(url, logfile)
    if result and b'success' in result:
        return True
    log(logfile, ('update task failed %s:' % TAG(taskId) + result.decode()))
    return False


def update_error_list(logfile, taskId, failCaseInfos):
    baseUrl = SERVER_ADDRESS + 'api/taskcase/newCase'
    successCount = 0
    for caseName, caseDetail in failCaseInfos.items():
        postData = {}
        caseDesc = caseDetail['description']
        picUrl = caseDetail['picUrl'] if caseDetail['picUrl'] else ''
        logUrl = caseDetail['logUrl'] if caseDetail['logUrl'] else ''
        postData['id'] = taskId
        postData['case_id'] = caseName
        postData['case_name'] = caseDesc
        postData['result'] = '0'
        logData = ''
        if logUrl:
            localLogPath = logUrl[logUrl.index('share'):len(logUrl)]
            for line in open(localLogPath).readlines():
                if line.endswith('\n'):
                    logData += line.replace('\n', '\n\r')
                elif line.endswith('\r'):
                    logData += line.replace('\r', '\n\r')
        postData['log_info'] = logData
        postData['screenshots'] = '["%s"]' % picUrl
        postData = urllib.parse.urlencode(postData)
        req = urllib.request.Request(url=baseUrl, data=postData)
        result = openUrl(req)
        if 'success' in result:
            successCount += 1
    if successCount == 0 or len(failCaseInfos) != successCount:
        log(logfile, 'update error list failed,check taskid:%s' % taskId)


# test plan,p0:1,p1:2,BVT:4,BVT+P0:5
def createTask(decomposable, buildPath, revision, buildNumber, args="platform=Android&mobilenum=All",
               tasktype="EntMobileTest", product="entmobile-android", testplan=5):
    baseUrl = SERVER_ADDRESS + 'api/task/newTask2?'
    params = "task_type=%s&decomposable=%s&testplan=%d&emails=&ips=&product=%s&build_path=%s&revision=%s&build_number=%s&install_type=0&%s"
    url = baseUrl + (params % (tasktype, decomposable, testplan, product, buildPath, revision, buildNumber, args))
    isSuccess = False
    try:
        socket = urllib.request.urlopen(url)
        result = socket.read()
        if result and 'success' in result:
            isSuccess = True
    except Exception as e:
        print('createTask Exception', args)
        isSuccess = False
    finally:
        if socket is not None:
            socket.close()
    print('createTask successed and isSuccess = %s  args = %s' % (isSuccess, args))
    return isSuccess


def getTaskById(taskId):
    url = SERVER_ADDRESS + "api/task/getTaskById?id=%s" % str(taskId)
    task = None
    result = openUrl(url)
    if result:
        try:
            task = json.loads(result)
        except Exception as e:
            print('[getTaskById]', str(e))
    return task


# 0：task待执行，1：task执行中，2：task执行完成，待人工确认误报。3：完成，4：无效task。
def getTaskState(taskId):
    if projectConfig.isDebugMode:
        return TaskState.Ready
    task = getTaskById(taskId)
    if task:
        return string.atoi(task['status'])
    return TaskState.Running


def getTaskRevisionBuildNum(taskId):
    task = getTaskById(taskId)
    if task:
        return task['revision'], task['build_number']
    return None


def isTaskCanceled(taskId):
    taskState = getTaskState(taskId)
    if taskState == TaskState.Cancel:
        return True
    else:
        return False


if __name__ == "__main__":

    content = openUrl('http://172.25.43.4/share/138285/85GBBMD22743/mobileLog/logs/logs_2017_08_02_20_31.txt')
    for c in content.split('\n'):
        if 'TestRunner' in c:
            print(c)
    url = SERVER_ADDRESS + 'api/task/getReadyTasks?task_type=' + "PerfTest"
    taskidlist = []
    tasks = None
    try:
        content = openUrl(url)
        if content:
            tasks = json.loads(content)
        else:
            print("content is None")
    except Exception as e:
        print("conten:%s=" % content)
        traceback.print_exc()
    if tasks:
        for task in tasks:
            taskidlist.append(task['id'])
    f = open('kill.txt', 'w+')
    for id in taskidlist:
        kill_task(f, id)
