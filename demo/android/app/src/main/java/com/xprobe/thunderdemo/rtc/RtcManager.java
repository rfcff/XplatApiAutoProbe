package com.xprobe.thunderdemo.rtc;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import com.thunder.livesdk.ThunderEngine;
import com.thunder.livesdk.ThunderEventHandler;
import com.thunder.livesdk.ThunderRtcConstant;
import com.thunder.livesdk.ThunderVideoCanvas;
import com.thunder.livesdk.video.ThunderPlayerView;
import com.thunder.livesdk.video.ThunderPreviewView;
import com.xprobe.rpc.ChannelCache;

/**
 * thunder RTC SDK 极简封装
 * <p>
 * 覆盖 demo 所需的最小功能集：初始化/销毁引擎、进/退频道、订阅远端音视频、
 * 本地采集与预览、远端视频渲染；SDK 回调统一转发给 RPC 测试端与 UI 日志。
 * <p>
 * 注意：Thunder 视频视图内部依赖 native 库，必须在引擎创建后才可实例化，
 * 因此视频视图由本类在主线程按需动态创建并挂载到容器中（不能写进 XML 布局）。
 */
public class RtcManager extends ThunderEventHandler {

    private static final String TAG = "RtcManager";

    private static volatile RtcManager sInstance;

    /** 视图容器（MainActivity 注入） */
    private ViewGroup mLocalContainer;
    private ViewGroup mRemoteContainer;
    /** thunder 视频视图（引擎创建后按需生成） */
    private ThunderPreviewView mLocalView;
    private ThunderPlayerView mRemoteView;

    private ThunderEngine mEngine;
    private String mRoomName;
    private String mUid;
    /** 最近一次加入频道的远端用户（订阅目标） */
    private volatile String mRemoteUid = "";

    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    /** UI 日志回调（MainActivity 注入，SDK 回调线程 -> 主线程刷新） */
    public interface UiLogger {
        void onLog(String msg);
    }

    private volatile UiLogger mUiLogger;

    private RtcManager() {
    }

    public static RtcManager getInstance() {
        if (sInstance == null) {
            synchronized (RtcManager.class) {
                if (sInstance == null) {
                    sInstance = new RtcManager();
                }
            }
        }
        return sInstance;
    }

    public void setUiLogger(UiLogger logger) {
        mUiLogger = logger;
    }

    public void setVideoContainers(ViewGroup localContainer, ViewGroup remoteContainer) {
        mLocalContainer = localContainer;
        mRemoteContainer = remoteContainer;
    }

