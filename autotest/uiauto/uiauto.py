# -*- coding: UTF-8 -*-
import os
import subprocess
import sys
import threading
import time
from importlib import reload

from uiautomator import Device

import espressoControl
import projectConfig

reload(sys)  # reload 才能调用 setdefaultencoding 方法


devices = {
    "OPPO": ["QKOJFEPN99999999", "7LFMU4QCCIBUZLCE", "NZMZ7P5P5DFQAENN", "CIF689JBOZT4L79D"],
    "HUAWEI": ["DLQ0215C14023970", "P4M7N15917000262", "EJLDU16716025892", "PBV7N16419016161", 'PBV0216615001113',
               '5LM0216114005932', 'DU2SSE15C7051173', "WTKGK17214000041", "RJC5T17B07000557", "8BN0217A27004706",
               'M9N7N15315002694', 'TWGDU16820001308'],
    "XIAOMI": ["558b3e63", "61152c5b", "50686737", "975cbeac", "B679CMCQLZB6TCRS", "d0ed6633", "14dc2c34", "85256580",
               "d0ed6633"],
    "VIVO": ["GQ9L4SYLN7FEMZZ9", "bbcaef17", "d7c8e886", "25964aff", '193a708f', "e0b0c88f"],
    "SAMSUNG": ["0815f81867d63504", "04157df43d92780a", "b7439bb1", "ee7c534d", '3343da60', '0715f7b4f8730c34',
                '6d407e2b'],
    "HTC": ["FA54AYJ05331"],
    "MEIZU": ["98AKBNN22H38"],
    "OTHER": ["85GBBMD22743"],
}


