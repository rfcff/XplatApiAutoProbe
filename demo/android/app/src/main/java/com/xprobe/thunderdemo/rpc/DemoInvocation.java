package com.xprobe.thunderdemo.rpc;

import android.content.Context;

import com.xprobe.rpc.ChannelCache;
import com.xprobe.rpc.CustomInvocation;
import com.xprobe.thunderdemo.rtc.RtcManager;

import java.util.HashMap;
import java.util.Map;

/**
 * RPC 自定义调用方案（ver=2）：
 * <p>
 * 测试端下发 {"api":{"apiName":"命令名","params":{...}},"threadMode":0,"ver":2}，
 * 本类将命令映射到 RtcManager 的简易操作并回传返回值，配合 Python 客户端
 * 即可一键完成「初始化 -> 进频道 -> 订阅 -> 播放」的自动化验证。
 * <p>
 * 支持命令：
 * <ul>
 *   <li>createEngine   {appId, sceneId}          初始化 SDK，返回耗时(ms)</li>
 *   <li>destroyEngine  {}                        销毁 SDK，返回 0</li>
 *   <li>joinRoom       {roomName, uid}           进频道，返回错误码</li>
 *   <li>leaveRoom      {}                        退频道，返回错误码</li>
 *   <li>addSubscribe   {roomName, uid}           订阅远端音视频，返回错误码</li>
 *   <li>removeSubscribe{roomName, uid}           取消订阅，返回错误码</li>
 *   <li>startLocalPreview {}                     开启本地采集与预览，返回错误码</li>
 *   <li>stopLocalPreview  {}                     关闭本地采集与预览，返回错误码</li>
 *   <li>setupRemoteVideo  {uid}                  设置远端视频画布，返回错误码</li>
 *   <li>getState      {}                         返回 {init, room, uid, remoteUid}</li>
 *   <li>echo          {任意}                     回显 apiName 与 params（协议回归）</li>
 *   <li>scheduleCallback {name, info, delayMs}   先 return 再异步 callback（协议回归）</li>
 * </ul>
 */
public class DemoInvocation implements CustomInvocation {

    private final Context mContext;
    private final android.os.Handler mMainHandler =
            new android.os.Handler(android.os.Looper.getMainLooper());

    public DemoInvocation(Context context) {
        mContext = context.getApplicationContext();
    }

    @Override
    public void callMethod(String api, Map<String, Object> params) {
        if (params == null) {
            params = new HashMap<>();
        }
        try {
            dispatch(api, params);
        } catch (Exception e) {
            ChannelCache.sendErrorInfo(api, android.util.Log.getStackTraceString(e));
        }
    }

    private void dispatch(String api, Map<String, Object> params) {
        RtcManager rtc = RtcManager.getInstance();
        if ("createEngine".equals(api)) {
            String appId = str(params.get("appId"), "10034");
            long sceneId = lng(params.get("sceneId"), 0);
            long cost = rtc.initialize(mContext, appId, sceneId);
            ChannelCache.sendMethodReturn(api, String.valueOf(cost));
        } else if ("destroyEngine".equals(api)) {
            rtc.deInitialize();
            ChannelCache.sendMethodReturn(api, "0");
        } else if ("joinRoom".equals(api)) {
            String room = str(params.get("roomName"), "82552971");
            String uid = str(params.get("uid"), "123456789");
            int ret = rtc.joinRoom(room, uid);
            ChannelCache.sendMethodReturn(api, String.valueOf(ret));
        } else if ("leaveRoom".equals(api)) {
            int ret = rtc.leaveRoom();
            ChannelCache.sendMethodReturn(api, String.valueOf(ret));
        } else if ("addSubscribe".equals(api)) {
            String room = str(params.get("roomName"), rtc.getRoomName());
            String uid = str(params.get("uid"), rtc.getRemoteUid());
            int ret = rtc.addSubscribe(room, uid);
            ChannelCache.sendMethodReturn(api, String.valueOf(ret));
        } else if ("removeSubscribe".equals(api)) {
            String room = str(params.get("roomName"), rtc.getRoomName());
            String uid = str(params.get("uid"), rtc.getRemoteUid());
            int ret = rtc.removeSubscribe(room, uid);
            ChannelCache.sendMethodReturn(api, String.valueOf(ret));
        } else if ("startLocalPreview".equals(api)) {
            int ret = rtc.startLocalPreview();
            ChannelCache.sendMethodReturn(api, String.valueOf(ret));
        } else if ("stopLocalPreview".equals(api)) {
            int ret = rtc.stopLocalPreview();
            ChannelCache.sendMethodReturn(api, String.valueOf(ret));
        } else if ("setupRemoteVideo".equals(api)) {
            String uid = str(params.get("uid"), rtc.getRemoteUid());
            int ret = rtc.setupRemoteVideo(uid);
            ChannelCache.sendMethodReturn(api, String.valueOf(ret));
        } else if ("getState".equals(api)) {
            String state = "init=" + rtc.isInitialized()
                    + ", room=" + rtc.getRoomName()
                    + ", uid=" + rtc.getUid()
                    + ", remoteUid=" + rtc.getRemoteUid();
            ChannelCache.sendMethodReturn(api, state);
        } else if ("echo".equals(api)) {
            ChannelCache.sendMethodReturn(api, "echo api=" + api + " params=" + params);
        } else if ("scheduleCallback".equals(api)) {
            final String name = str(params.get("name"), "onProbeCallback");
            final String info = str(params.get("info"), "ok");
            long delayMs = lng(params.get("delayMs"), 200);
            ChannelCache.sendMethodReturn(api, "0");
            mMainHandler.postDelayed(new Runnable() {
                @Override
                public void run() {
                    ChannelCache.sendCallbackMsg(name, info);
                }
            }, delayMs);
        } else {
            ChannelCache.sendErrorInfo(api, "unknown api: " + api);
        }
    }

    private static String str(Object o, String def) {
        return o == null ? def : String.valueOf(o);
    }

    private static long lng(Object o, long def) {
        if (o == null) {
            return def;
        }
        try {
            if (o instanceof Number) {
                return ((Number) o).longValue();
            }
            return Long.parseLong(String.valueOf(o));
        } catch (NumberFormatException e) {
            return def;
        }
    }
}
