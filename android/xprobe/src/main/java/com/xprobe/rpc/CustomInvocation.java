package com.xprobe.rpc;

import java.util.Map;

/**
 * 自定义调用接口：业务方注册后，ver=2 的命令交由业务方自行解析并分发
 */
public interface CustomInvocation {

    /**
     * @param api    方法名（命令中的 apiName）
     * @param params 命令中的 params 对象原样透传，可能为 null
     */
    void callMethod(String api, Map<String, Object> params);
}
