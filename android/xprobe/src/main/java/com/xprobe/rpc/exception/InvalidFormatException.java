package com.xprobe.rpc.exception;

/**
 * 命令格式解析异常：JSON 结构或字段不满足线协议
 */
public class InvalidFormatException extends RuntimeException {

    // 合法命令示例，用于错误回包时提示客户端
    public static final String EXAMPLE_CMD =
            "example: [{field:com.foo.DemoConfig, param_name:[a,b], param_type:[String,int],"
                    + " param_value:[234,567]},"
                    + "{api:com.foo.Bar.add, param_name:[a,b], param_type:[int,int],"
                    + " param_value:[1,2]}]";

    public InvalidFormatException(String cmd, String errInfo) {
        super(cmd + " [format error] " + errInfo);
    }
}
