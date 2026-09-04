package com.xprobe.rpc.protocol;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * 帧解码器：从输入流中按「4 字节大端长度头 + N 字节 UTF-8 内容」手工切帧。
 * <p>
 * TCP 是字节流，存在粘包/半包，处理方式：
 * <ol>
 * <li>循环读取：每次 read 读到任意长度的字节块，先追加到缓冲区尾部；</li>
 * <li>循环切帧：只要缓冲区中攒够一帧（头 + 体），就取出一条消息，直到剩余数据不足一帧；</li>
 * <li>buffer 前移：一轮切帧结束后，把剩余不完整数据整体前移到缓冲区头部，等待后续数据补齐。</li>
 * </ol>
 * 每个连接独享一个 CmdDecoder 实例（内部持有缓冲区状态）。
 */
public class CmdDecoder {

    // 帧头长度（字节）
    public static final int HEADER_LENGTH = 4;
    // 单帧内容最大长度（32MB），超过视为非法帧，避免非法长度头导致 OOM。
    // 32MB 为 PROTOCOL.md §2 规定的跨端统一值，各平台必须一致，不得单独调整。
    private static final int MAX_FRAME_LENGTH = 32 * 1024 * 1024;
    // 缓冲区初始大小
    private static final int INIT_BUFFER_SIZE = 8 * 1024;

    private byte[] mBuffer = new byte[INIT_BUFFER_SIZE];
    // 缓冲区中当前有效数据长度
    private int mSize = 0;

    /**
     * 阻塞读取一次数据，并解析出缓冲区内所有完整帧。
     * <ul>
     * <li>返回列表可能为空（本次数据不足以凑成一帧，等待下次调用补齐）；</li>
     * <li>一次读到多条帧数据（粘包）时，全部切出；</li>
     * <li>对端关闭连接时抛出 IOException，由调用方负责关闭连接。</li>
     * </ul>
     */
    public List<CmdMessage> readFrames(InputStream in) throws IOException {
        // 1. 循环读取：保证缓冲区还有空间可读
        if (mSize == mBuffer.length) {
            growBuffer();
        }
        int read = in.read(mBuffer, mSize, mBuffer.length - mSize);
        if (read < 0) {
            // 对端关闭
            throw new IOException("end of stream");
        }
        mSize += read;

        // 2. 循环切帧：提取缓冲区内所有完整帧
        List<CmdMessage> frames = new ArrayList<>();
        int offset = 0;
        while (mSize - offset >= HEADER_LENGTH) {
            int contentLength = readInt(mBuffer, offset);
            if (contentLength < 0 || contentLength > MAX_FRAME_LENGTH) {
                throw new IOException("invalid frame length: " + contentLength);
            }
            if (mSize - offset - HEADER_LENGTH < contentLength) {
                // 半包：剩余数据不足一帧，等待下次读取补齐
                break;
            }
            String content = new String(mBuffer, offset + HEADER_LENGTH, contentLength,
                    StandardCharsets.UTF_8);
            frames.add(new CmdMessage(contentLength, content));
            offset += HEADER_LENGTH + contentLength;
        }

        // 3. buffer 前移：把未消费的数据移动到缓冲区头部
        if (offset > 0) {
            System.arraycopy(mBuffer, offset, mBuffer, 0, mSize - offset);
            mSize -= offset;
        }

        // 缓冲区已满但仍凑不出完整帧（半包且超长），扩容后等待下次读取
        if (mSize == mBuffer.length) {
            growBuffer();
        }
        return frames;
    }

    /**
     * 读取缓冲区指定位置起的 4 字节大端整数
     */
    private static int readInt(byte[] buf, int offset) {
        return ((buf[offset] & 0xFF) << 24)
                | ((buf[offset + 1] & 0xFF) << 16)
                | ((buf[offset + 2] & 0xFF) << 8)
                | (buf[offset + 3] & 0xFF);
    }

    /**
     * 缓冲区扩容（翻倍，上限为最大帧长 + 帧头）
     */
    private void growBuffer() throws IOException {
        if (mBuffer.length >= MAX_FRAME_LENGTH + HEADER_LENGTH) {
            throw new IOException("frame exceeds max buffer size: " + mSize);
        }
        int newLength = Math.min(mBuffer.length * 2, MAX_FRAME_LENGTH + HEADER_LENGTH);
        byte[] newBuffer = new byte[newLength];
        System.arraycopy(mBuffer, 0, newBuffer, 0, mSize);
        mBuffer = newBuffer;
    }
}
