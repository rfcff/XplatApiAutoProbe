package com.xprobe.rpc.utils;

import android.util.Log;

/**
 * 日志工具：统一打点格式「类名:行号」前缀，tag 默认 TestRunner。
 * <p>
 * 支持注册日志回调 {@link LogCallback}：注册后全部日志改走回调分发（同步调用），
 * 未注册或注册 null 时保持默认 android.util.Log 输出，格式不变。
 */
public class LogUtils {

    public static final String TEST_TAG = "TestRunner";
    private static final int DEFAULT_LEVEL = 2;
    private static final int PARENT_LEVEL = 3;

    // 日志回调（volatile 保证多线程注册 / 读取的可见性；null 表示走默认 logcat 输出）
    private static volatile LogCallback sCallback;

    /**
     * 日志回调接口：业务与测试用例实现后可接收 SDK 内部日志并自行输出
     */
    public interface LogCallback {

        /**
         * 日志分发回调（在打点线程同步执行，请不要在回调中做耗时操作）
         *
         * @param level   日志级别，取值见 {@link LogLevel}
         * @param tag     日志 tag
         * @param message 日志内容（已包含「类名:行号」前缀）
         */
        void onLog(int level, String tag, String message);
    }

    /**
     * 注册日志回调：注册后全部日志改走回调；传 null 恢复默认 logcat 输出
     */
    public static void setLogCallback(LogCallback cb) {
        sCallback = cb;
    }

    // 默认日志为 line 级别
    public static void v(String msg) {
        output(LogLevel.VERBOSE, TEST_TAG, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void v(String tag, String msg) {
        output(LogLevel.VERBOSE, tag, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void d(String msg) {
        output(LogLevel.DEBUG, TEST_TAG, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void d(String tag, String msg) {
        output(LogLevel.DEBUG, tag, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void i(String msg) {
        output(LogLevel.INFO, TEST_TAG, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void i(String tag, String msg) {
        output(LogLevel.INFO, tag, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void w(String msg) {
        output(LogLevel.WARN, TEST_TAG, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void w(String tag, String msg) {
        output(LogLevel.WARN, tag, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void e(String msg) {
        output(LogLevel.ERROR, TEST_TAG, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    public static void e(String tag, String msg) {
        output(LogLevel.ERROR, tag, "[" + getClassAndLine(DEFAULT_LEVEL) + "] " + msg);
    }

    // 日志打印在 parent 的 line 级别
    public static int pv(String msg) {
        return output(LogLevel.VERBOSE, getLineTag(PARENT_LEVEL), msg);
    }

    public static int pd(String msg) {
        return output(LogLevel.DEBUG, getLineTag(PARENT_LEVEL), msg);
    }

    public static int pi(String msg) {
        return output(LogLevel.INFO, getLineTag(PARENT_LEVEL), msg);
    }

    public static int pw(String msg) {
        return output(LogLevel.WARN, getLineTag(PARENT_LEVEL), msg);
    }

    public static int pe(String msg) {
        return output(LogLevel.ERROR, getLineTag(PARENT_LEVEL), msg);
    }

    /**
     * 统一分发出口：注册回调时在当前线程同步调用回调（回调可容忍耗时操作，
     * 但请业务方避免）；未注册时保持 android.util.Log 默认输出，格式不变
     */
    private static int output(int level, String tag, String msg) {
        LogCallback callback = sCallback;
        if (callback != null) {
            callback.onLog(level, tag, msg);
            return 0;
        }
        switch (level) {
            case LogLevel.VERBOSE:
                return Log.v(tag, msg);
            case LogLevel.DEBUG:
                return Log.d(tag, msg);
            case LogLevel.WARN:
                return Log.w(tag, msg);
            case LogLevel.ERROR:
                return Log.e(tag, msg);
            case LogLevel.INFO:
            default:
                return Log.i(tag, msg);
        }
    }

    private static String getLineTag(int parentLevel) {
        return TEST_TAG + getClassAndLine(parentLevel);
    }

    private static String getClassAndLine(int parentLevel) {
        StackTraceElement ste = new Throwable().getStackTrace()[parentLevel];
        String fullName = ste.getClassName();
        String simpleName = fullName.substring(fullName.lastIndexOf('.') + 1);
        return simpleName + ":" + ste.getLineNumber();
    }
}
