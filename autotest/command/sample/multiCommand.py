# encoding=UTF-8
# ==================================================SDKAutoTestActions=================================================

# 初始化SDK
CREATE_ENGINE = "createEngine";

# 卸载SDK
DESTROY_ENGINE = "destroyEngine";

# 设置区域
# 参数:  AREA_ID  0 -> 国内，1 -> 国外，11 -> 联调，12 -> QA测试
SET_AREA = "setArea";

# 内部配置
SET_PARAMETER = "setParameter";

# 设置远端视图显示模式
# 参数:  MUTE  TRUE -> 关闭，FALSE -> 打开
SET_REMOTE_PLAY_TYPE = "setRemotePlayType";

# 进频道
JOIN_ROOM = "joinRoom";

# 初始化后进频道（包括初始化，进频道）
JOIN_ROOM_WITH_INIT = "joinRoomWithInit";

# 纯音频模式进频道（包括初始化，设置纯音频模式，进频道）
JOIN_ROOM_WITH_AUDIO_MODE = "joinRoomWithAudioMode";

# 退频道
LEAVE_ROOM = "leaveRoom";

# 只开播音频（包括初始化，进频道，打开音频）
START_AUDIO_LIVE = "startAudioLive";

# 只开播视频（包括初始化，进频道，开播视频）
START_VIDEO_LIVE = "startVideoLive";

# 只开播视频（前提是已初始化和进入频道）
START_VIDEO_LIVE_WITHOUT_INIT = "startVideoLiveWithoutInit";

# 开播音视频（包括初始化，进频道，开播音视频）
START_AUDIO_VIDEO_LIVE = "startAudioVideoLive";

# 纯音频开播（包括初始化，进频道，设置纯音频模式，打开音频）
START_ADUIO_LIVE_WITH_AUDIO_MODE = "startAudioLiveWithAudioMode";

# 高清开播音视频
START_AUDIO_VIDEO_LIVE_WITH_HD = "startVideoLiveWithHD";

# 观众进频道，订阅音频（包括初始化，进频道，订阅音频）
SUBSCRIBE_AUDIO = "subscribeAudio";

# 观众进频道，订阅视频（包括初始化，进频道，订阅视频）
SUBSCRIBE_VIDEO = "subscribeVideo";

# 观众进频道，订阅音视频（包括初始化，进频道，订阅音视频）
SUBSCRIBE_AUDIO_VIDEO = "subscribeAudioVideo";

# 设置SDK媒体模式
SET_MEDIA_MODE = "setMediaMode";

# 设置房间场景模式
# 参数：ROOM_MODE  0 -> 直播模式，1 -> 通信模式，3 -> 游戏模式， 4 -> 语音房间， 5 -> 会议模式
SET_ROOM_MODE = "setRoomMode";

# 设置纯音频模式
SET_AUDIO_MEDIA_MODE = "setAudioMediaMode";

# 房间模式参数
ROOM_MODE = "roomMode";

# 设置音频属性
# 参数：AUDIO_PROFILE  0 -> 默认，1 -> speech，2 -> music延时高， 3 -> music延时低， 4 -> 高音质，5 -> 超高音质
SET_AUDIO_CONFIG = "setAudioConfig";

# 音频属性参数
AUDIO_PROFILE = "audioProfile";

# 远端用户的立体声
# 参数: OPEN  TRUE -> 打开，FALSE -> 关闭
ENABLE_VOICE_POSITION = "enableVoicePosition";

# 获取网络连接状态
GET_CONNECTION_STATUES = "setConnectionStation";

# CPU/内存使用情况
OPEN_DEVICE_STATS_CALLBACK = "openDeviceStatusCallback";

# 打开音频
ENABLE_AUDIO_ENGINE = "enableAudioEngine";

# 关闭音频
DISABLE_AUDIO_ENGINE = "disableAudioEngine";

# 打开扬声器
# 参数: OPEN  TRUE -> 打开，FALSE -> 关闭
ENABLE_LOUD_SPEAKER = "enableLoudspeaker";

# 是否打开扬声器
IS_LOUD_SPEAKER_ENABLE = "isLoudspeakerEnable";

# 打开播放回调
SET_AUDIO_VOLUME_INDICATION = "setAudioVolumeIndication";

# 打开采集回调
ENABLE_CAPTURE_VOLUME_INDICATION = "enableCaptureVolumeIndication";

# 是否静音自己
# 参数：MUTE  False -> 不静音， True -> 静音
STOP_LOCAL_AUDIO_STREAM = "setLocalAudioStream";

