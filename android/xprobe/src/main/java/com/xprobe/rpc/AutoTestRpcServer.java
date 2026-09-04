package com.xprobe.rpc;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.os.Build;

import com.xprobe.rpc.utils.LogUtils;
import com.xprobe.rpc.utils.ObjectHelper;

import java.util.Map;

/**
 * 统一测试 RPC 服务入口（门面）。
 * <p>
 * 通过前台 Service {@link AutoTestService} 保活，内部由
 * {@link AutoServer}（ServerSocket 实现，默认端口 9000）提供 TCP 服务。
 * <p>
 * 回包接口提供两套等价命名：
 * <ul>
 * <li>协议统一命名：{@link #sendReturn} / {@link #sendError} / {@link #sendCallback} /
 * {@link #sendCallbackJson}；</li>
 * <li>等价命名：{@link ChannelCache#sendMethodReturn} / {@link ChannelCache#sendErrorInfo} /
 * {@link ChannelCache#sendCallbackMsg} / {@link ChannelCache#sendCallbackJson}。</li>
 * </ul>
 */
public final class AutoTestRpcServer {

    // 当前监听端口（start 指定，缺省 9000）
    private static volatile int sPort = AutoServer.DEFAULT_PORT;
    // 运行中的 server 实例
    private static volatile AutoServer sServer;
    // 启动时使用的 application context（stop 时停 Service 用）
    private static volatile Context sAppContext;
    // 是否放行非 debuggable（Release）构建启动；默认拒绝，见 allowInReleaseBuild
    private static volatile boolean sAllowInReleaseBuild = false;

    private AutoTestRpcServer() {
    }

    // ==================== 生命周期 ====================

    /**
     * 放行在非 debuggable（Release）构建中启动服务。
     * <p>
     * 默认拒绝：Release 包调用任意 {@code start} 重载都会直接返回并记错误日志。
     * 启动后的服务在监听端口上不对调用方做身份校验，且命令帧可反射调用本进程内
     * 任意类的任意方法（含 {@code java.lang.Runtime.exec}），等同于向网络可达范围
     * 开放本进程内的任意方法调用。仅在内测包等受控场景下、明确接受该后果后才可开启。
     * <p>
     * 必须在调用 {@code start} 之前调用，否则本次启动仍按拒绝处理。
     *
     * @param allow true 放行 Release 构建；false 恢复默认拒绝
     */
    public static void allowInReleaseBuild(boolean allow) {
        sAllowInReleaseBuild = allow;
    }

    /**
     * 启动 rpc 服务（仅反射调用方案）
     */
    public static void start(Context context, BaseObjectManager manager) {
        ObjectHelper.getInstance().addObjectManager(manager);
        startService(context);
    }

    /**
     * 启动 rpc 服务（仅自定义调用方案）
     */
    public static void start(Context context, CustomInvocation custInvoc) {
        ObjectHelper.getInstance().addCustomInvocation(custInvoc);
        startService(context);
    }

    /**
     * 启动 rpc 服务（反射调用 + 自定义调用）
     */
    public static void start(Context context, BaseObjectManager manager,
                             CustomInvocation custInvoc) {
        ObjectHelper.getInstance().addObjectManager(manager);
        ObjectHelper.getInstance().addCustomInvocation(custInvoc);
        startService(context);
    }

    /**
     * 启动 rpc 服务（指定监听端口）
     */
    public static void start(Context context, int port, BaseObjectManager manager,
                             CustomInvocation custInvoc) {
        sPort = port;
        ObjectHelper.getInstance().addObjectManager(manager);
        ObjectHelper.getInstance().addCustomInvocation(custInvoc);
        startService(context);
    }

    /**
     * 停止 rpc 服务：关闭 server 与全部连接，并停止前台 Service
     */
    public static void stop() {
        stopServerQuietly();
        Context context = sAppContext;
        if (context != null) {
            context.stopService(new Intent(context, AutoTestService.class));
        }
        LogUtils.i("rpc server stopped");
    }

