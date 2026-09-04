# XplatApiAutoProbe Android 库的 consumer 混淆规则
#
# 本库通过反射调用被测 SDK 的类与方法（类名由测试客户端在命令帧中下发），
# 混淆会破坏反射查找，宿主工程若开启混淆，请自行对被测 SDK 的类及其公开方法保留：
#   -keep class com.yoursdk.** { public *; }
#
# 库自身无需额外 keep 规则。