    /** 供 RPC 反射方案获取本地预览视图（不存在则在主线程创建） */
    public ThunderPreviewView ensureLocalView() {
        if (mLocalView == null && mEngine != null && mLocalContainer != null) {
            runOnMain(new Runnable() {
                @Override
                public void run() {
                    if (mLocalView == null && mEngine != null) {
                        mLocalView = new ThunderPreviewView(mLocalContainer.getContext());
                        mLocalContainer.addView(mLocalView, new FrameLayout.LayoutParams(
                                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
                    }
                }
            });
        }
        return mLocalView;
    }

    /** 供 RPC 反射方案获取远端播放视图（不存在则在主线程创建） */
    public ThunderPlayerView ensureRemoteView() {
        if (mRemoteView == null && mEngine != null && mRemoteContainer != null) {
            runOnMain(new Runnable() {
                @Override
                public void run() {
                    if (mRemoteView == null && mEngine != null) {
                        mRemoteView = new ThunderPlayerView(mRemoteContainer.getContext());
                        mRemoteContainer.addView(mRemoteView, new FrameLayout.LayoutParams(
                                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
                    }
                }
            });
        }
        return mRemoteView;
    }

    public ThunderEngine getEngine() {
        return mEngine;
    }

    public boolean isInitialized() {
        return mEngine != null;
    }

    public String getRoomName() {
        return mRoomName;
    }

    public String getUid() {
        return mUid;
    }

    public String getRemoteUid() {
        return mRemoteUid;
    }

    // ------------------------------------------------------------------
    // 引擎生命周期
    // ------------------------------------------------------------------

    /** 初始化 SDK（重复调用返回 0 耗时，幂等；引擎创建固定在主线程，SDK 要求调用线程持有 Looper） */
    public synchronized long initialize(Context context, String appId, long sceneId) {
        if (mEngine != null) {
            return 0;
        }
        final Context appCtx = context.getApplicationContext();
        final String id = appId;
        final long scene = sceneId;
        final long[] cost = new long[1];
        runOnMain(new Runnable() {
            @Override
            public void run() {
                long begin = SystemClock.elapsedRealtime();
                mEngine = ThunderEngine.createEngine(appCtx, id, scene, RtcManager.this);
                cost[0] = SystemClock.elapsedRealtime() - begin;
            }
        });
        if (mEngine != null) {
            // 音频属性：音乐标准
            mEngine.setAudioConfig(
                    ThunderRtcConstant.AudioConfig.THUNDER_AUDIO_CONFIG_MUSIC_STANDARD_PRO,
                    ThunderRtcConstant.CommutMode.THUNDER_COMMUT_MODE_DEFAULT,
                    ThunderRtcConstant.ScenarioMode.THUNDER_SCENARIO_MODE_DEFAULT);
        }
        log("createEngine 耗时 " + cost[0] + "ms, appId=" + appId + ", sceneId=" + sceneId);
        return cost[0];
    }

    /** 销毁 SDK */
    public synchronized void deInitialize() {
        if (mEngine != null) {
            mEngine.destroyEngine();
            mEngine = null;
            mRoomName = null;
            mRemoteUid = "";
            // 引擎销毁后视频视图不可复用，一并移除
            final ThunderPreviewView local = mLocalView;
            final ThunderPlayerView remote = mRemoteView;
            mLocalView = null;
            mRemoteView = null;
            runOnMain(new Runnable() {
                @Override
                public void run() {
                    if (local != null && local.getParent() instanceof ViewGroup) {
                        ((ViewGroup) local.getParent()).removeView(local);
                    }
                    if (remote != null && remote.getParent() instanceof ViewGroup) {
                        ((ViewGroup) remote.getParent()).removeView(remote);
                    }
                }
            });
            log("destroyEngine 完成");
        }
    }

    // ------------------------------------------------------------------
    // 频道
    // ------------------------------------------------------------------

    /** 加入频道（空 token 模式） */
    public int joinRoom(String roomName, String uid) {
        if (mEngine == null) {
            return -1;
        }
        mRoomName = roomName;
        mUid = uid;
        int ret = mEngine.joinRoom(new byte[0], roomName, uid);
        log("joinRoom(" + roomName + ", " + uid + ") ret=" + ret);
        return ret;
    }

    /** 离开频道 */
    public int leaveRoom() {
        if (mEngine == null) {
            return -1;
        }
        int ret = mEngine.leaveRoom();
        mRemoteUid = "";
        log("leaveRoom() ret=" + ret);
        return ret;
    }

    // ------------------------------------------------------------------
    // 订阅与播放
    // ------------------------------------------------------------------

    /** 订阅频道内指定远端用户的音视频流 */
    public int addSubscribe(String channelId, String uid) {
        if (mEngine == null) {
            return -1;
        }
        int ret = mEngine.addSubscribe(channelId, uid);
        log("addSubscribe(" + channelId + ", " + uid + ") ret=" + ret);
        return ret;
    }

    /** 取消订阅 */
    public int removeSubscribe(String channelId, String uid) {
        if (mEngine == null) {
            return -1;
        }
        int ret = mEngine.removeSubscribe(channelId, uid);
        log("removeSubscribe(" + channelId + ", " + uid + ") ret=" + ret);
        return ret;
    }

    /** 设置远端视频画布并开始渲染（renderMode 1=FIT） */
    public int setupRemoteVideo(String uid) {
        if (mEngine == null) {
            return -1;
        }
        final String target = uid;
        final int[] ret = new int[1];
        runOnMain(new Runnable() {
            @Override
            public void run() {
                ensureRemoteView();
                if (mRemoteView == null) {
                    ret[0] = -1;
                    return;
                }
                ret[0] = mEngine.setRemoteVideoCanvas(new ThunderVideoCanvas(mRemoteView, 1, target));
            }
        });
        log("setRemoteVideoCanvas(" + uid + ") ret=" + ret[0]);
        return ret[0];
    }

    /** 开启本地采集并预览 */
    public int startLocalPreview() {
        if (mEngine == null) {
            return -1;
        }
        final int[] ret = new int[1];
        runOnMain(new Runnable() {
            @Override
            public void run() {
                ensureLocalView();
                if (mLocalView == null) {
                    ret[0] = -1;
                    return;
                }
                ret[0] = mEngine.setLocalVideoCanvas(new ThunderVideoCanvas(mLocalView, 1, mUid));
                if (ret[0] == 0) {
                    ret[0] = mEngine.enableLocalVideoCapture(true);
                    if (ret[0] == 0) {
                        ret[0] = mEngine.startLocalVideoPreview();
                    }
                }
            }
        });
        log("startLocalPreview() ret=" + ret[0]);
        return ret[0];
    }

    /** 关闭本地采集与预览 */
    public int stopLocalPreview() {
        if (mEngine == null) {
            return -1;
        }
        final int[] ret = new int[1];
        runOnMain(new Runnable() {
            @Override
            public void run() {
                ret[0] = mEngine.stopLocalVideoPreview();
                if (mLocalView != null) {
                    mLocalView.setVisibility(View.INVISIBLE);
                }
            }
        });
        log("stopLocalPreview() ret=" + ret[0]);
        return ret[0];
    }

    // ------------------------------------------------------------------
    // SDK 回调（ThunderEventHandler）
    // ------------------------------------------------------------------

    @Override
    public void onJoinRoomSuccess(String channel, String uid, int elapsed) {
        String msg = "onJoinRoomSuccess: room=" + channel + " uid=" + uid + " elapsed=" + elapsed;
        log(msg);
        ChannelCache.sendCallbackMsg("onJoinRoomSuccess", msg);
    }

    @Override
    public void onJoinRoomStatus(String channel, String uid, int status, int elapsed) {
        String msg = "onJoinRoomStatus: room=" + channel + " uid=" + uid
                + " status=" + status + " elapsed=" + elapsed;
        log(msg);
        ChannelCache.sendCallbackMsg("onJoinRoomStatus", msg);
    }

    @Override
    public void onLeaveRoom(ThunderEventHandler.RoomStats roomStats) {
        String msg = "onLeaveRoom: status=" + (roomStats != null ? roomStats.temp : "null");
        log(msg);
        ChannelCache.sendCallbackMsg("onLeaveRoom", msg);
    }

    @Override
    public void onUserJoined(String uid, int elapsed) {
        mRemoteUid = uid;
        String msg = "onUserJoined: uid=" + uid + " elapsed=" + elapsed;
        log(msg);
        ChannelCache.sendCallbackMsg("onUserJoined", msg);
    }

    @Override
    public void onRemoteAudioPlay(String uid, int elapsed) {
        String msg = "onRemoteAudioPlay: uid=" + uid + " elapsed=" + elapsed;
        log(msg);
        ChannelCache.sendCallbackMsg("onRemoteAudioPlay", msg);
    }

    @Override
    public void onRemoteVideoPlay(String uid, int width, int height, int elapsed) {
        String msg = "onRemoteVideoPlay: uid=" + uid + " " + width + "x" + height
                + " elapsed=" + elapsed;
        log(msg);
        ChannelCache.sendCallbackMsg("onRemoteVideoPlay", msg);
    }

    @Override
    public void onRemoteAudioStopped(String uid, boolean stopped) {
        String msg = "onRemoteAudioStopped: uid=" + uid + " stopped=" + stopped;
        log(msg);
        ChannelCache.sendCallbackMsg("onRemoteAudioStopped", msg);
    }

    @Override
    public void onRemoteVideoStopped(String uid, boolean stopped) {
        String msg = "onRemoteVideoStopped: uid=" + uid + " stopped=" + stopped;
        log(msg);
        ChannelCache.sendCallbackMsg("onRemoteVideoStopped", msg);
    }

    @Override
    public void onConnectionStatus(int status) {
        String msg = "onConnectionStatus: status=" + status;
        log(msg);
        ChannelCache.sendCallbackMsg("onConnectionStatus", msg);
    }

    @Override
    public void onError(int error) {
        String msg = "onError: code=" + error;
        log(msg);
        ChannelCache.sendCallbackMsg("onError", msg);
    }

    // ------------------------------------------------------------------
    // 工具
    // ------------------------------------------------------------------

    private void runOnMain(Runnable r) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            r.run();
        } else {
            final Object lock = new Object();
            synchronized (lock) {
                mMainHandler.post(new Runnable() {
                    @Override
                    public void run() {
                        r.run();
                        synchronized (lock) {
                            lock.notifyAll();
                        }
                    }
                });
                try {
                    lock.wait(3000);
                } catch (InterruptedException ignored) {
                    Thread.currentThread().interrupt();
                }
            }
        }
    }

    /** SDK 回调线程与子线程统一打日志：UI + RPC 端 */
    private void log(final String msg) {
        android.util.Log.i(TAG, msg);
        UiLogger logger = mUiLogger;
        if (logger != null) {
            final String line = msg;
            mMainHandler.post(new Runnable() {
                @Override
                public void run() {
                    UiLogger l = mUiLogger;
                    if (l != null) {
                        l.onLog(line);
                    }
                }
            });
        }
    }

    /** 空值兜底 */
    public static String safe(String s, String def) {
        return s == null || s.length() == 0 ? def : s;
    }
}