class UiAuto(threading.Thread):
    def __init__(self, devId, port):
        threading.Thread.__init__(self)
        self.deviceId = devId
        self.stopFlag = False
        self.alive = False
        print("%-20s" % self.deviceId, "__init__ ", port)
        self.d = Device(devId, local_port=port, adb_server_host='127.0.0.1')
        '''初始化时开始注册事件'''
        try:
            self.registerWatcher()
        except Exception as e1:
            print("%-20s" % self.deviceId, "registerWatcher Exception:", e1)

    def stop(self):
        print("%-20s" % self.deviceId, "call stop")
        self.stopFlag = True
        try:
            self.d.server.stop()
        except Exception as e:
            print("%-20s" % self.deviceId, "UiAuto stop Exception", e)
        os.popen("adb -s %s shell am force-stop %s" % (self.deviceId, espressoControl.uiautomatorPackage + ".test"))
        os.popen("adb -s %s shell am force-stop %s" % (self.deviceId, espressoControl.uiautomatorPackage))
        inprocess = False
        try:
            out = subprocess.check_output(["adb", "-s", self.deviceId, "shell", "ps"]).decode(
                "utf-8").strip().splitlines()
            if out:
                index = out[0].split().index("PID")
                for line in out[1:]:
                    if line.strip().endswith("com.github.uiautomator") and len(line.split()) > index:
                        inprocess = True
                        subprocess.check_output(
                            ["adb", "-s", self.deviceId, "shell", "kill", "-9", line.split()[index]])
        except:
            pass
        if not inprocess:
            print("%-20s" % self.deviceId, "no com.github.uiautomator in processes, may be force-stop success!")

    def watcherTriggered(self, wtName):
        devId = self.d.server.adb.default_serial
        if self.d.watcher(wtName).triggered:
            print("%-20s" % self.deviceId, wtName, "on ", devId, " is triggered")

    def registerWatcher(self):
        # 所有设备都需要监控的watcher
        self.d.watcher("huawei_authority_allow").when(textContains="始终允许").click(text="始终允许")
        self.d.watcher("common_install").when(text="安装").click(text="安装")

        devId = self.d.server.adb.default_serial
        if devId in devices["OPPO"]:
            print("%-20s" % self.deviceId, "register oppo watchers ")
            self.d.watcher("oppo_no_more_remind").when(text="不再提醒").click(text="不再提醒")
            self.d.watcher("oppo_safeTip_yes").when(text="安全提醒").click(text="同意并继续")
            self.d.watcher("oppo_safeok_yes").when(text="安装完成").click(text="完成")
            self.d.watcher("oppo_installnewversion_yes").when(text="继续安装旧版本").click(text="继续安装旧版本")
            self.d.watcher("oppo_still_install_yes").when(text="继续安装").click(text="继续安装")
            self.d.watcher("oppo_install_replace").when(textContains="替换当前版本").click(text="替换")
            self.d.watcher("oppo_allow_install").when(textContains="静默安装拦截").click(text="允许")
            self.d.watcher("oppo_delete").when(textContains="删除卸载残留").click(text="删除")
        elif devId in devices["HUAWEI"]:
            print("%-20s" % self.deviceId, "register huawei watchers ")
            # self.d.watcher("huawei_install_delete_w1").when(text="暂不删除").click(text="立即删除")
            self.d.watcher("huawei_install_delete_w2").when(textContains="暂不删除").click(text="立即删除")
            self.d.watcher("huawei_member_permit_w2").when(text="记住我的选择").click(text="允许")
            self.d.watcher("huawei_install_continue_w13").when(text="我已充分了解该风险，继续安装").click(text="我已充分了解该风险，继续安装")
            self.d.watcher("huawei_install_continue_w14").when(text="该应用版本较低，无法体验最新功能，建议安装官方最新版本。").click(text="继续安装")
            self.d.watcher("huawei_install_continue_meta8").when(text="风险提示").click(text="继续安装")
            self.d.watcher("huawei_install_allow").when(text="读取已安装应用列表").click(text="允许")
            self.d.watcher("huawei_install_allow_1").when(textContains="您可以在“手机管家”>“权限管理”中配置权限。").click(text="始终允许")
        elif devId in devices["VIVO"]:
            print("%-20s" % self.deviceId, "register vivo watchers ")
            self.d.watcher("vivo_secure_ok_w14").when(text="安全警告").click(text="好")
            self.d.watcher("vivo_install_yes_w1").when(text="来源于非官方商店的应用可能对您的手机和数据安全造成威胁，您要安装此应用吗？").click(text="安装")
            self.d.watcher("vivo_secure_yes_w2").when(text="权限请求").click(text="允许")
            self.d.watcher("vivo_usb_debug_yes_w1").when(text="允许USB调试吗？").click(text="确定")
        elif devId in devices["XIAOMI"]:
            print("%-20s" % self.deviceId, "register xiaomi watchers ")
            self.d.watcher("mi4_install_continue_w2").when(textContains="继续安装").click(text="继续安装")
            self.d.watcher("mi4_lanjie_permit_w1").when(text="静默安装拦截").click(text="允许")
            self.d.watcher("mi4_locate_permit").when(text="安全警告").click(text="允许")
            self.d.watcher("mi_uninstall_rubbish").when(textContains="残留垃圾").click(text="忽略")
            self.d.watcher("mi_secure_yes_w1").when(textContains="正在尝试").click(text="允许")
        elif devId in devices["SAMSUNG"]:
            print("%-20s" % self.deviceId, "register samsung watchers ")
            self.d.watcher("samsung_attention_sure_w1").when(text="注意").click(text="确定")
            self.d.watcher("samsung_admin_sure_w2").when(text="应用程序权限管理").click(text="确认")
            self.d.watcher("samsung_admin_sure_w3").when(textContains="使用所有其他权限").click(text="允许")
            self.d.watcher("samsung_admin_sure_w4").when(textContains="访问您设备上的照片、媒体内容和文件吗").click(text="允许")
            self.d.watcher("samsung_admin_sure_w5").when(textContains="使用此设备的位置信息吗").click(text="允许")
            self.d.watcher("samsung_admin_sure_w6").when(textContains="使用所有其他权限").click(text="总是允许")
            self.d.watcher("samsung_admin_sure_w7").when(textContains="访问您设备上的照片、媒体内容和文件吗").click(text="总是允许")
            self.d.watcher("samsung_admin_sure_w8").when(textContains="使用此设备的位置信息吗").click(text="总是允许")
            self.d.watcher("samsung_admin_sure_w9").when(textContains="应用程序许可").click(text="确认")
        elif devId in devices["HTC"]:
            print("%-20s" % self.deviceId, "register HTC watchers ")
            self.d.watcher("HTC_appOp_sure_w1").when(text="应用程序操作").click(text="确定")
        elif devId in devices["MEIZU"]:
            print("%-20s" % self.deviceId, "register MEIZU watchers ")
            self.d.watcher("MEIZU_allow_install").when(text="正在通过USB自动安装以下应用").click(text="允许")
            self.d.watcher("MEIZU_allow_locate").when(textContains="申请获取定位权限").click(text="允许")
        elif devId in devices["OTHER"]:
            print("%-20s" % self.deviceId, "register other watchers ")
            self.d.watcher("mi4meizu_memeber_permit_w0").when(resourceId="flyme:id/event_remember").click(
                resourceId="flyme:id/accept")
        else:
            print("%-20s" % self.deviceId, "no other special watcher ")

        self.watches = self.d.watchers
        print("%-20s" % self.deviceId, 'register watcher end!')

    def run(self):

        # self.d.server.restart = True
        # self.event.wait()
        while self.stopFlag == False:
            # print "uiauto"
            # if self.d.server.alive:      
            # 每次变化到alive状态，则重新注册watcher
            try:
                # if (self.d.server.restart == True):
                #     self.d.server.restart = False
                #     self.registerWatcher()
                self.d.watchers.run()

                # self.d(text='Clock', className='android.widget.TextView').exists
                if self.watches.triggered:
                    # print 'self.watches.triggered'
                    map(self.watcherTriggered, self.watches)
                    self.watches.reset()
            except Exception as e:
                print("JsonRPCError:", e)
            time.sleep(0.5)


