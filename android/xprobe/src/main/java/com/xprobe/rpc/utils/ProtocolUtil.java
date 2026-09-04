package com.xprobe.rpc.utils;

import com.xprobe.rpc.exception.InvalidFormatException;
import com.xprobe.rpc.metadata.ClassMap;
import com.xprobe.rpc.metadata.CmdType;
import com.xprobe.rpc.metadata.Command;
import com.xprobe.rpc.metadata.Parameter;
import com.xprobe.rpc.metadata.ThreadMode;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;

/**
 * 协议解析工具：把收到的 JSON 文本解析为命令列表。
 * <ul>
 * <li>载荷支持单条命令（{...}）与命令数组（[{...}, {...}]）；</li>
 * <li>api 支持扁平形式（api 为字符串，param_name/param_type/param_value 在顶层）与
 * 嵌套形式（api 为对象，apiName/paramName/paramType/paramValue 在 api 对象内）；</li>
 * <li>参数字段名兼容 param_name / paramName 两套写法；</li>
 * <li>ver 兼容整数 2 与字符串 "2"（缺省为 1）；</li>
 * <li>支持 field 命令（修改静态成员变量）。</li>
 * </ul>
 */
public class ProtocolUtil {

    // 参数字段的两套命名（驼峰 / 下划线）
    private static final String KEY_PARAM_NAME_CAMEL = "paramName";
    private static final String KEY_PARAM_NAME_SNAKE = "param_name";
    private static final String KEY_PARAM_TYPE_CAMEL = "paramType";
    private static final String KEY_PARAM_TYPE_SNAKE = "param_type";
    private static final String KEY_PARAM_VALUE_CAMEL = "paramValue";
    private static final String KEY_PARAM_VALUE_SNAKE = "param_value";

    private static final String KEY_API_NAME = "apiName";
    private static final String KEY_PARAMS = "params";

    /**
     * 解析入口：单条命令返回长度为 1 的列表，多条命令按序返回
     *
     * @throws InvalidFormatException JSON 结构不合法或字段不满足协议
     */
    public static List<Command> parseMethodInfo(String msg) throws InvalidFormatException {
        if (msg == null || msg.trim().length() == 0) {
            throw new InvalidFormatException(String.valueOf(msg), "cmd content is empty");
        }
        try { // 支持单条命令，直接以 { 开头
            JSONObject jsonObject = new JSONObject(msg);
            Command cmd = jsonToCommand(jsonObject);
            return Arrays.asList(cmd);
        } catch (JSONException e) {
            try { // 支持多条命令，以 [] 装载
                JSONArray jsonArray = new JSONArray(msg);
                return msgToCommandList(jsonArray);
            } catch (JSONException ex) {
                LogUtils.e(ExceptionUtil.getExceptionStack(e));
                throw new InvalidFormatException(String.valueOf(msg), InvalidFormatException.EXAMPLE_CMD);
            }
        }
    }

    /**
     * 命令数组解析：数组元素为 JSON 对象或对象文本
     */
    public static List<Command> msgToCommandList(JSONArray jsonArray) throws InvalidFormatException {
        List<Command> methodList = new ArrayList<>();

        for (int i = 0; i < jsonArray.length(); i++) {
            JSONObject obj;
            try {
                Object temObj = jsonArray.get(i);
                if (temObj instanceof String) {
                    obj = new JSONObject((String) temObj);
                } else if (temObj instanceof JSONObject) {
                    obj = (JSONObject) temObj;
                } else {
                    throw new InvalidFormatException(jsonArray.toString(), InvalidFormatException.EXAMPLE_CMD);
                }
            } catch (JSONException e) {
                e.printStackTrace();
                throw new InvalidFormatException(jsonArray.toString(), InvalidFormatException.EXAMPLE_CMD);
            }
            methodList.add(jsonToCommand(obj));
        }

        // 解析结果为空，抛出异常
        if (methodList.isEmpty()) {
            throw new InvalidFormatException(jsonArray.toString(), " please input cmd content");
        }
        return methodList;
    }

