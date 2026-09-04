package com.xprobe.rpc.metadata;

/**
 * 命令类型：修改成员变量（field）或调用方法（api）
 */
public enum CmdType {
    FILED_TYPE("field"), // 成员变量
    METHOD_TYPE("api"); // 成员方法

    private final String mTag;

    CmdType(String tag) {
        this.mTag = tag;
    }

    public String getTAG() {
        return mTag;
    }
}
