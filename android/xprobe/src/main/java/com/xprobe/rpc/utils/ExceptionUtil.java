package com.xprobe.rpc.utils;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.InvocationTargetException;

/**
 * 异常堆栈工具：把 Throwable 转为字符串
 */
public class ExceptionUtil {

    /**
     * 获取完整异常堆栈
     */
    public static String getExceptionStack(Throwable e) {
        StringWriter errorsWriter = new StringWriter();
        unwrap(e).printStackTrace(new PrintWriter(errorsWriter));
        return errorsWriter.toString();
    }

    /**
     * 获取异常堆栈的首行（异常类型与描述）
     */
    public static String getShortExceptionStack(Throwable e) {
        StringWriter errorsWriter = new StringWriter();
        unwrap(e).printStackTrace(new PrintWriter(errorsWriter));
        String msg = errorsWriter.toString();
        int index = msg.indexOf("\n");
        return index >= 0 ? msg.substring(0, index) : msg;
    }

    /**
     * 剥掉反射包装层：Method.invoke 抛出的 InvocationTargetException 只是壳，
     * 被测 SDK 真正抛出的异常在 cause 里。不剥掉的话首行只会显示反射包装类型，
     * 排查时看不到真正的失败原因。
     */
    private static Throwable unwrap(Throwable e) {
        Throwable t = e;
        while (t instanceof InvocationTargetException && t.getCause() != null) {
            t = t.getCause();
        }
        return t;
    }
}
