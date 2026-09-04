package com.xprobe.rpc.utils;

import com.xprobe.rpc.BaseObjectManager;
import com.xprobe.rpc.CustomInvocation;

/**
 * 单例持有者：保存业务方注册的 BaseObjectManager（反射调用）与 CustomInvocation（自定义调用）
 */
public class ObjectHelper {

    private static volatile ObjectHelper sInstance;

    private volatile BaseObjectManager mManager;
    private volatile CustomInvocation mInvoc;

    private ObjectHelper() {
    }

    public static ObjectHelper getInstance() {
        if (sInstance == null) {
            synchronized (ObjectHelper.class) {
                if (sInstance == null) {
                    sInstance = new ObjectHelper();
                }
            }
        }
        return sInstance;
    }

    /**
     * 注册对象管理器
     */
    public void addObjectManager(BaseObjectManager cache) {
        this.mManager = cache;
    }

    /**
     * 注册自定义调用实现
     */
    public void addCustomInvocation(CustomInvocation invoc) {
        this.mInvoc = invoc;
    }

    /**
     * 获取对象管理器（可能为 null）
     */
    public BaseObjectManager getObjecManager() {
        return mManager;
    }

    /**
     * 获取自定义调用实现（可能为 null）
     */
    public CustomInvocation getCustomInvocation() {
        return mInvoc;
    }
}
