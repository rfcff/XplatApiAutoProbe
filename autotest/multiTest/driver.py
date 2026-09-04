# encoding=utf8
import subprocess
import time

import requests

import projectConfig
from utils import util1, adbCommand
from utils.util1 import getDeviceName
from .exception import *
from command.sample.multiCommand import *
from command.common import *

url = "http://127.0.0.1:%d/"
STEP_TIME_OUT = 5 * 60

caseMultiLog = {}  # {caseName:multiLog}


class Driver:
    def __init__(self, deviceInfo):
        self.deviceInfo = deviceInfo
        self.taskId = deviceInfo.taskId
        self.deviceId = deviceInfo.deviceId
        self.configInfo = deviceInfo.configInfo
        self.caseName = deviceInfo.runCaseName
        self.port = self.configInfo.getPort()
        self.hostPort = deviceInfo.clientPort
        self.url = url % self.hostPort

        if not self.caseName in caseMultiLog.keys():
            self.multiLogFile = open(util1.getLogPath(self.taskId, 'multiLog.txt', self.caseName), 'a+', encoding="GBK")
            caseMultiLog[self.caseName] = self.multiLogFile
        else:
            self.multiLogFile = caseMultiLog[self.caseName]
        self.totalLogFile = open(util1.getLogPath(self.taskId, 'totalLog.txt'), 'a+', encoding="GBK")

    def start(self):
        # 强制停止app
        adbCommand.forceStopApp(self.deviceId)
        time.sleep(5)  # 等待杀掉应用
        # 清一下系统日志缓存，避免拿到上次跑用例的log
        adbCommand.clearDeviceLogCache(self.deviceId)

        adbForwardCommand = "adb -s %s forward tcp:%d tcp:%d"
        logcatCommand = "adb -s %s shell logcat -v time |grep TestRunner"
        espressoCommand = "adb -s %s shell am instrument -w -r -e debug false -e class test.testcases.multi.MultiTest#multiTerminalTest \
                         %s –no-window-animation"
        # adbforward 映射
        forward = adbForwardCommand % (self.deviceId, self.hostPort, self.port)
        print('-------%s----------' % forward)
        self.adbForwardSuprocess = subprocess.Popen(forward.split(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

        self.logcatFN = open(
            util1.getLogPath(self.taskId, 'logcat_%s_%s.txt' % (self.deviceId, util1.getDeviceName(self.deviceId)),
                             self.caseName), 'a+')
        self.logcatSP = subprocess.Popen((logcatCommand % self.deviceId).split(), stdout=self.logcatFN,
                                         stderr=self.logcatFN, )
        self.espressoFN = open(util1.getLogPath(self.taskId, 'espresso_%s.txt' % (self.deviceId), self.caseName), 'a+')
        self.espressoSP = subprocess.Popen((espressoCommand % (self.deviceId, adbCommand.multiTestRunner)).split(),
                                           stdout=self.espressoFN, stderr=self.espressoFN)

        adbstartCommand = "adb -s %s shell am start -n " + projectConfig.mainActivity
        self.startSP = subprocess.Popen((adbstartCommand % self.deviceId).split(), stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT)
        time.sleep(5)  # 等待拉起应用

        print('%-32s | 推送的账号信息：[AUserName = %s ] [ANickName = %s ] [passWord = %s ] [uid = %s ]' %
              ((self.deviceId + ' : ' + getDeviceName(self.deviceId).strip()),
               self.configInfo.getUserAUserName(), self.configInfo.getUserANickName(),
               self.configInfo.getUserAPassword(), self.configInfo.getUserAUID()))

    '''
    发送命令，接收命令执行完会送的结果
    command 约定的命令
    caseMsg 命令描述
    kwargs 命令对应接口的传参
    '''

    def step(self, command, caseMsg="", kwargs={}):
        print('%-32s | step   [%s] Command [%s] %s' % (
            (self.deviceId + ' : ' + getDeviceName(self.deviceId).strip()), caseMsg, command,
            ('args [%s]' % (str(kwargs)) if kwargs else "")))
        try:
            result = requests.get(self.url, params=self.newCommand(command, kwargs), timeout=STEP_TIME_OUT)
            dataValue = None
            if result.status_code == 200:
                response = result.text.split(SPLIT)
                if len(response) > 1 and response[1] != RETURN_VOID:
                    dataValue = self.changeValue(response[2], response[1])
                    # print("%-32s | command %s with return: \n %s." % (
                    #     (self.deviceId + ' : ' + getDeviceName(self.deviceId).strip()), command, dataValue))
                assert response[0] == STEP_PASS, '%s 命令执行失败, 失败原因: %s' % (command, response[3])
            else:
                assert 0, 'http server error, please retry'
            return dataValue
        except requests.RequestException:
            assert 0, 'http server启动失败，请检查app版本是否带有测试代码'

    def newCommand(self, command, kwargs):
        result = {COMMAND: command}
        if kwargs:
            result.update(kwargs)
        return result

    def changeValue(self, data, returnType):
        try:
            if returnType == RETURN_INT:
                return int(data)
            elif returnType == RETURN_FLOAT or returnType == RETURN_DOUBLE:
                return float(data)
            elif returnType == RETURN_BOOLEAN:
                if data == "true":
                    return True
                elif data == "false":
                    return False
                # multiLog(self.multiLogFile, "[Warning] return data: %s" % str(data))
                # totalLog(self.totalLogFile, "[Warning] return data: %s" % str(data))
                raise ValueChangeError(self.deviceId)
            elif returnType == RETURN_STRING:
                return data
        except ValueError as e:
            # multiLog(self.multiLogFile, "[Warning] return data:")
            # totalLog(self.totalLogFile, "[Warning] return data:")
            raise ValueChangeError(self.deviceId)

    def finish(self):
        # 强制停止app
        adbCommand.forceStopApp(self.deviceId)
        self.logcatFN.flush()
        self.logcatFN.close()
        self.espressoFN.flush()
        self.espressoFN.close()

        self.logcatSP.terminate()
        self.adbForwardSuprocess.terminate()

        time.sleep(2)

    def verifyOnConnectionStatus(self, status):
        self.step(CHECK_ON_CONNECTION_STATUS, kwargs={STATUS: status})

    def verifyOnNetworkTypeChanged(self, type):
        self.step(CHECK_ON_NETWORKTYPE_CHANGED, kwargs={TYPE: type})

    def verifyJoinRoom(self):
        self.step(CHECK_JOIN_ROOM)

    def verifyJoinOtherRoom(self):
        self.step(CHECK_JOIN_OTHER_ROOM)

    def verifyOnUserJoined(self):
        self.step(CHECK_ON_USER_JOINED)

    def verifyOnUserOffline(self):
        self.step(CHECK_ON_USER_Offline)

    def verifyOnRemoteAudioStopped(self, muted):
        self.step(CHECK_ON_REMOTE_AUDIO_STOPPED, kwargs={MUTE: muted})

    def verifyOnRemoteVideoStopped(self, muted):
        self.step(CHECK_ON_REMOTE_VIDEO_STOPPED, kwargs={MUTE: muted})

    def verifyOnRemoteAudioStateChangedOfUid(self, state, reason):
        self.step(CHECK_ON_REMOTE_AUDIO_STATE_CHANGED_OF_UID, kwargs={STATE: state, REASON: reason})

    def verifyOnRemoteAudioPlay(self):
        self.step(CHECK_ON_REMOTE_AUDIO_PLAY)

    def verifyOnRemoteVideoStateChangedOfUid(self, state, reason):
        self.step(CHECK_ON_REMOTE_VIDEO_STATE_CHANGED_OF_UID, kwargs={STATE: state, REASON: reason})

    def verifyOnVideoSizeChanged(self, uid, width, height):
        self.step(CHECK_ON_VIDEO_SIZE_CHANGED, kwargs={UID: uid, WIDTH: width, HEIGHT: height})

    def verifyOnRemoteAudioStatsOfUid(self):
        self.step(CHECK_ON_REMOTE_AUDIO_STATS_OF_UID)

    def verifyOnRemoteVideoStatsOfUid(self):
        self.step(CHECK_ON_REMOTE_VIDEO_STATS_OF_UID)

    def verifyOnAudioCaptureStatus(self, status):
        self.step(CHECK_ON_AUDIO_CAPTURE_STATUS, kwargs={STATUS: status})

    def verifyOnLocalAudioStatusChanged(self, status, errorReason):
        self.step(CHECK_ON_LOCAL_AUDIO_STATUS_CHANGED, kwargs={STATUS: status, ERROR_REASON: errorReason})

    def verifyOnFirstLocalAudioFrameSent(self):
        self.step(CHECK_ON_FIRST_LOCAL_AUDIO_FRAMESENT)

    def verifyOnDeviceStats(self):
        self.step(CHECK_ON_DEVICE_STATS)

    def verifyOnLocalAudioStats(self):
        self.step(CHECK_ON_LOCAL_AUDIO_STATS)

    def verifyOnRoomStats(self):
        self.step(CHECK_ON_ROOM_STATS)

    def verifyOnLeaveRoom(self):
        self.step(CHECK_ON_LEAVE_ROOM)

    def verifyOnLocalVideoStatusChanged(self, status, errorReason):
        self.step(CHECK_ON_LOCAL_VIDEO_STATUS_CHANGED, kwargs={STATUS: status, ERROR_REASON: errorReason})

    def verifyOnVideoCaptureStatus(self, status):
        self.step(CHECK_ON_VIDEO_CAPTURE_STATUS, kwargs={STATUS: status})

    def verifyLocalVideoStats(self):
        self.step(CHECK_LOCAL_VIDEO_STATS)

    def verifyOnFirstLocalVideoFrameSent(self):
        self.step(CHECK_ON_FIRST_LOCAL_VIDEO_FRAMESENT)

    def verifyOnRemoteVideoPlay(self, width, height):
        self.step(CHECK_ON_REMOTE_VIDEOPLAY, kwargs={WIDTH: width, HEIGHT: height})

    def verifyOnAudioRouteChanged(self, routing):
        self.step(CHECK_ON_AUDIO_ROUTE_CHANGED, kwargs={ROUTING: routing})

    def verifyStartAudioLive(self):
        self.verifyJoinRoom()
        self.verifyOnDeviceStats()
        self.verifyOnRoomStats()
        self.verifyOnAudioCaptureStatus(0)
        self.verifyOnLocalAudioStatusChanged(1, 0)
        self.verifyOnLocalAudioStatusChanged(2, 0)
        self.verifyOnFirstLocalAudioFrameSent()
        self.verifyOnLocalAudioStatusChanged(3, 0)
        self.verifyOnLocalAudioStats()

    def verifyOtherStartAudioLive(self):
        self.verifyJoinOtherRoom()
        self.verifyOnDeviceStats()
        self.verifyOnRoomStats()
        self.verifyOnAudioCaptureStatus(0)
        self.verifyOnLocalAudioStatusChanged(1, 0)
        self.verifyOnLocalAudioStatusChanged(2, 0)
        self.verifyOnFirstLocalAudioFrameSent()
        self.verifyOnLocalAudioStatusChanged(3, 0)
        self.verifyOnLocalAudioStats()

    def verifyStartVideoLive(self):
        uid = self.configInfo.getMyUID()
        self.verifyJoinRoom()
        self.verifyOnDeviceStats()
        self.verifyOnRoomStats()
        self.verifyOnLocalVideoStatusChanged(1, 0)
        self.verifyOnLocalVideoStatusChanged(2, 0)
        self.verifyOnVideoCaptureStatus(0)
        self.verifyLocalVideoStats()
        self.verifyOnVideoSizeChanged(uid, 368, 640)
        self.verifyOnLocalVideoStatusChanged(4, 0)
        self.verifyOnFirstLocalVideoFrameSent()
        self.verifyOnLocalVideoStatusChanged(3, 0)

    def verifyOtherStartVideoLive(self):
        uid = self.configInfo.getMyUID()
        self.verifyOnDeviceStats()
        self.verifyOnRoomStats()
        self.verifyOnLocalVideoStatusChanged(1, 0)
        self.verifyOnLocalVideoStatusChanged(2, 0)
        self.verifyOnVideoCaptureStatus(0)
        self.verifyLocalVideoStats()
        self.verifyOnVideoSizeChanged(uid, 368, 640)
        self.verifyOnLocalVideoStatusChanged(4, 0)
        self.verifyOnFirstLocalVideoFrameSent()
        self.verifyOnLocalVideoStatusChanged(3, 0)

    def verifyStartAudioVideoLive(self):
        uid = self.configInfo.getMyUID()
        self.verifyJoinRoom()
        self.verifyOnDeviceStats()
        self.verifyOnRoomStats()
        self.verifyOnAudioCaptureStatus(0)
        self.verifyOnLocalAudioStatusChanged(1, 0)
        self.verifyOnLocalAudioStatusChanged(2, 0)
        self.verifyOnFirstLocalAudioFrameSent()
        self.verifyOnLocalAudioStatusChanged(3, 0)
        self.verifyOnLocalVideoStatusChanged(1, 0)
        self.verifyOnLocalVideoStatusChanged(2, 0)
        self.verifyOnVideoCaptureStatus(0)
        self.verifyLocalVideoStats()
        self.verifyOnVideoSizeChanged(uid, 368, 640)
        self.verifyOnLocalVideoStatusChanged(4, 0)
        self.verifyOnFirstLocalVideoFrameSent()
        self.verifyOnLocalVideoStatusChanged(3, 0)

    def verifyOtherStartAudioVideoLive(self):
        uid = self.configInfo.getMyUID()
        self.verifyJoinOtherRoom()
        self.verifyOnDeviceStats()
        self.verifyOnRoomStats()
        self.verifyOnAudioCaptureStatus(0)
        self.verifyOnLocalAudioStatusChanged(1, 0)
        self.verifyOnLocalAudioStatusChanged(2, 0)
        self.verifyOnFirstLocalAudioFrameSent()
        self.verifyOnLocalAudioStatusChanged(3, 0)
        self.verifyOnLocalVideoStatusChanged(1, 0)
        self.verifyOnLocalVideoStatusChanged(2, 0)
        self.verifyOnVideoCaptureStatus(0)
        self.verifyLocalVideoStats()
        self.verifyOnVideoSizeChanged(uid, 368, 640)
        self.verifyOnLocalVideoStatusChanged(4, 0)
        self.verifyOnFirstLocalVideoFrameSent()
        self.verifyOnLocalVideoStatusChanged(3, 0)

    def verifySubscribeAudio(self):
        self.verifyOnRemoteAudioStopped(False)
        self.verifyOnRemoteAudioStateChangedOfUid(1, 0)
        self.verifyOnRemoteAudioStateChangedOfUid(2, 0)
        self.verifyOnRemoteAudioPlay()

    def verifySubscribeVideo(self):
        uid = self.configInfo.getAnotherUID()
        self.verifyOnRemoteVideoStopped(False)
        self.verifyOnRemoteVideoStateChangedOfUid(1, 0)
        self.verifyOnRemoteVideoStateChangedOfUid(2, 0)
        self.verifyOnVideoSizeChanged(uid, 368, 640)
        self.verifyOnRemoteVideoStatsOfUid()
        self.verifyOnRemoteVideoStateChangedOfUid(3, 0)
        self.verifyOnRemoteVideoPlay(368, 640)

    def verifySubscribeAudioVideo(self):
        uid = self.configInfo.getAnotherUID()
        self.verifyOnRemoteAudioStopped(False)
        self.verifyOnRemoteAudioStateChangedOfUid(1, 0)
        self.verifyOnRemoteAudioStateChangedOfUid(2, 0)
        self.verifyOnRemoteAudioPlay()
        self.verifyOnRemoteAudioStatsOfUid()
        self.verifyOnRemoteVideoStopped(False)
        self.verifyOnRemoteVideoStateChangedOfUid(1, 0)
        self.verifyOnRemoteVideoStateChangedOfUid(2, 0)
        self.verifyOnVideoSizeChanged(uid, 368, 640)
        self.verifyOnRemoteVideoStatsOfUid()
        self.verifyOnRemoteVideoStateChangedOfUid(3, 0)
        self.verifyOnRemoteVideoPlay(368, 640)

    '''
    fileType:播放的类型 0 -> mp3, 1 -> aac, 2 -> m4a, 3 -> mp4, 4 -> 3gp, 5 -> mkv, 6 -> http
    '''
    def verifyOnAudioFileStateChange(self, fileType, event, errorCode):
        self.step(CHECK_ON_AUDIO_FILE_STATE_CHANGE, kwargs={TYPE: fileType, EVENT: event, ERROR_CODE: errorCode})

    '''
    fileType:播放的类型 0 -> mp3, 1 -> aac, 2 -> m4a, 3 -> mp4, 4 -> 3gp, 5 -> mkv, 6 -> http
    '''
    def verifyOnAudioFileStateChangeProgress(self, fileType, event, progress):
        self.step(CHECK_ON_AUDIO_FILE_STATE_CHANGE_PROGRESS, kwargs={TYPE: fileType, EVENT: event, PROGRESS: progress})
