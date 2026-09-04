package com.xprobe.rpc.exception;

/**
 * 命令执行异常：反射找不到方法 / 类，或 SDK 未初始化等
 */
public class ExeCmdException extends RuntimeException {

    public ExeCmdException(String cmd, String message) {
        super(cmd + " [error] " + message);
    }
}
