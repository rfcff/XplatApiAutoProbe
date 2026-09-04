package com.xprobe.thunderdemo;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.text.method.ScrollingMovementMethod;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;
import com.xprobe.thunderdemo.rtc.RtcManager;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * thunder SDK 简易测试主界面
 * <p>
 * 功能：初始化/销毁 SDK、进/退频道、订阅远端音视频、本地摄像头预览、远端视频播放；
 * 同时内置 RPC 探针服务（端口 9000），可被 XplatApiAutoProbe Python 客户端远程驱动。
 */
public class MainActivity extends AppCompatActivity implements RtcManager.UiLogger {

    private static final int MAX_LOG_LINES = 300;
    private static final SimpleDateFormat TS = new SimpleDateFormat("HH:mm:ss", Locale.US);

    private static MainActivity sInstance;

    private TextInputEditText mAppIdInput;
    private TextInputEditText mSceneIdInput;
    private TextInputEditText mRoomInput;
    private TextInputEditText mUidInput;
    private TextView mLogView;
    private final StringBuilder mLogBuilder = new StringBuilder();

    private MaterialButton mInitBtn;
    private MaterialButton mDestroyBtn;
    private MaterialButton mJoinBtn;
    private MaterialButton mLeaveBtn;
    private MaterialButton mCameraBtn;
    private MaterialButton mSubAudioBtn;
    private MaterialButton mSubVideoBtn;

    private boolean mCameraOn = false;
    private boolean mSubAudioOn = false;
    private boolean mSubVideoOn = false;

    private final ActivityResultLauncher<String[]> mPermissionLauncher =
            registerForActivityResult(new ActivityResultContracts.RequestMultiplePermissions(),
                    result -> appendLog("权限请求完成: " + result));

    public static MainActivity getInstance() {
        return sInstance;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sInstance = this;
        setContentView(R.layout.activity_main);

        bindViews();
        requestRtcPermissions();
        setupButtons();

        // 注入视频视图容器与 UI 日志（thunder 视图由 RtcManager 在引擎创建后动态挂载）
        RtcManager.getInstance().setVideoContainers(
                findViewById(R.id.local_video_container),
                findViewById(R.id.remote_video_container));
        RtcManager.getInstance().setUiLogger(this);

        appendLog("demo 已启动，RPC 服务监听 0.0.0.0:9000");
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (sInstance == this) {
            sInstance = null;
        }
    }

    private void bindViews() {
        mAppIdInput = findViewById(R.id.input_app_id);
        mSceneIdInput = findViewById(R.id.input_scene_id);
        mRoomInput = findViewById(R.id.input_room);
        mUidInput = findViewById(R.id.input_uid);
        mLogView = findViewById(R.id.log_view);
        mLogView.setMovementMethod(new ScrollingMovementMethod());

        mInitBtn = findViewById(R.id.btn_init);
        mDestroyBtn = findViewById(R.id.btn_destroy);
        mJoinBtn = findViewById(R.id.btn_join);
        mLeaveBtn = findViewById(R.id.btn_leave);
        mCameraBtn = findViewById(R.id.btn_camera);
        mSubAudioBtn = findViewById(R.id.btn_sub_audio);
        mSubVideoBtn = findViewById(R.id.btn_sub_video);
    }

    private void requestRtcPermissions() {
        String[] wanted = new String[]{
                Manifest.permission.RECORD_AUDIO,
                Manifest.permission.CAMERA,
                Manifest.permission.POST_NOTIFICATIONS
        };
        boolean need = false;
        for (String p : wanted) {
            if (ContextCompat.checkSelfPermission(this, p) != PackageManager.PERMISSION_GRANTED) {
                need = true;
                break;
            }
        }
        if (need) {
            mPermissionLauncher.launch(wanted);
        }
    }

