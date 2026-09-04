package com.xprobe.rpc;

import android.os.Build;

import com.xprobe.rpc.protocol.CmdMessage;
import com.xprobe.rpc.utils.Api2Protocol;
import com.xprobe.rpc.utils.LogUtils;

import java.nio.charset.StandardCharsets;

/**
 * 帧分发基类：处理 PING / PONG / GET_API 三类特殊文本帧，其余交给子类。
 * <p>
 * 心跳由客户端发送 PING、服务端回复 PONG 完成保活，服务端不做空闲检测。
 */
public abstract class BaseHandler {

    public static final String PING = "PING";
    public static final String PONG = "PONG";
    public static final String GET_API = "GET_API"; // 格式：GET_API:类名

    /**
     * 连接建立：登记到 ChannelCache
     */
    public void onChannelActive(TcpChannel channel) {
        LogUtils.i("channel:" + channel.getRemoteAddress() + " is active");
        ChannelCache.add(channel);
    }

    /**
     * 连接断开：从 ChannelCache 移除
     */
    public void onChannelInactive(TcpChannel channel) {
        LogUtils.i("channel:" + channel.getRemoteAddress() + " is inactive");
        ChannelCache.remove(channel);
    }

    /**
     * 收到一帧：按内容分发（PING / PONG / GET_API / 业务命令）
     */
    public final void handleFrame(TcpChannel channel, CmdMessage msg) {
        String content = msg.getContent();
        if (content == null) {
            return;
        }
        if (PING.equals(content)) {
            sendPong(channel);
        } else if (PONG.equals(content)) {
            LogUtils.i("get pong msg from " + channel.getRemoteAddress());
        } else if (content.startsWith(GET_API)) {
            handleGetApi(channel, content);
        } else {
            handleData(channel, msg);
        }
    }

    /**
     * 处理 GET_API:类名 —— 反射枚举该类公开方法并回包
     */
    private void handleGetApi(TcpChannel channel, String content) {
        String className = content.substring(content.indexOf(":") + 1);
        String apiList;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            // Method.getParameters() 需要 API 26
            try {
                apiList = Api2Protocol.generateProtocol(className);
            } catch (Exception e) {
                // 类名为空等异常情况：返回空数组，避免断开连接
                LogUtils.e("generate api list failed: " + e);
                apiList = "[]";
            }
        } else {
            // 低版本不支持参数名反射，返回空数组
            apiList = "[]";
        }
        sendText(channel, apiList);
    }

    /**
     * 子类实现：处理业务命令帧
     */
    protected abstract void handleData(TcpChannel channel, CmdMessage msg);

    /**
     * 连接处理异常：打印并关闭连接
     */
    public void exceptionCaught(TcpChannel channel, Throwable cause) {
        // 关键路径打点：帧处理异常兜底（含命令解析异常）
        LogUtils.e("channel:" + channel.getRemoteAddress() + " caught exception: " + cause);
        cause.printStackTrace();
        channel.close();
    }

    protected void sendPong(TcpChannel channel) {
        sendText(channel, PONG);
        LogUtils.i("send pong message to " + channel.getRemoteAddress());
    }

    protected void sendPing(TcpChannel channel) {
        sendText(channel, PING);
        LogUtils.i("send ping message to " + channel.getRemoteAddress());
    }

    /**
     * 按 UTF-8 字节数构造帧头并发送，保证长度头与实际内容一致
     */
    private static void sendText(TcpChannel channel, String text) {
        byte[] body = text.getBytes(StandardCharsets.UTF_8);
        channel.send(new CmdMessage(body.length, text));
    }
}
