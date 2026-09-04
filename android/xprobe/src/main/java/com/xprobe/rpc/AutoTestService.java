package com.xprobe.rpc;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

import com.xprobe.rpc.utils.ExceptionUtil;
import com.xprobe.rpc.utils.LogUtils;

/**
 * 前台保活 Service：承载 AutoServer 的生命周期。
 * 不依赖 androidx，直接使用平台 Notification API。
 */
public class AutoTestService extends Service {

    // 前台通知 id
    private static final int NOTIFICATION_ID = 101;
    // 通知渠道 id
    private static final String CHANNEL_ID = "com.xprobe.rpc";

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        if (!AutoTestRpcServer.isStartAllowed(getApplicationContext())) {
            // 非 debuggable 构建未显式放行：不建 server，直接停止自身
            stopSelf();
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startRpcForeground();
        }
        try {
            LogUtils.i("service thread:" + Thread.currentThread().getId());
            // 端口由 AutoTestRpcServer.start(...) 指定，缺省 9000；
            // Service 被系统重建时，若 server 仍在运行则不重复创建
            AutoServer exist = AutoTestRpcServer.getServer();
            if (exist == null || !exist.isRunning()) {
                AutoTestRpcServer.holdServer(
                        new AutoServer(getApplicationContext(), AutoTestRpcServer.getPort()));
            }
        } catch (Exception e) {
            e.printStackTrace();
            LogUtils.e("启动auto test server失败" + ExceptionUtil.getExceptionStack(e));
        }
    }

    @Override
    public void onDestroy() {
        // 服务销毁时联动停止 rpc server
        AutoTestRpcServer.stopServerQuietly();
        super.onDestroy();
    }

    /**
     * 升级为前台服务（仅 API 26 及以上调用）
     */
    private void startRpcForeground() {
        NotificationChannel channel = new NotificationChannel(CHANNEL_ID, "Rpc server Service",
                NotificationManager.IMPORTANCE_NONE);
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.createNotificationChannel(channel);
        }

        // 通知小图标取宿主应用图标，取不到时用系统默认图标
        int icon = getApplicationInfo().icon;
        if (icon == 0) {
            icon = android.R.drawable.sym_def_app_icon;
        }
        Notification notification = new Notification.Builder(this, CHANNEL_ID)
                .setOngoing(true)
                .setSmallIcon(icon)
                .setContentTitle("RPC server is running")
                .build();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            // targetSdk 34+ 强制要求前台服务声明类型，显式传入避免 MissingForegroundServiceTypeException
            startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
    }
}
