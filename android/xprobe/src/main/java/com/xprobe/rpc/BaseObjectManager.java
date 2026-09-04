package com.xprobe.rpc;

import java.util.List;

/**
 * 对象管理器：由业务方实现，为反射调用提供对象缓存与自定义类型构造能力
 */
public abstract class BaseObjectManager {

    /**
     * 按类名取缓存的对象实例（通常为被测 SDK 的已初始化对象）
     *
     * @param className 类名（命令中最后一个 "." 之前的部分）
     * @return 缓存实例，找不到时抛异常或返回 null
     */
    public abstract Object getObject(String className);

    /**
     * 被测 SDK 是否已完成初始化
     */
    public abstract boolean isInitialize();

    /**
     * 根据构造参数，创建自定义类型对象
     *
     * @param typeName   类型名
     * @param paramList  构造参数（按顺序），来自命令中 param_value 的数组形式
     * @return 自定义类型实例
     */
    public abstract Object generateCustomType(String typeName, List<String> paramList);

    /**
     * 获取 SDK 包名：命令中的类型 / 类名不含包名时，默认拼接该包名
     *
     * @return SDK 包名
     */
    public abstract String getSDKPackageName();

    /**
     * 字符串转为基本数据类型（含基础类型数组）
     *
     * @param value     字符串形式参数值
     * @param typeClass 目标类型
     * @return 转换后的参数值
     */
    public Object generateBaseType(String value, Class<?> typeClass) {
        if (value.equals("None") || value.equals("null")) {
            return null;
        }
        if (typeClass == String.class || typeClass == Object.class) {
            return value;
        } else if (typeClass == int.class || typeClass == Integer.class) {
            return Integer.parseInt(value);
        } else if (typeClass == byte.class || typeClass == Byte.class) {
            return Byte.parseByte(value);
        } else if (typeClass == long.class || typeClass == Long.class) {
            return Long.parseLong(value);
        } else if (typeClass == double.class || typeClass == Double.class) {
            return Double.parseDouble(value);
        } else if (typeClass == float.class || typeClass == Float.class) {
            return Float.parseFloat(value);
        } else if (typeClass == boolean.class || typeClass == Boolean.class) {
            return Boolean.parseBoolean(value);
        } else if (typeClass == char.class || typeClass == Character.class) {
            return (value).toCharArray()[0];
        } else if (typeClass == int[].class || typeClass == Integer[].class) {
            String[] values = value.split(",");
            int[] results = new int[values.length];
            for (int i = 0; i < values.length; i++) {
                results[i] = Integer.parseInt(values[i].trim());
            }
            return results;
        } else if (typeClass == long[].class || typeClass == Long[].class) {
            String[] values = value.split(",");
            long[] results = new long[values.length];
            for (int i = 0; i < values.length; i++) {
                results[i] = Long.parseLong(values[i].trim());
            }
            return results;
        } else if (typeClass == double[].class || typeClass == Double[].class) {
            String[] values = value.split(",");
            double[] results = new double[values.length];
            for (int i = 0; i < values.length; i++) {
                results[i] = Double.parseDouble(values[i].trim());
            }
            return results;
        } else if (typeClass == float[].class || typeClass == Float[].class) {
            String[] values = value.split(",");
            float[] results = new float[values.length];
            for (int i = 0; i < values.length; i++) {
                results[i] = Float.parseFloat(values[i].trim());
            }
            return results;
        } else if (typeClass == boolean[].class || typeClass == Boolean[].class) {
            String[] values = value.split(",");
            boolean[] results = new boolean[values.length];
            for (int i = 0; i < values.length; i++) {
                results[i] = Boolean.parseBoolean(values[i].trim());
            }
            return results;
        } else if (typeClass == byte[].class || typeClass == Byte[].class) {
            return value.getBytes();
        }
        return value;
    }
}