    /**
     * 单个命令对象解析：按 field / api 关键字分发
     */
    private static Command jsonToCommand(JSONObject obj) {
        if (obj.has(CmdType.FILED_TYPE.getTAG())) {
            return parseField(obj);
        }
        if (obj.has(CmdType.METHOD_TYPE.getTAG())) {
            return parseMethod(obj);
        }
        throw new InvalidFormatException(obj.toString(), "only support field and api keyword command");
    }

    /**
     * 解析方法调用命令（ver=2 为自定义调用，其余为反射调用）
     */
    private static Command parseMethod(JSONObject obj) {
        try {
            Object apiObj = obj.get(CmdType.METHOD_TYPE.getTAG());
            String methodName;
            // 参数字段所在的容器：扁平形式在顶层，嵌套形式在 api 对象内
            JSONObject paramHolder;

            if (apiObj instanceof JSONObject) {
                // 嵌套形式（iOS 风格）：{"api":{"apiName":..,"paramName":[..],...}}
                JSONObject api = (JSONObject) apiObj;
                methodName = api.optString(KEY_API_NAME, "");
                paramHolder = api;
            } else if (apiObj instanceof String) {
                // 扁平形式（Android/PC 风格）：{"api":"com.foo.Bar.add",...}
                methodName = (String) apiObj;
                paramHolder = obj;
            } else {
                throw new InvalidFormatException(obj.toString(), "api must be string or object");
            }

            int version = parseVersion(obj);
            ThreadMode threadMode = parseThreadMode(obj);

            // ver=2：自定义调用，params 原样透传给 CustomInvocation
            Map<String, Object> paramsMap = null;
            if (version == 2 && apiObj instanceof JSONObject) {
                JSONObject api = (JSONObject) apiObj;
                if (api.has(KEY_PARAMS)) {
                    paramsMap = JsonUtils.getMapForJson(api.getJSONObject(KEY_PARAMS).toString());
                }
            }

            JSONArray paraNameArray = getParamArray(paramHolder, KEY_PARAM_NAME_CAMEL, KEY_PARAM_NAME_SNAKE);
            JSONArray paraTypeArray = getParamArray(paramHolder, KEY_PARAM_TYPE_CAMEL, KEY_PARAM_TYPE_SNAKE);
            JSONArray paraValueArray = getParamArray(paramHolder, KEY_PARAM_VALUE_CAMEL, KEY_PARAM_VALUE_SNAKE);

            // 协议规则：三个参数数组长度必须一致，否则解析错误
            checkParamArrayLength(obj, paraNameArray, paraTypeArray, paraValueArray);

            Command methodInfo = new Command(methodName, CmdType.METHOD_TYPE, threadMode, version, paramsMap);

            if (version == 1 && paraValueArray != null && paraValueArray.length() > 0) {
                for (int j = 0; j < paraTypeArray.length(); j++) {
                    String typeStr = paraTypeArray.getString(j);
                    boolean isBaseType = ClassMap.isBaseType(typeStr); // 是否为基础数据类型
                    String paramName = paraNameArray.getString(j);

                    Object value;
                    if (isBaseType) {
                        value = paraValueArray.getString(j);
                    } else {
                        // 自定义类型：param_value 用数组传构造参数，按顺序保存
                        JSONArray constructValues = paraValueArray.getJSONArray(j);
                        List<String> valueList = new ArrayList<>();
                        for (int k = 0; k < constructValues.length(); k++) {
                            valueList.add(constructValues.getString(k));
                        }
                        value = valueList;
                    }
                    methodInfo.addParameter(new Parameter(paramName, typeStr, value));
                }
            }
            return methodInfo;
        } catch (JSONException e) {
            LogUtils.e(ExceptionUtil.getExceptionStack(e));
            throw new InvalidFormatException(obj.toString(), InvalidFormatException.EXAMPLE_CMD);
        }
    }

