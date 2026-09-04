package com.xprobe.rpc.metadata;

import com.xprobe.rpc.BaseObjectManager;
import com.xprobe.rpc.utils.ExceptionUtil;
import com.xprobe.rpc.utils.LogUtils;
import com.xprobe.rpc.utils.ObjectHelper;

import java.util.List;

/**
 * 方法参数的元数据：参数名、类型与取值。
 * <p>
 * 自定义数据类型说明：
 * 1. 由 BaseObjectManager.generateCustomType 构造；
 * 2. 自定义类型的参数值要求以数组形式下发（无参数名，按顺序作为构造参数）；
 * 3. 建议使用完整包名定义自定义类型，否则默认拼接 SDK 包名。
 */
public class Parameter {
    private final String mName; // 参数名
    private final String mTypeName; // 类型名（原始字符串）
    private Class<?> mTypeClass; // 参数类型
    private final Object mValue; // 参数值，自定义类型参数以 List<String> 存储

    public Parameter(String name, String type, Object value) {
        this.mName = name;
        this.mValue = value;
        setType(type);
        this.mTypeName = type;
    }

    public String getName() {
        return mName;
    }

    public Class<?> getType() {
        return mTypeClass;
    }

    /**
     * 按类型名解析参数类型：基础类型走注册表，自定义类型反射加载
     */
    private void setType(String type) {
        if (type == null || type.trim().length() == 0) {
            return;
        }
        if (type.indexOf("<") > 0 && type.indexOf(">") > type.indexOf("<")) {
            // 去掉泛型参数，如 List<String> -> List
            type = type.substring(0, type.indexOf("<"));
        }
        ClassMeta meta = ClassMap.getClassMeta(type);
        if (meta != null) { // 基础类型
            this.mTypeClass = meta.getmClass();
        } else { // 自定义类型
            try {
                if (type.indexOf(".") > 0) {
                    this.mTypeClass = Class.forName(type);
                } else {
                    BaseObjectManager manager = ObjectHelper.getInstance().getObjecManager();
                    if (manager != null) {
                        // 未使用完整包名时，默认拼接 SDK 包名
                        this.mTypeClass = Class.forName(manager.getSDKPackageName() + "." + type);
                    } else {
                        // 未注册 ObjectManager（如仅使用 CustomInvocation 场景），无法补全包名
                        LogUtils.w("ObjectManager not registered, skip custom type: " + type);
                    }
                }
            } catch (ClassNotFoundException e) {
                LogUtils.e("paramter error:" + ExceptionUtil.getExceptionStack(e));
            }
        }
    }

    public Object getValue() {
        return mValue;
    }

    /**
     * 数据类型转化：基础类型由 BaseObjectManager 转换，自定义类型交由其构造
     */
    public Object getTargetTypeValue() {
        BaseObjectManager manager = ObjectHelper.getInstance().getObjecManager();
        if (ClassMap.isBaseType(mTypeName)) {
            // 基础数据类型
            return manager.generateBaseType((String) mValue, mTypeClass);
        }
        // 自定义类型
        return manager.generateCustomType(mTypeName, (List<String>) mValue);
    }

    @Override
    public String toString() {
        return "Parameter{" +
                "name='" + mName + '\'' +
                ", typeName='" + mTypeName + '\'' +
                ", typeClass=" + mTypeClass +
                ", value=" + mValue +
                '}';
    }
}