# 静音所有播放
STOP_ALL_REMOTE_AUDIO_STREAMS = "stopAllRemoteAudioStreams";

# 打开所有音频播放
START_ALL_REMOTE_AUDIO_STREAMS = "startAllRemoteAudioStreams";

# 静音某个用户
STOP_REMOTE_AUDIO_STREAM = "stopRemoteAudioStream";

# 连麦下，静音所有播放
STOP_ALL_REMOTE_AUDIO_STREAMS_WITH_LIANMAI = "stopAllRemoteAudioStreamsWithLianMai";

# 连麦下，打开所有音频播放
START_ALL_REMOTE_AUDIO_STREAMS_WITH_LIANMAI = "startAllRemoteAudioStreamsWithLianMai";

# 连麦下，静音某个用户
STOP_REMOTE_AUDIO_STREAM_WITH_LIANMAI = "stopRemoteAudioStreamWithLianMai";

# 连麦下，打开某个用户音频
START_REMOTE_AUDIO_STREAM_WITH_LIANMAI = "startRemoteAudioStreamWithLianMai";

# 打开某个用户音频
START_REMOTE_AUDIO_STREAM = "startRemoteAudioStream";

# 麦克风音量
# 参数: VOLUME 音量
SET_MIC_VOLUME = "setMicVolume";

# 扬声器音量
# 参数: VOLUME 音量
SET_LOUDSPEAKER_VOLUME = "setLoudspeakerVolume";

# 指定流的音量
# 参数:  UID 指定的uid
#       VOLUME 音量
SET_REMOTE_AUDIO_STREAMS_VOLUME = "setRemoteAudioStreamVolume";

# 指定流的位置和音量
# 参数:  UID 指定的uid
#       AZIMUTH 方位
#       GAIN 增益
SET_REMOTE_UID_VOICE_POSITION = "seRemoteUidVoicePosition";

# 开始（停止）录制
START_OR_STOP_AUDIO_SAVER = "startOrStopAudioSaver";

# 设置音效模式
# 参数:  MODE 0 -> 关闭模式
#            1 -> VALLEY模式
#            2 -> R&B模式
#            3 -> KTV模式
#            4 -> CHARMING模式
#            5 -> 流行模式
#            6 -> 嘻哈模式
#            7 -> 摇滚模式
#            8 -> 演唱会模式
#            9 -> 录音棚模式
SET_SOUND_EFFECT = "setSoundEffect";

# 设置变声模式
# 参数:  MODE 0 -> 关闭模式
#            1 -> 空灵
#            2 -> 惊悚
#            3 -> 鲁班
#            4 -> 萝莉
#            5 -> 大叔
#            6 -> 死肥仔
#            7 -> 熊孩子
#            8 -> 魔兽农民
#            9 -> 重金属
#            10 -> 感冒
#            11 -> 重机械
#            12 -> 困兽
#            13 -> 强电流
SET_VOICE_CHANGER = "setVoiceChanger";

# 打开渲染回调
ENABLE_RENDER_PCM_DATA_CALLBACK_TEST = "enableRenderPcmDataCallbackTest";

# 耳返
# 参数:  OPEN  TRUE -> 打开，FALSE -> 关闭
SET_ENABLE_IN_EARMONITOR = "setEnableInEarMonitor";

# 设置外部音频外部推流
# 参数:  MUTE  True -> 关闭，False -> 打开
SET_CUSTOM_AUDIO_SOURCE = "setCustomAudioSource";

# 注册音频帧观察者
# 参数:  REGISTERED TRUE -> 注册，FALSE -> 不注册
REGISTER_AUDIO_FRAME_OBEERVER = "registerAudioFrameObserver";

# 修改采集音频帧数据
# 参数:  MODE 1 -> 只读，2 -> 只写，3 -> 读写
SET_RECORDING_AUDIO_FRAME_PARAMETERS = "setRecordingAudioFrameParameters";

# 修改播放音频帧数据
# 参数:  MODE 1 -> 只读，2 -> 只写，3 -> 读写
SET_PLAYBACK_AUDIO_FRAME_PARAMETERS = "setPlaybackAudioFrameParameters";

# 打开音频统计回调
# 参数：TYPE  0 -> 关闭，1 -> 本地，2 -> 远端
OPEN_REMOTE_AUDIO_STATS_CALLBACK = "openRemoteAudioStatsCallback";

# 开启视频模块
ENABLE_VIDEO_ENGINE = "enableVideoEngine";

# 关闭视频模块
DISABLE_VIDEO_ENGINE = "disableVideoEngine";

