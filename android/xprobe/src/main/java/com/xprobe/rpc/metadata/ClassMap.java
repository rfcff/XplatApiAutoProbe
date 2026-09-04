package com.xprobe.rpc.metadata;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * 类型注册表：基础数据类型、容器类型与自定义类型的统一映射
 */
public class ClassMap {

    private static final Map<String, ClassMeta> sBaseClassMap = new HashMap<>(); // 基础数据类型
    private static final Map<String, ClassMeta> sCollectionClassMap = new HashMap<>(); // 容器数据类型
    private static final Map<String, ClassMeta> sCustomClassMap = new HashMap<>(); // 自定义类型

    // 基础数据类型
    static {
        sBaseClassMap.put("int", new ClassMeta(int.class, null));
        sBaseClassMap.put("Integer", new ClassMeta(Integer.class, null));
        sBaseClassMap.put("int[]", new ClassMeta(int[].class, null));
        sBaseClassMap.put("Integer[]", new ClassMeta(Integer[].class, null));

        sBaseClassMap.put("long", new ClassMeta(long.class, null));
        sBaseClassMap.put("Long", new ClassMeta(Long.class, null));
        sBaseClassMap.put("long[]", new ClassMeta(long[].class, null));
        sBaseClassMap.put("Long[]", new ClassMeta(Long[].class, null));

        sBaseClassMap.put("double", new ClassMeta(double.class, null));
        sBaseClassMap.put("Double", new ClassMeta(Double.class, null));
        sBaseClassMap.put("double[]", new ClassMeta(double[].class, null));
        sBaseClassMap.put("Double[]", new ClassMeta(Double[].class, null));

        sBaseClassMap.put("float", new ClassMeta(float.class, null));
        sBaseClassMap.put("Float", new ClassMeta(Float.class, null));
        sBaseClassMap.put("float[]", new ClassMeta(float[].class, null));
        sBaseClassMap.put("Float[]", new ClassMeta(Float[].class, null));

        sBaseClassMap.put("boolean", new ClassMeta(boolean.class, null));
        sBaseClassMap.put("Boolean", new ClassMeta(Boolean.class, null));
        sBaseClassMap.put("boolean[]", new ClassMeta(boolean[].class, null));
        sBaseClassMap.put("Boolean[]", new ClassMeta(Boolean[].class, null));

        sBaseClassMap.put("string", new ClassMeta(String.class, null)); // 兼容小写写法
        sBaseClassMap.put("String", new ClassMeta(String.class, null));
        sBaseClassMap.put("string[]", new ClassMeta(String[].class, null)); // 兼容小写写法
        sBaseClassMap.put("String[]", new ClassMeta(String[].class, null));

        sBaseClassMap.put("char", new ClassMeta(char.class, null));
        sBaseClassMap.put("Character", new ClassMeta(Character.class, null));
        sBaseClassMap.put("char[]", new ClassMeta(char[].class, null));
        sBaseClassMap.put("Character[]", new ClassMeta(Character[].class, null));

        sBaseClassMap.put("byte", new ClassMeta(byte.class, null));
        sBaseClassMap.put("Byte", new ClassMeta(Byte.class, null));
        sBaseClassMap.put("byte[]", new ClassMeta(byte[].class, null));
        sBaseClassMap.put("Byte[]", new ClassMeta(Byte[].class, null));

        sBaseClassMap.put("object", new ClassMeta(Object.class, null)); // 兼容小写写法
        sBaseClassMap.put("Object", new ClassMeta(Object.class, null));
        sBaseClassMap.put("object[]", new ClassMeta(Object[].class, null)); // 兼容小写写法
        sBaseClassMap.put("Object[]", new ClassMeta(Object[].class, null));

        // 兼容 iOS 端的类型写法
        sBaseClassMap.put("BOOL", new ClassMeta(boolean.class, null));
        sBaseClassMap.put("NSString", new ClassMeta(String.class, null));
        sBaseClassMap.put("NSInteger", new ClassMeta(int.class, null));
    }

    // 容器类型
    static {
        sCollectionClassMap.put("List", new ClassMeta(List.class, null));
        sCollectionClassMap.put("Map", new ClassMeta(Map.class, null));
    }

    /**
     * 按类型名查元数据，未命中返回 null
     */
    public static ClassMeta getClassMeta(String className) {
        if (className == null) {
            return null;
        }
        ClassMeta meta = sBaseClassMap.get(className);
        if (meta != null) {
            return meta;
        }
        String collectionType = getCollectionType(className);
        if (collectionType != null) {
            return sCollectionClassMap.get(collectionType);
        }
        return sCustomClassMap.get(className);
    }

    /**
     * 是否为基础数据类型（含数组形式）
     */
    public static boolean isBaseType(String className) {
        return className != null && sBaseClassMap.containsKey(className);
    }

    /**
     * 是否为容器类型（List / Map 及其泛型形式）
     */
    public static boolean isCollectionType(String className) {
        return className != null && (className.contains("List") || className.contains("Map"));
    }

    /**
     * 提取容器的裸类型名：List&lt;String&gt; 返回 List，非容器返回 null
     */
    private static String getCollectionType(String className) {
        if (className.contains("List")) {
            return "List";
        }
        if (className.contains("Map")) {
            return "Map";
        }
        return null;
    }
}
