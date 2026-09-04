package com.xprobe.rpc;

import com.xprobe.rpc.metadata.SrvMsgType;
import com.xprobe.rpc.protocol.CmdMessage;
import com.xprobe.rpc.utils.ExceptionUtil;
import com.xprobe.rpc.utils.LogUtils;

import org.json.JSONException;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 连接缓存：记录当前全部客户端连接（socket + 输出流抽象）。
 * <p>
 * sendMethodReturn / sendErrorInfo / sendCallbackMsg / sendCallbackJson
 * 默认发往「最近活跃」的连接（最近一次收到数据的连接）。
 */
public class ChannelCache {

    private static final ConcurrentHashMap<String, TcpChannel> sMap = new ConcurrentHashMap<>();

    public static void add(TcpChannel channel) {
        if (channel != null) {
            sMap.put(channel.getKey(), channel);
        }
    }

    public static TcpChannel get(String key) {
        return sMap.get(key);
    }

    public static void remove(TcpChannel channel) {
        if (channel != null) {
            sMap.remove(channel.getKey());
        }
    }

    public static void remove(String key) {
        sMap.remove(key);
    }

    /**
     * 关闭并清空全部连接
     */
    public static void clear() {
        for (TcpChannel channel : sMap.values()) {
            channel.close();
        }
        sMap.clear();
    }

    /**
     * 连接收到数据时调用，刷新其活跃时间
     */
    public static void markActive(TcpChannel channel) {
        if (channel != null) {
            channel.markActive();
        }
    }

    /**
     * 挑选最近活跃且仍然可用的连接，顺带清理失效连接
     */
    private static TcpChannel latestActiveChannel() {
        TcpChannel target = null;
        for (TcpChannel channel : sMap.values()) {
            if (!channel.isActive()) {
                sMap.remove(channel.getKey());
                continue;
            }
            if (target == null || channel.getLastActiveTime() > target.getLastActiveTime()) {
                target = channel;
            }
        }
        return target;
    }

    /**
     * 发送信息给客户端（发往最近活跃连接）
     */
    public static void sendMsg(String msg) {
        TcpChannel channel = latestActiveChannel();
        if (channel == null) {
            LogUtils.e("no active channel, drop msg: " + msg);
            return;
        }
        byte[] body = msg.getBytes(StandardCharsets.UTF_8);
        channel.send(new CmdMessage(body.length, msg));
    }

    /**
     * 发送方法的返回值给客户端
     *
     * @param methodName 方法名
     * @param result     返回值
     */
    public static void sendMethodReturn(String methodName, String result) {
        // 关键路径打点：发送返回帧
        LogUtils.i("send method return: " + methodName);
        try {
            sendMsg(generateJsonString(SrvMsgType.RETURN_TYPE, methodName, result));
        } catch (JSONException e) {
            sendErrorInfo(methodName, ExceptionUtil.getShortExceptionStack(e));
        }
    }

    /**
     * 发送异常信息给客户端
     *
     * @param name   方法名，或类名（修改成员变量时）
     * @param errMsg 异常信息
     */
    public static void sendErrorInfo(String name, String errMsg) {
        // 关键路径打点：发送错误返回帧
        LogUtils.i("send error info: " + name);
        try {
            sendMsg(generateJsonString(SrvMsgType.ERROR_TYPE, name, errMsg));
        } catch (JSONException e) {
            // 极端情况下 JSON 拼装失败，直接拼接文本兜底
            String msg = String.format("{\"type\": %s, \"key\": %s,\"value\": %s}",
                    SrvMsgType.ERROR_TYPE.getTAG(), name, errMsg);
            sendMsg(msg);
        }
    }

    /**
     * 发送回调信息给客户端
     *
     * @param callbackName 回调函数名称
     * @param msg          回调信息
     */
    public static void sendCallbackMsg(String callbackName, String msg) {
        // 关键路径打点：发送回调帧
        LogUtils.i("send callback msg: " + callbackName);
        try {
            sendMsg(generateJsonString(SrvMsgType.CALLBACK_TYPE, callbackName, msg));
        } catch (JSONException e) {
            e.printStackTrace();
            sendErrorInfo(callbackName, ExceptionUtil.getShortExceptionStack(e));
        }
    }

    /**
     * 发送回调 json 信息给客户端
     *
     * @param callbackName 回调函数名称
     * @param map          回调的 json 信息
     */
    public static void sendCallbackJson(String callbackName, Map<String, Object> map) {
        // 关键路径打点：发送回调 json 帧
        LogUtils.i("send callback json: " + callbackName);
        try {
            sendMsg(generateJsonString(SrvMsgType.CALLBACK_TYPE, callbackName,
                    new JSONObject(map).toString()));
        } catch (JSONException e) {
            e.printStackTrace();
            sendErrorInfo(callbackName, ExceptionUtil.getShortExceptionStack(e));
        }
    }

    /**
     * 组装统一格式的回包 JSON：{"type":..,"key":..,"value":..}
     */
    private static String generateJsonString(SrvMsgType type, String key, String value)
            throws JSONException {
        JSONObject jsonObject = new JSONObject();
        jsonObject.put("type", type.getTAG());
        jsonObject.put("key", key);
        jsonObject.put("value", value);
        return jsonObject.toString();
    }
}
