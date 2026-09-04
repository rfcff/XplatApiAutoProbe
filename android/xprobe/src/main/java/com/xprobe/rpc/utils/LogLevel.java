package com.xprobe.rpc.utils;

/**
 * 日志级别常量（三端统一对齐）：0=VERBOSE、1=DEBUG、2=INFO、3=WARN、4=ERROR
 */
public final class LogLevel {

    public static final int VERBOSE = 0;
    public static final int DEBUG = 1;
    public static final int INFO = 2;
    public static final int WARN = 3;
    public static final int ERROR = 4;

    private LogLevel() {
        // 常量类，禁止实例化
    }
}
