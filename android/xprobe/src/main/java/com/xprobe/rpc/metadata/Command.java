package com.xprobe.rpc.metadata;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * 一条解析后的命令：方法调用或成员变量修改
 */
public class Command {
    private final String mCommandName; // 方法名 / 类名
    private final CmdType mCmdType; // 命令类型
    private final ThreadMode mThreadMode; // 执行线程
    private final int mVersion; // 1: 反射调用, 2: 上层业务处理
    private final List<Parameter> mParameterList; // 反射调用方案的参数列表
    private final Map<String, Object> mParamsMap; // 业务处理方案(ver=2)的参数 map

    public Command(String commandName, CmdType cmdType) {
        this(commandName, cmdType, ThreadMode.BACKGROUND);
    }

    public Command(String commandName, CmdType cmdType, ThreadMode mode) {
        this(commandName, cmdType, mode, 1, null);
    }

    public Command(String commandName, CmdType cmdType, ThreadMode mode, int version,
                   Map<String, Object> params) {
        this.mCommandName = commandName;
        this.mCmdType = cmdType;
        this.mThreadMode = modeToEnum(mode.getTAG());
        this.mVersion = version;
        this.mParameterList = new ArrayList<>();
        this.mParamsMap = params;
    }

    public void addParameter(Parameter parameter) {
        mParameterList.add(parameter);
    }

    /**
     * 按协议数值转换为线程模式，非法值回落 BACKGROUND
     */
    public ThreadMode modeToEnum(int mode) {
        return ThreadMode.fromTag(mode);
    }

    @Override
    public String toString() {
        String format = "MethodInfo{" + (mCmdType.equals(CmdType.FILED_TYPE) ? "field:" : "api:")
                + "='" + mCommandName + '\''
                + ", params=[%s]"
                + '}';

        StringBuilder params = new StringBuilder();
        for (int i = 0; i < mParameterList.size(); i++) {
            params.append(mParameterList.get(i).toString());
            if (i < mParameterList.size() - 1) {
                params.append(",");
            }
        }
        return String.format(format, params.toString());
    }

    /**
     * 参数类型数组，无参数时返回 null
     */
    public Class<?>[] getClassArray() {
        if (mParameterList == null || mParameterList.isEmpty()) {
            return null;
        }
        Class<?>[] array = new Class<?>[mParameterList.size()];
        for (int i = 0; i < array.length; i++) {
            array[i] = mParameterList.get(i).getType();
        }
        return array;
    }

    public String getCommandName() {
        return mCommandName;
    }

    /**
     * 参数值数组，无参数时返回 null
     */
    public Object[] getParamValues() {
        if (mParameterList == null || mParameterList.isEmpty()) {
            return null;
        }
        Object[] values = new Object[mParameterList.size()];
        for (int i = 0; i < values.length; i++) {
            values[i] = mParameterList.get(i).getTargetTypeValue();
        }
        return values;
    }

    public CmdType getCmdType() {
        return mCmdType;
    }

    public List<Parameter> getParameterList() {
        return mParameterList;
    }

    public ThreadMode getThreadMode() {
        return mThreadMode;
    }

    public int getVersion() {
        return mVersion;
    }

    public Map<String, Object> getParamsMap() {
        return mParamsMap;
    }
}