# 设置视频编码配置
# 参数:  PLAY_TYPE  0 -> 单人开播，1 —> 视频连麦开播，2 -> 录屏开播，3 -> 多人开播
#       PUBLISH_MODE  -1 -> 自动，1 -> 标清，2 -> 高清， 3 -> 超清， 4 -> 蓝光2M，5 -> 蓝光4M
SET_VIDEO_ENCODER_CONFIG = "setVideoEncoderConfig";

# 获取视频编码参数
GET_VIDEO_ENCODER_PARAM = "getVideoEncoderParam";

# 初始化多人连麦布局
SET_MULTIVIEW_PARAMS = "setMultiViewParams";

# 初始化多人连麦布局2
SET_MULTIVIEW_PARAMS2 = "setMultiViewParams2";

# 设置本地预览视图
SET_LOCAL_VIDEO_CANVAS = "setLocalVideoCanvas";

# 设置远端uid预览视图
SET_REMOTE_VIDEO_CANVAS = "setRemoteVideoCanvas";

# 设置远端uid预览视图2预览视图
SET_REMOTE_VIDEO_CANVAS2 = "setRemoteVideoCanvas2";

# 设置基流uid预览视图
SET_REMOTE_VIDEO_CANVAS3 = "setRemoteVideoCanvas3";

# 设置本地视图渲染模式
# MODE 0 -> 拉伸铺满
#      1 -> 自适应
#      2 -> 裁剪铺满
#      3 -> 原画模式
SET_LOCAL_CANVAS_SCALE_MODE = "setLocalCanvasScaleMode";

# 设置远端用户视图渲染模式
# UID 远端uid
# MODE 0 -> 拉伸铺满
#      1 -> 自适应
#      2 -> 裁剪铺满
#      3 -> 原画模式
SET_REMOTE_CANVAS_SCALE_MODE = "setRemoteCanvasScaleMode";

# 打开本地视频采集
ENABLE_LOCAL_VIDEO_CAPTURE = "enableLocalVideoCapture";

# 开启预览
START_VIDEO_PREVIEW = "startVideoPreview";

# 停止预览
STOP_VIDEO_PREVIEW = "stopVideoPreview";

# 打开本地视频发送
START_LOCAL_VIDEO_STREAM = "startLocalVideoStream";

# 关闭本地视频发送
STOP_LOCAL_VIDEO_STREAM = "stopLocalVideoStream";

# 打开指定流播放
START_REMOTE_VIDEO_STREAM = "startRemoteVideoStream";

# 关闭指定流播放
STOP_REMOTE_VIDEO_STREAM = "stopRemoteVideoStream";

# 打开所有远端流播放
START_ALL_REMOTE_VIDEO_STREAMS = "startAllRemoteVideoStreams";

# 关闭所有远端流播放
STOP_ALL_REMOTE_VIDEO_STREAMS = "stopAllRemoteVideoStreams";

# 打开美颜
# 参数:  MUTE TRUE -> 关闭，FALSE -> 打开
ENABLE_VIDEO_BEAUTY = "enableVideoBeauty";

# 打开贴纸
# 参数:  MUTE TRUE -> 关闭，FALSE -> 打开
ENABLE_VIDEO_STICKER = "enableVideoSticker";

# 兼容webSDk
# 参数:  MUTE TRUE -> 关闭，FALSE -> 打开
ENABLE_WEB_SDK_COMPATIBILITY = "enableWenSdkCompatibility";

# 摄像头镜像
# 参数: MODE  0 -> 预览镜像|推流不镜像
#            1 -> 预览镜像|推流镜像
#            2 -> 预览不镜像|推流不镜像
#            3 -> 预览不镜像|推流镜像
SET_LOCAL_VIDEO_MIRROR_MODE = "setLocalVideoMirrorMode";

# 设置横竖屏直播
# 参数:  ORIENTATION 0 -> 竖屏，1 -> 横屏
SET_VIDEO_CAPTURE_ORIENTATION = "setVideoCaptureOrientation";

# 摄像头切换
# 参数:  IS_FRONT TRUE -> 前置摄像头，FALSE -> 后置摄像头
SWITCH_FRONT_CAMERA = "switchFrontCamera";

# 设置外部推流视频源(摄像头)
SET_CUSTOM_VIDEO_SOURCE_WITH_CAMERA = "setCustomVideoSourceWithCamera";

# 设置外部推流视频源(录屏)
SET_CUSTOM_VIDEO_SOURCE_WITH_SCREEN_RECORD = "setCustomVideoSourceWithScreenRecord";

# 添加本地视频水印
SET_VIDEO_WATERMARK = "setVideoWaterMark";