    // ==================== 日志回调 ====================

    /**
     * 注册日志回调（便捷透传，内部转 {@link LogUtils}）：
     * 注册后可接收 SDK 内部日志并自行输出；传 null 恢复默认 logcat 输出
     *
     * @param cb 日志回调，见 {@link LogUtils.LogCallback}
     */
    public static void setLogCallback(LogUtils.LogCallback cb) {
        LogUtils.setLogCallback(cb);
    }

    // ==================== 协议统一命名的回包接口 ====================

    /**
     * 发送方法返回值给客户端（发往最近活跃连接）
     *
     * @param apiName 方法名
     * @param result  返回值文本
     */
    public static void sendReturn(String apiName, String result) {
        ChannelCache.sendMethodReturn(apiName, result);
    }

    /**
     * 发送异常信息给客户端
     *
     * @param key     方法名或 parseError
     * @param message 异常描述 / 堆栈
     */
    public static void sendError(String key, String message) {
        ChannelCache.sendErrorInfo(key, message);
    }

    /**
     * 发送回调信息给客户端
     *
     * @param callbackName 回调名称
     * @param message      回调内容文本
     */
    public static void sendCallback(String callbackName, String message) {
        ChannelCache.sendCallbackMsg(callbackName, message);
    }

    /**
     * 发送回调 json 信息给客户端
     *
     * @param callbackName 回调名称
     * @param json         回调内容键值对
     */
    public static void sendCallbackJson(String callbackName, Map<String, Object> json) {
        ChannelCache.sendCallbackJson(callbackName, json);
    }

    // ==================== 内部方法（供 AutoTestService 使用） ====================

    private static void startService(Context context) {
        if (context == null) {
            LogUtils.e("context is null, ignore start");
            return;
        }
        Context appContext = context.getApplicationContext();
        if (!isStartAllowed(appContext)) {
            return;
        }
        sAppContext = appContext;
        Intent intent = new Intent(sAppContext, AutoTestService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            sAppContext.startForegroundService(intent);
        } else {
            sAppContext.startService(intent);
        }
    }

    /**
     * 是否允许启动：非 debuggable 构建需先经 {@link #allowInReleaseBuild} 显式放行。
     * <p>
     * {@link AutoTestService#onCreate} 也走这里——Service 默认 START_STICKY，进程被杀后
     * 系统会重建 Service 并直接建 server，只校验 start() 会被这条路径绕过。
     */
    static boolean isStartAllowed(Context context) {
        if (isStartAllowed(context.getApplicationInfo().flags, sAllowInReleaseBuild)) {
            return true;
        }
        LogUtils.e("拒绝在非 debuggable 构建中启动 xprobe 服务：监听端口无身份校验，"
                + "可反射调用本进程内任意方法。确为受控内测场景时，"
                + "请先调用 AutoTestRpcServer.allowInReleaseBuild(true)");
        return false;
    }

    /**
     * 门禁判定：显式放行，或构建本身 debuggable。
     * 与 Android 类解耦，便于脱离 Instrumentation 验证。
     */
    static boolean isStartAllowed(int appFlags, boolean allowInRelease) {
        return allowInRelease || (appFlags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
    }

    /**
     * 回填运行中的 server 实例
     */
    static void holdServer(AutoServer server) {
        sServer = server;
    }

    /**
     * 获取当前 server 实例（可能为 null）
     */
    static AutoServer getServer() {
        return sServer;
    }

    /**
     * 获取监听端口
     */
    static int getPort() {
        return sPort;
    }

    /**
     * 仅关闭 server 与连接（不触碰 Service），stop() 与 Service 销毁时共用
     */
    static void stopServerQuietly() {
        AutoServer server = sServer;
        sServer = null;
        if (server != null) {
            server.shutdown();
        } else {
            ChannelCache.clear();
        }
    }
}
