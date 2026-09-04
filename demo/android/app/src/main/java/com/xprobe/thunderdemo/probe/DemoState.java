package com.xprobe.thunderdemo.probe;

/**
 * 协议回归用的轻量静态字段目标（无 RTC 依赖）。
 * <p>
 * 供 {@code client/test_client_android.py} 验证 field 命令改写静态成员后，
 * 再经反射 {@link #describe()} 回读。
 */
public final class DemoState {

    public static String uid = "";
    public static String channelId = "";

    private DemoState() {
    }

    /** 回读静态字段，供 ver=1 反射调用 */
    public static String describe() {
        return "uid=" + uid + " channelId=" + channelId;
    }
}