# 设置外部推流视频源(原始流)
# 参数：WIDTH 宽度
#      HEIGHT 高度
#      PIXEL_FORMAT 像素格式 0 -> RGBA，1 -> I420，2 -> NV21
#      ROTATION 旋转角度
SET_CUSTOM_VIDEO_SOURCE_WITH_ORIGIN_YUV_DATA = "setCustomVideoSourceWithOriginYUVData";

# 监听视频解码数据
REGISTER_VIDEO_DECODE_FRAME_OBSERVER = "registerVideoDecodeFrameObserver";

# 打开视频统计回调
# 参数： TYPE  0 -> 关闭，1 -> 本地，2 -> 远端
OPEN_REMOTE_VIDEO_STATS_CALLBACK = "openRemoteVideoStatsCallback";

# 添加源流推流地址
# 参数:  RTMP_URL
ADD_PUBLISH_ORIGIN_STREAM_URL = "addPublishOriginStreamUrl";

# 删除源流推流地址
# 参数:  RTMP_URL
REMOVE_PUBLISH_ORIGIN_STREAM = "removePublishOriginStream";

# 添加/更新转码任务
SET_LIVE_TRANSCODING_TASK = "setLiveTranscodingTask";

# 删除转码任务
# 参数:  TASK_ID
REMOVE_LIVE_TRANSCODING_TASK = "removeLiveTranscodingTask";

# 添加转码流推流地址
# 参数:  TASK_ID
#       URL
#       OPEN  TRUE -> 注入主播系统，FALSE -> 不注入主播系统
ADD_PUBLISH_TRANSCODING_STREAM_URL = "addPublishTrancodingStreamUrl";

# 删除转码流推流地址
# 参数:  TASK_ID
#       URL
REMOVE_PUBLISH_TRANSCODING_STREAM_URL = "removePublishTrancodingStreamUrl";

# 订阅用户的开播流
# 参数:  CHANNEL_ID 频道id
#       UID 用户uid
ADD_SUBSCRIBE = "addSubscribe";

# 删除订阅的用户的开播流
# 参数:  CHANNEL_ID 频道id
#       UID 用户uid
REMOVE_SUBSCRIBE = "removeSubscribe";

# 跨频道音频开播
OTHER_CHANNEL_AUDIO = "otherChannelAudio";

# 跨频道视频开播（包括进频道，视频开播）
OTHER_CHANNEL_VIDEO = "otherChannelVideo";

# 跨频道视频开播（前提是需要初始化和进频道）
OTHER_CHANNEL_VIDEO_WITHOUT_INIT = "otherChannelVideoWithoutInit";

# 跨频道音视频开播
OTHER_CHANNEL_AUDIO_VIDEO = "otherChannelAudioVideo";

# 主播1跨频道订阅主播2
CROSS_CHANNEL_SUBSCRIBE = "crossChannelSubscribe";

# 主播2跨频道订阅主播1
OTHER_CROSS_CHANNEL_SUBSCRIBE = "otherCrossChannelSubscribe";

# 主播1取消跨频道订阅主播2
CANCEL_CROSS_SUBSCRIBE = "cancelCrossSubscribe";

# 主播2取消跨频道订阅主播1
OTHER_CANCEL_CROSS_SUBSCRIBE = "otherCancelCrossSubscribe";

# 跨频道订阅音频
CROSS_CHANNEL_SUBSCRIBE_AUDIO = "crossChannelSubscribeAudio";

# 跨频道订阅视频
CROSS_CHANNEL_SUBSCRIBE_VIDEO = "crossChannelSubscribeVideo";

# 多文件播放
CREATE_AUDIO_FILE_PLAYER = "createAudioFilePlayer";

# 播放
# 参数： TYPE  播放的类型 0 -> mp3, 1 -> aac, 2 -> m4a, 3 -> mp4, 4 -> 3gp, 5 -> mkv, 6 -> http
PLAYER_PLAY = "playerPlay";

# 停止
PLAYER_STOP = "playerStop";

# 暂停
PLAYER_PAUSE = "playerPause";

# 恢复
PLAYER_RESUME = "playerResume";

# 设置播放进度
# 参数： POSITION  进度 millisecond
PLAYER_SEEK = "playerSeek";

# 播放完毕
PLAYER_COMPLETE = "playerComplete";

# 发布模式
# 参数： MODE  0 -> 麦克风, 1 -> 文件, 2 -> 文件+麦克风, 10 -> 空挡
PUBLISH_BY_MODE = "publishByMode";

