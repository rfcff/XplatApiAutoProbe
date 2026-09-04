package com.xprobe.rpc.utils;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

/**
 * JSON 工具：JSON 文本与 Map/List 之间的转换
 */
public class JsonUtils {

    /**
     * Json 转成 Map
     *
     * @param jsonStr JSON 对象文本
     * @return 转换失败或入参为 null 时返回 null
     */
    public static Map<String, Object> getMapForJson(String jsonStr) {
        if (jsonStr == null || jsonStr.equals("null")) {
            return null;
        }
        try {
            JSONObject jsonObject = new JSONObject(jsonStr);

            Iterator<String> keyIter = jsonObject.keys();
            String key;
            Object value;
            Map<String, Object> valueMap = new HashMap<String, Object>();
            while (keyIter.hasNext()) {
                key = keyIter.next();
                value = jsonObject.get(key);
                valueMap.put(key, value);
            }
            return valueMap;
        } catch (Exception e) {
            e.printStackTrace();
            LogUtils.e("json转Map错误: " + e.toString());
        }
        return null;
    }

    /**
     * Json 转成 List&lt;Map&gt;
     *
     * @param jsonStr JSON 数组文本
     * @return 转换失败时返回 null
     */
    public static List<Map<String, Object>> getlistForJson(String jsonStr) {
        List<Map<String, Object>> list = null;
        try {
            JSONArray jsonArray = new JSONArray(jsonStr);
            JSONObject jsonObj;
            list = new ArrayList<Map<String, Object>>();
            for (int i = 0; i < jsonArray.length(); i++) {
                jsonObj = (JSONObject) jsonArray.get(i);
                list.add(getMapForJson(jsonObj.toString()));
            }
        } catch (Exception e) {
            e.printStackTrace();
            LogUtils.e("json转list错误: " + e.toString());
        }
        return list;
    }
}
