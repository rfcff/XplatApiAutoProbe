package com.xprobe.rpc;

import com.xprobe.rpc.protocol.CmdEncoder;
import com.xprobe.rpc.protocol.CmdMessage;
import com.xprobe.rpc.utils.LogUtils;

import java.io.BufferedOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Socket;

/**
 * TCP 连接抽象：封装 socket 与其输出流。
 * <ul>
 * <li>写操作内部加锁，保证线程池 / 主线程并发回包时帧不交错；</li>
 * <li>记录最近活跃时间，供 ChannelCache 挑选「最近活跃连接」。</li>
 * </ul>
 */
public class TcpChannel {

    private final Socket mSocket;
    private final OutputStream mOutputStream;
    // 最近一次收到数据的时间戳
    private volatile long mLastActiveTime;

    public TcpChannel(Socket socket) throws IOException {
        this.mSocket = socket;
        this.mOutputStream = new BufferedOutputStream(socket.getOutputStream());
        this.mLastActiveTime = System.currentTimeMillis();
    }

    /**
     * 连接唯一标识（远端地址）
     */
    public String getKey() {
        return String.valueOf(mSocket.getRemoteSocketAddress());
    }

    /**
     * 远端地址描述
     */
    public String getRemoteAddress() {
        return String.valueOf(mSocket.getRemoteSocketAddress());
    }

    /**
     * 连接是否仍然可用
     */
    public boolean isActive() {
        return !mSocket.isClosed() && mSocket.isConnected();
    }

    /**
     * 该连接有数据到达时调用，刷新活跃时间
     */
    public void markActive() {
        mLastActiveTime = System.currentTimeMillis();
    }

    public long getLastActiveTime() {
        return mLastActiveTime;
    }

    /**
     * 获取输入流（仅供该连接的读线程使用）
     */
    public InputStream getInputStream() throws IOException {
        return mSocket.getInputStream();
    }

    /**
     * 向该连接写出一帧（4 字节大端长度头 + UTF-8 内容），内部加锁
     */
    public synchronized void send(CmdMessage msg) {
        try {
            CmdEncoder.encode(msg, mOutputStream);
        } catch (IOException e) {
            LogUtils.e("send msg failed: " + e);
            close();
        }
    }

    /**
     * 关闭连接（幂等）
     */
    public void close() {
        try {
            mSocket.close();
        } catch (IOException e) {
            LogUtils.e("close channel failed: " + e);
        }
    }
}
