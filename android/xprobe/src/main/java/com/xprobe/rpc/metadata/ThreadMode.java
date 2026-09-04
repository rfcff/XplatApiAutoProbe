package com.xprobe.rpc.metadata;

/**
 * 命令执行的线程模式。
 * <p>
 * 线协议约定：0 或缺省 = BACKGROUND（工作线程池执行），1 = MAIN（平台主线程执行）。
 */
public enum ThreadMode {
    BACKGROUND(0), // 子线程：固定大小线程池
    MAIN(1); // 主线程

    private final int mTag;

    ThreadMode(int tag) {
        this.mTag = tag;
    }

    public int getTAG() {
        return mTag;
    }

    /**
     * 按线协议数值解析线程模式，非法值一律回落到 BACKGROUND
     */
    public static ThreadMode fromTag(int tag) {
        for (ThreadMode mode : values()) {
            if (mode.mTag == tag) {
                return mode;
            }
        }
        return BACKGROUND;
    }
}
