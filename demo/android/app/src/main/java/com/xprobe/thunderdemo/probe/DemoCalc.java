package com.xprobe.thunderdemo.probe;

/**
 * 协议回归用的轻量反射目标（无 RTC 依赖）。
 * <p>
 * 供 {@code client/test_client_android.py} 验证 ver=1 静态方法调用与 GET_API 枚举。
 */
public final class DemoCalc {

    private DemoCalc() {
    }

    /** 两整数相加，供反射调用：{@code com.xprobe.thunderdemo.probe.DemoCalc.add} */
    public static int add(int left, int right) {
        return left + right;
    }
}
