package com.xprobe.rpc.protocol;

/**
 * 一帧消息：4 字节长度头之后的文本内容（UTF-8 JSON 或特殊文本）
 */
public class CmdMessage {

    // 消息内容长度（字节）
    private int mContentLength;

    // 消息内容
    private String mContent;

    public CmdMessage(int contentLength, String content) {
        this.mContentLength = contentLength;
        this.mContent = content;
    }

    public int getContentLength() {
        return mContentLength;
    }

    public void setContentLength(int contentLength) {
        this.mContentLength = contentLength;
    }

    public String getContent() {
        return mContent;
    }

    public void setContent(String content) {
        this.mContent = content;
    }

    @Override
    public String toString() {
        return "CmdMessage{" +
                "contentLength=" + mContentLength +
                ", content='" + mContent + '\'' +
                '}';
    }
}