# 是否发布
ENABLE_PUBLISH = "enablePublish";
# ==========================================SDKCallbackCheck===========================================
UID = "uid";
STATUS = "status";
TYPE = "type";
MUTE = "mute";
STATE = "state"
REASON = "reason"
WIDTH = "width"
HEIGHT = "height"
ERROR_REASON = "errorReason"
ROUTING = "routing";
POSITION = "position";
EVENT = "event";
ERROR_CODE = "errorCode";
PROGRESS = "progress";
MODE = "mode";
PLAY_TYPE = "playType"
PUBLISH_MODE = "publishMode"
PIXEL_FORMAT = "pixelFormat";
ROTATION = "rotation";
OPEN = "open";
VOLUME = "volume";
REGISTERED = "registered";
ORIENTATION = "orientation";
IS_FRONT = "isFront";
RTMP_URL = "rtmpUrl";
TASK_ID = "taskId";
URL = "url";
AZIMUTH = "azimuth";
GAIN = "gain";
AREA_ID = "areaId";
CHANNEL_ID = "channelId";

# 检查是否有多余回调
CHECK_NO_MORE_CALLBACK = "checkNoMoreCallback";

# 参数：STATUS
CHECK_ON_CONNECTION_STATUS = "checkonconnectionstatus";

# 参数：TYPE
CHECK_ON_NETWORKTYPE_CHANGED = "checkonnetworktypechanged";

CHECK_JOIN_ROOM = "checkjoinroom";

CHECK_JOIN_OTHER_ROOM = "checkJoinOtherRoom";

CHECK_ON_USER_JOINED = "checkOnUserJoined";

CHECK_ON_USER_Offline = "checkOnUserOffline";

# 参数：MUTE
CHECK_ON_REMOTE_AUDIO_STOPPED = "checkonremoteaudiostopped";

# 参数：MUTE
CHECK_ON_REMOTE_VIDEO_STOPPED = "checkonremotevideostopped";

# 参数：STATE
#      REASON
CHECK_ON_REMOTE_AUDIO_STATE_CHANGED_OF_UID = "checkonremoteaudiostatechangedofuid";

CHECK_ON_REMOTE_AUDIO_PLAY = "checkonremoteaudioplay";

# 参数：STATE
#      REASON
CHECK_ON_REMOTE_VIDEO_STATE_CHANGED_OF_UID = "checkonremotevideostatechangedofuid";

# 参数：UID
#      WIDTH
#      HEIGHT
CHECK_ON_VIDEO_SIZE_CHANGED = "checkonvideosizechanged";

CHECK_ON_REMOTE_AUDIO_STATS_OF_UID = "checkonremoteaudiostatsofuid";

CHECK_ON_REMOTE_VIDEO_STATS_OF_UID = "checkonremotevideostatsofuid";

# 参数：STATUS
CHECK_ON_AUDIO_CAPTURE_STATUS = "checkonaudiocapturestatus";

# 参数：STATUS
#      ERROR_REASON
CHECK_ON_LOCAL_AUDIO_STATUS_CHANGED = "checkonlocalaudiostatuschanged";

CHECK_ON_FIRST_LOCAL_AUDIO_FRAMESENT = "checkonfirstlocalaudioframesent";

CHECK_ON_DEVICE_STATS = "checkondevicestats";

CHECK_ON_LOCAL_AUDIO_STATS = "checkonlocalaudiostats";

CHECK_ON_ROOM_STATS = "checkonroomstats";

CHECK_ON_LEAVE_ROOM = "checkonleaveroom";

# 参数：STATUS
#      ERROR_REASON
CHECK_ON_LOCAL_VIDEO_STATUS_CHANGED = "checkonlocalvideostatuschanged";

# 参数：STATUS
CHECK_ON_VIDEO_CAPTURE_STATUS = "checkonvideocapturestatus";

CHECK_LOCAL_VIDEO_STATS = "checklocalvideostats";

CHECK_ON_FIRST_LOCAL_VIDEO_FRAMESENT = "checkonfirstlocalvideoframesent";

# 参数：WIDTH
#      HEIGHT
CHECK_ON_REMOTE_VIDEOPLAY = "checkonremotevideoplay";

# 参数：ROUTING
CHECK_ON_AUDIO_ROUTE_CHANGED = "checkOnAudioRouteChanged";

# 参数：TYPE
#      EVENT
#      ERROR_CODE
CHECK_ON_AUDIO_FILE_STATE_CHANGE = "checkOnAudioFileStateChange";

# 参数：TYPE
#      EVENT
#      ERROR_CODE
CHECK_ON_AUDIO_FILE_STATE_CHANGE_PROGRESS = "checkOnAudioFileStateChangeProgress";
