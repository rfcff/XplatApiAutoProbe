package com.xprobe.rpc.utils;

import com.xprobe.rpc.BaseObjectManager;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.lang.reflect.Method;
import java.lang.reflect.Parameter;

/**
 * API 列表生成工具：通过反射枚举指定类的公开方法，
 * 生成 GET_API 命令的响应（JSON 数组文本）。
 * <p>
 * 注意：Method.getParameters() 需要 API 26 及以上，低版本由调用方拦截。
 */
public class Api2Protocol {

    /**
     * 生成指定类的 API 描述列表：
     * [{"api":"com.foo.Bar.add","param_name":["a","b"],"param_type":["int","int"]}, ...]
     *
     * @param className 完整类名
     * @return JSON 数组文本；类不存在时返回空数组 "[]"
     */
    public static String generateProtocol(String className) {
        if (className == null || className.trim().isEmpty()) {
            throw new RuntimeException("param is null");
        }

        LogUtils.i("api result list ===========start");
        try {
            Class<?> cls = Class.forName(className);
            JSONArray methodArray = new JSONArray();

            // 遍历该类声明的全部方法
            for (Method method : cls.getDeclaredMethods()) {
                Parameter[] parameters = method.getParameters();
                if (parameters == null || parameters.length < 1) {
                    continue; // 无参方法不在此列举
                }

                JSONObject methodObj = new JSONObject();
                methodObj.put("api", className + "." + method.getName());

                JSONArray paramNameArray = new JSONArray();
                JSONArray paramTypeArray = new JSONArray();
                for (Parameter param : parameters) {
                    paramNameArray.put(param.getName());

                    String type = param.getType().toString();
                    if (param.getType().equals(byte[].class)) { // byte[].class 的 toString 为 "[B"
                        type = "byte[]";
                    }
                    type = type.replace("class ", ""); // 去掉"class "
                    type = type.replace("interface ", ""); // 去掉"interface"
                    type = type.replace("java.lang.", "");
                    // 去掉 SDK 包名前缀，让类型更简洁
                    type = type.replace(getSDKPackageName(), "");

                    paramTypeArray.put(type);
                }

                methodObj.put("param_name", paramNameArray);
                methodObj.put("param_type", paramTypeArray);

                LogUtils.i("method: " + methodObj.toString());
                methodArray.put(methodObj);
            }

            LogUtils.i("===========end");
            return methodArray.toString();
        } catch (ClassNotFoundException e) {
            // 类不存在：返回空数组
            LogUtils.e("class not found: " + ExceptionUtil.getExceptionStack(e));
            return new JSONArray().toString();
        } catch (JSONException e) {
            LogUtils.e("generate protocol error: " + ExceptionUtil.getExceptionStack(e));
            return new JSONArray().toString();
        }
    }

    /**
     * 获取 SDK 包名（用于类型名去前缀），未注册 ObjectManager 时返回空串
     */
    private static String getSDKPackageName() {
        BaseObjectManager manager = ObjectHelper.getInstance().getObjecManager();
        return manager == null ? "" : manager.getSDKPackageName();
    }
}
