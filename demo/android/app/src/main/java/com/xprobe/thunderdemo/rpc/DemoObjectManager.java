package com.xprobe.thunderdemo.rpc;

import com.thunder.livesdk.ThunderEngine;
import com.thunder.livesdk.ThunderVideoCanvas;
import com.xprobe.rpc.BaseObjectManager;
import com.xprobe.thunderdemo.rtc.RtcManager;

import java.util.List;

/**
 * RPC 反射调用方案的 ObjectManager：
 * <p>
 * 测试端通过 ver=1 反射命令调用 ThunderEngine 的任意公开方法，
 * 本类负责提供引擎对象缓存、初始化状态与自定义类型构造。
 */
public class DemoObjectManager extends BaseObjectManager {

    public static final String SDK_PACKAGE_NAME = "com.thunder.livesdk";
    public static final String ENGINE_CLASS_NAME = SDK_PACKAGE_NAME + ".ThunderEngine";

    @Override
    public Object getObject(String className) {
        // 默认返回 ThunderEngine；也支持返回封装管理器
        if (RtcManager.class.getName().equals(className)) {
            return RtcManager.getInstance();
        }
        return RtcManager.getInstance().getEngine();
    }

    @Override
    public boolean isInitialize() {
        return RtcManager.getInstance().isInitialized();
    }

    @Override
    public Object generateCustomType(String typeName, List<String> paramList) {
        // ThunderVideoCanvas: [canvasType, uid] canvasType 0=本地预览 1=远端播放
        // 视图由 RtcManager 在主线程按需创建（thunder 视图依赖引擎已加载的 native 库）
        if (typeName.endsWith("ThunderVideoCanvas")) {
            int canvasType = Integer.parseInt(paramList.get(0));
            String uid = paramList.size() > 1 ? paramList.get(1) : "";
            RtcManager rtc = RtcManager.getInstance();
            if (canvasType == 0) {
                return new ThunderVideoCanvas(rtc.ensureLocalView(), 1, uid);
            } else {
                return new ThunderVideoCanvas(rtc.ensureRemoteView(), 1, uid);
            }
        }
        return null;
    }

    @Override
    public String getSDKPackageName() {
        return SDK_PACKAGE_NAME;
    }
}