    /**
     * 解析成员变量修改命令：{"field":"com.foo.DemoConfig","param_name":[..],"param_type":[..],"param_value":[..]}
     */
    private static Command parseField(JSONObject obj) {
        try {
            String className = obj.getString(CmdType.FILED_TYPE.getTAG());
            JSONArray paraNameArray = getParamArray(obj, KEY_PARAM_NAME_CAMEL, KEY_PARAM_NAME_SNAKE);
            JSONArray paraTypeArray = getParamArray(obj, KEY_PARAM_TYPE_CAMEL, KEY_PARAM_TYPE_SNAKE);
            JSONArray paraValueArray = getParamArray(obj, KEY_PARAM_VALUE_CAMEL, KEY_PARAM_VALUE_SNAKE);

            if (paraNameArray == null || paraTypeArray == null || paraValueArray == null) {
                throw new InvalidFormatException(obj.toString(), " param name, type, value cannot be null ");
            }
            checkParamArrayLength(obj, paraNameArray, paraTypeArray, paraValueArray);

            Command methodInfo = new Command(className, CmdType.FILED_TYPE);
            for (int j = 0; j < paraTypeArray.length(); j++) {
                String typeStr = paraTypeArray.getString(j);
                boolean isBaseType = ClassMap.isBaseType(typeStr); // 是否为基础数据类型
                String paramName = paraNameArray.getString(j);

                Object value;
                if (isBaseType) {
                    value = paraValueArray.getString(j);
                } else {
                    // 自定义类型：构造参数以数组下发，统一转为 List<String>
                    JSONArray constructValues = paraValueArray.getJSONArray(j);
                    List<String> valueList = new ArrayList<>();
                    for (int k = 0; k < constructValues.length(); k++) {
                        valueList.add(constructValues.getString(k));
                    }
                    value = valueList;
                }
                methodInfo.addParameter(new Parameter(paramName, typeStr, value));
            }
            return methodInfo;
        } catch (JSONException e) {
            LogUtils.e(ExceptionUtil.getExceptionStack(e));
            throw new InvalidFormatException(obj.toString(), InvalidFormatException.EXAMPLE_CMD);
        }
    }

    /**
     * 协议规则：param_name / param_type / param_value 三个数组长度必须一致，否则解析错误
     */
    private static void checkParamArrayLength(JSONObject obj, JSONArray nameArray,
                                              JSONArray typeArray, JSONArray valueArray)
            throws InvalidFormatException {
        if (valueArray == null) {
            if (nameArray != null || typeArray != null) {
                throw new InvalidFormatException(obj.toString(), " param_value is missing ");
            }
            return;
        }
        if (nameArray == null || typeArray == null
                || nameArray.length() != valueArray.length()
                || typeArray.length() != valueArray.length()) {
            throw new InvalidFormatException(obj.toString(),
                    " param_name/param_type/param_value count must be equal ");
        }
    }

    /**
     * 兼容 paramName / param_name 两套字段名，优先驼峰写法
     */
    private static JSONArray getParamArray(JSONObject holder, String camelName, String snakeName) {
        JSONArray array = holder.optJSONArray(camelName);
        if (array == null) {
            array = holder.optJSONArray(snakeName);
        }
        return array;
    }

    /**
     * 解析协议版本：整数 2 或字符串 "2" 均有效，缺省为 1（反射调用）
     */
    private static int parseVersion(JSONObject obj) {
        Object ver = obj.opt("ver");
        if (ver == null) {
            return 1;
        }
        if (ver instanceof Number) {
            return ((Number) ver).intValue();
        }
        try {
            return Integer.parseInt(String.valueOf(ver).trim());
        } catch (NumberFormatException e) {
            LogUtils.e("invalid ver: " + ver);
            return 1;
        }
    }

    /**
     * 解析线程模式：0 或缺省 = BACKGROUND，1 = MAIN
     */
    private static ThreadMode parseThreadMode(JSONObject obj) {
        Object mode = obj.opt("threadMode");
        if (mode == null) {
            return ThreadMode.BACKGROUND;
        }
        if (mode instanceof Number) {
            return ThreadMode.fromTag(((Number) mode).intValue());
        }
        try {
            return ThreadMode.fromTag(Integer.parseInt(String.valueOf(mode).trim()));
        } catch (NumberFormatException e) {
            LogUtils.e("invalid threadMode: " + mode);
            return ThreadMode.BACKGROUND;
        }
    }
}
