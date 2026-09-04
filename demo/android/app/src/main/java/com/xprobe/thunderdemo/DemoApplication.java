package com.xprobe.thunderdemo;

import android.app.Application;
import android.util.Log;

import com.xprobe.rpc.AutoTestRpcServer;
import com.xprobe.rpc.utils.LogUtils;
import com.xprobe.thunderdemo.rpc.DemoInvocation;
import com.xprobe.thunderdemo.rpc.DemoObjectManager;

/**
 * Application：启动统一 RPC 探针服务（XplatApiAutoProbe）
 * <p>
 * 同时注册反射调用方案（DemoObjectManager）与自定义调用方案（DemoInvocation），
 * 测试端（Python 客户端等）可通过 TCP 9000 端口远程驱动本 demo 的全部功能。
 */
public class DemoApplication extends Application {

    private static final String TAG = "XprobeDemo";

    @Override
    public void onCreate() {
        super.onCreate();

        // 注册日志回调：SDK 内部日志统一走回调输出（业务侧自定义输出方式的示例）
        AutoTestRpcServer.setLogCallback(new LogUtils.LogCallback() {
            private static final String[] NAMES = {"V", "D", "I", "W", "E"};

            @Override
            public void onLog(int level, String tag, String message) {
                String name = level >= 0 && level < NAMES.length ? NAMES[level] : String.valueOf(level);
                Log.println(level, TAG, "[" + name + "][" + tag + "] " + message);
            }
        });

        // 启动 RPC 服务：反射 + 自定义调用双方案
        AutoTestRpcServer.start(this, new DemoObjectManager(), new DemoInvocation(this));
        Log.i(TAG, "RPC server started on port 9000");
    }
}
