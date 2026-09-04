package com.xprobe.rpc.metadata;

/**
 * 服务端回给客户端的消息类型
 */
public enum SrvMsgType {

    RETURN_TYPE("return"), // 函数返回值
    CALLBACK_TYPE("callback"), // 回调信息
    ERROR_TYPE("error"); // 异常信息

    private final String mTag;

    SrvMsgType(String tag) {
        this.mTag = tag;
    }

    public String getTAG() {
        return mTag;
    }
}
