package com.xprobe.rpc.protocol;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * 帧编码器：将 CmdMessage 按「4 字节大端长度头 + UTF-8 内容」写入输出流。
 * <p>
 * 长度头取内容 UTF-8 字节数（不含头本身），与解码端 CmdDecoder 保持一致。
 */
public final class CmdEncoder {

    private CmdEncoder() {
    }

    /**
     * 编码并写出一帧，写完后立即 flush
     */
    public static void encode(CmdMessage message, OutputStream out) throws IOException {
        String content = message.getContent() == null ? "" : message.getContent();
        byte[] body = content.getBytes(StandardCharsets.UTF_8);
        // 写入 4 字节大端长度头
        out.write((body.length >>> 24) & 0xFF);
        out.write((body.length >>> 16) & 0xFF);
        out.write((body.length >>> 8) & 0xFF);
        out.write(body.length & 0xFF);
        // 写入消息主体
        out.write(body);
        out.flush();
        message.setContentLength(body.length);
    }
}
