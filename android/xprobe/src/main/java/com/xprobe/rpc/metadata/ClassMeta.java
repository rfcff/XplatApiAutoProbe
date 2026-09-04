package com.xprobe.rpc.metadata;

import java.lang.reflect.Constructor;

/**
 * 类型元数据：类型与其构造器的映射关系
 */
public class ClassMeta {
    private final Class<?> mClass;
    private final Constructor<?> mConstructor;

    public ClassMeta(Class<?> clazz, Constructor<?> constructor) {
        this.mClass = clazz;
        this.mConstructor = constructor;
    }

    public Class<?> getmClass() {
        return mClass;
    }

    public Constructor<?> getmConstructor() {
        return mConstructor;
    }

    @Override
    public String toString() {
        return "ClassMeta{" +
                "mClass=" + mClass +
                ", mConstructor=" + mConstructor +
                '}';
    }
}