    private void setupButtons() {
        RtcManager rtc = RtcManager.getInstance();

        // ---- 引擎生命周期 ----
        mInitBtn.setOnClickListener(v -> {
            String appId = text(mAppIdInput, "10034");
            long sceneId = Long.parseLong(text(mSceneIdInput, "0"));
            long cost = rtc.initialize(getApplicationContext(), appId, sceneId);
            toast(cost >= 0 ? "初始化完成，耗时 " + cost + "ms" : "初始化失败");
            appendLog("createEngine appId=" + appId + " sceneId=" + sceneId + " cost=" + cost + "ms");
        });
        mDestroyBtn.setOnClickListener(v -> {
            rtc.deInitialize();
            resetToggles();
            toast("SDK 已销毁");
        });

        // ---- 频道 ----
        mJoinBtn.setOnClickListener(v -> {
            String room = text(mRoomInput, "82552971");
            String uid = text(mUidInput, "123456789");
            int ret = rtc.joinRoom(room, uid);
            toast(ret == 0 ? "进频道中: " + room : "joinRoom 失败: " + ret);
        });
        mLeaveBtn.setOnClickListener(v -> {
            int ret = rtc.leaveRoom();
            resetToggles();
            toast(ret == 0 ? "已离开频道" : "leaveRoom 失败: " + ret);
        });

        // ---- 本地摄像头 ----
        mCameraBtn.setOnClickListener(v -> {
            mCameraOn = !mCameraOn;
            int ret = mCameraOn ? rtc.startLocalPreview() : rtc.stopLocalPreview();
            if (ret != 0) {
                mCameraOn = false;
            }
            refreshButtons();
        });

        // ---- 订阅远端 ----
        mSubAudioBtn.setOnClickListener(v -> toggleSubscribe(true));
        mSubVideoBtn.setOnClickListener(v -> toggleSubscribe(false));
    }

    private void toggleSubscribe(boolean audio) {
        RtcManager rtc = RtcManager.getInstance();
        String room = text(mRoomInput, rtc.getRoomName());
        String remoteUid = rtc.getRemoteUid();
        if (remoteUid.isEmpty()) {
            toast("尚未收到远端用户（onUserJoined）");
            return;
        }
        boolean on = audio ? mSubAudioOn : mSubVideoOn;
        int ret;
        if (on) {
            ret = rtc.removeSubscribe(room, remoteUid);
        } else {
            ret = rtc.addSubscribe(room, remoteUid);
            if (ret == 0 && !audio) {
                // 订阅视频后同步设置远端画布开始渲染
                rtc.setupRemoteVideo(remoteUid);
            }
        }
        if (ret == 0) {
            if (audio) {
                mSubAudioOn = !mSubAudioOn;
            } else {
                mSubVideoOn = !mSubVideoOn;
            }
        } else {
            toast("订阅操作失败: " + ret);
        }
        refreshButtons();
    }

    private void resetToggles() {
        mCameraOn = false;
        mSubAudioOn = false;
        mSubVideoOn = false;
        refreshButtons();
    }

    private void refreshButtons() {
        mCameraBtn.setText(mCameraOn ? R.string.btn_camera_off : R.string.btn_camera_on);
        mSubAudioBtn.setText(mSubAudioOn ? R.string.btn_sub_audio_off : R.string.btn_sub_audio_on);
        mSubVideoBtn.setText(mSubVideoOn ? R.string.btn_sub_video_off : R.string.btn_sub_video_on);
    }

    // ------------------------------------------------------------------
    // UI 日志（RtcManager.UiLogger）
    // ------------------------------------------------------------------

    @Override
    public void onLog(String msg) {
        appendLog(msg);
    }

    private void appendLog(String msg) {
        runOnUiThread(() -> {
            mLogBuilder.append('[').append(TS.format(new Date())).append("] ")
                    .append(msg).append('\n');
            // 控制日志行数
            int over = mLogBuilder.toString().split("\n").length - MAX_LOG_LINES;
            if (over > 0) {
                int idx = 0;
                for (int i = 0; i < over; i++) {
                    idx = mLogBuilder.indexOf("\n", idx) + 1;
                }
                mLogBuilder.delete(0, idx);
            }
            mLogView.setText(mLogBuilder);
            mLogView.scrollTo(0, Integer.MAX_VALUE);
        });
    }

    private void toast(String msg) {
        runOnUiThread(() -> Toast.makeText(this, msg, Toast.LENGTH_SHORT).show());
    }

    private static String text(TextInputEditText input, String def) {
        CharSequence cs = input != null ? input.getText() : null;
        return cs == null || cs.length() == 0 ? def : cs.toString().trim();
    }
}
