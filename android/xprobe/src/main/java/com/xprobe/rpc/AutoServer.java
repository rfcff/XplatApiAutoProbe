package com.xprobe.rpc;

import android.content.Context;

import com.xprobe.rpc.protocol.CmdDecoder;
import com.xprobe.rpc.protocol.CmdMessage;
import com.xprobe.rpc.utils.LogUtils;

import java.io.IOException;
import java.io.InputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.List;

/**
 * TCP 服务端：基于 java.net.ServerSocket 实现（无第三方依赖），
 * 监听线程 accept 连接，每个连接由一条独立读线程处理。
 * <p>
 * 帧协议：4 字节大端长度头 + UTF-8 内容，粘包 / 半包由 {@link CmdDecoder} 处理。
 * 默认端口 9000，绑定 0.0.0.0，支持多个客户端同时连接。
 */
public class AutoServer {

    // 默认监听端口
    public static final int DEFAULT_PORT = 9000;
    // accept 队列长度
    private static final int BACKLOG = 128;

    private final ServerSocket mServerSocket;
    private final AutoServerHandler mHandler;
    private final Thread mAcceptThread;
    private volatile boolean mRunning = true;

    public AutoServer(Context ctx) throws IOException {
        this(ctx, DEFAULT_PORT);
    }

    public AutoServer(Context ctx, int port) throws IOException {
        // 绑定 0.0.0.0，允许任意网络接口上的客户端连接
        mServerSocket = new ServerSocket(port, BACKLOG, InetAddress.getByName("0.0.0.0"));
        mHandler = new AutoServerHandler(ctx);
        ChannelCache.clear();
        mAcceptThread = new Thread(new AcceptRunnable(), "xprobe-accept");
        mAcceptThread.start();
        LogUtils.i("rpc server start on port " + port);
    }

    /**
     * 服务是否运行中
     */
    public boolean isRunning() {
        return mRunning && !mServerSocket.isClosed();
    }

    /**
     * 停止服务：关闭监听 socket 与全部客户端连接，并释放处理器线程池（幂等）
     */
    public void shutdown() {
        if (!mRunning) {
            return;
        }
        mRunning = false;
        try {
            mServerSocket.close(); // 解除 accept 阻塞
        } catch (IOException e) {
            LogUtils.e("close server socket failed: " + e);
        }
        ChannelCache.clear(); // 关闭全部客户端连接，读线程随之退出
        mHandler.shutdown(); // 释放线程池与主线程消息
        LogUtils.i("rpc server stopped");
    }

    /**
     * 监听线程：循环 accept 新连接
     */
    private class AcceptRunnable implements Runnable {

        @Override
        public void run() {
            while (mRunning) {
                try {
                    Socket socket = mServerSocket.accept();
                    socket.setTcpNoDelay(true); // 不延迟，直接发送
                    socket.setKeepAlive(true); // 保持长连接状态
                    TcpChannel channel = new TcpChannel(socket);
                    mHandler.onChannelActive(channel);
                    Thread reader = new Thread(new ReadRunnable(channel),
                            "xprobe-reader-" + channel.getRemoteAddress());
                    reader.start();
                } catch (IOException e) {
                    if (mRunning) {
                        LogUtils.e("accept failed: " + e);
                    }
                }
            }
        }
    }

    /**
     * 连接读线程：循环读帧并分发，连接断开时负责清理
     */
    private class ReadRunnable implements Runnable {

        private final TcpChannel mChannel;
        // 每个连接独享一个解码器（内部持有粘包 / 半包缓冲状态）
        private final CmdDecoder mDecoder = new CmdDecoder();

        ReadRunnable(TcpChannel channel) {
            mChannel = channel;
        }

        @Override
        public void run() {
            try {
                InputStream in = mChannel.getInputStream();
                while (mRunning && mChannel.isActive()) {
                    // 阻塞读取并切帧，一次可能返回多条消息（粘包）
                    List<CmdMessage> frames = mDecoder.readFrames(in);
                    for (CmdMessage frame : frames) {
                        // 收到数据即刷新「最近活跃连接」
                        ChannelCache.markActive(mChannel);
                        try {
                            mHandler.handleFrame(mChannel, frame);
                        } catch (Exception e) {
                            mHandler.exceptionCaught(mChannel, e);
                        }
                    }
                }
            } catch (IOException e) {
                // 连接断开或读取失败，走 finally 清理
                LogUtils.i("channel " + mChannel.getRemoteAddress() + " disconnected: " + e);
            } finally {
                mChannel.close();
                mHandler.onChannelInactive(mChannel);
                ChannelCache.remove(mChannel);
            }
        }
    }
}