def setSystemAuthority(uiauto):
    uiauto.d.press.home()
    if uiauto.d(text="手机管家").exists:
        uiauto.d(text="手机管家").click()
        uiauto.d(text="权限隐私").click()
        uiauto.d(text="应用权限管理").click()
        uiauto.d(text="YY").click()
        if uiauto.d(className='android.widget.Switch', checked='false').exists:
            uiauto.d(className='android.widget.Switch', checked='false').click()
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()


def setSystemAuthorityByVIVO(uiauto):
    print('设置vivo手机权限')
    uiauto.d.press.home()
    if uiauto.d(text="设置").exists:
        uiauto.d(text="设置").click()
        time.sleep(1)
        uiauto.d.swipe(538, 1642, 538, 500)
        time.sleep(1)
        uiauto.d(text="更多设置").click()
        uiauto.d(text="权限管理").click()
        uiauto.d(text="YY").click()
        time.sleep(1)
        uiauto.d.swipe(526, 1463, 526, 200)
        uiauto.d(text="使用摄像头").click()
        uiauto.d(text="允许").click()
        uiauto.d(text="录音").click()
        uiauto.d(text="允许").click()
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()


def setXiaoMiSystemAuthority(uiauto):
    print('设置红米手机权限')
    try:
        uiauto.d.press.home()
        if uiauto.d(text="安全中心").exists:
            uiauto.d(text="安全中心").click()
        uiauto.d(text='授权管理').click()
        uiauto.d(text='应用权限管理').click()
        time.sleep(1)
        uiauto.d.swipe(570, 1800, 570, 370)
        time.sleep(1)
        uiauto.d(text="YY").click()
        time.sleep(1)
        uiauto.d.swipe(570, 1200, 564, 370)
        time.sleep(1)
        uiauto.d(text="相机").click()
        uiauto.d(text="允许").click()
        time.sleep(1)
        uiauto.d(text="录音").click()
        uiauto.d(text="允许").click()
    except Exception as e:
        print('setXiaoMiSystemAuthority error:' + e)
    finally:
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()


def setSystemAuthorityByVIVOX7(uiauto):
    print('设置vivo x7手机权限')
    try:
        uiauto.d.press.home()
        if uiauto.d(text="i 管家").exists:
            uiauto.d(text="i 管家").click()
            time.sleep(1)
            if uiauto.d(text="发现新版本").exists:
                uiauto.d(text="以后再说").click()
            uiauto.d(text="软件管理").click()
            uiauto.d(text="软件权限管理").click()
            uiauto.d(text="软件").click()
            time.sleep(1)
            uiauto.d.swipe(570, 1500, 564, 370)
            uiauto.d(text=projectConfig.clientAppLabel).click()
            time.sleep(1)
            uiauto.d.swipe(570, 1000, 564, 370)
            time.sleep(1)
            uiauto.d(text="拍照").click()
            uiauto.d(text="允许").click()
            time.sleep(1)
            uiauto.d(text="摄像").click()
            uiauto.d(text="允许").click()
            time.sleep(1)
            uiauto.d(text="录音").click()
            uiauto.d(text="允许").click()
    except Exception as e:
        print('setSystemAuthorityByVIVOX7 error:' + e)
    finally:
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()
        uiauto.d.press.back()


if __name__ == "__main__":

    t = UiAuto('b842b256', 9193)
    t.setDaemon(True)
    t.start()
    time.sleep(20)
    # t.stop()

    while 1:
        time.sleep(10)
        pass
