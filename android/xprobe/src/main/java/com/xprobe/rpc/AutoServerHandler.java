package com.xprobe.rpc;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;

import com.xprobe.rpc.exception.ExeCmdException;
import com.xprobe.rpc.exception.InvalidFormatException;
import com.xprobe.rpc.metadata.CmdType;
import com.xprobe.rpc.metadata.Command;
import com.xprobe.rpc.metadata.Parameter;
import com.xprobe.rpc.metadata.ThreadMode;
import com.xprobe.rpc.protocol.CmdMessage;
import com.xprobe.rpc.utils.ExceptionUtil;
import com.xprobe.rpc.utils.LogUtils;
import com.xprobe.rpc.utils.ObjectHelper;
import com.xprobe.rpc.utils.ProtocolUtil;

import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 命令执行处理器：
 * <ul>
 * <li>解析 JSON 命令帧为命令列表；</li>
 * <li>按线程模式派发：MAIN 投递主线程 Handler，BACKGROUND 提交固定 8 线程池；</li>
 * <li>连续相同 threadMode 的命令合并为一组，组内按顺序执行；组间可能并发；</li>
 * <li>反射执行：静态方法优先，其次 ObjectManager 缓存实例；异常经 sendErrorInfo 返回堆栈。</li>
 * </ul>
 */
public class AutoServerHandler extends BaseHandler {

    // 解析错误回包使用的 key（线协议约定）
    public static final String KEY_PARSE_ERROR = "parseError";

    // 固定 8 线程的工作线程池，超出大小的任务排队等待
    private final ExecutorService mThreadPool = Executors.newFixedThreadPool(8);
    private final Handler mMainHandler;

    public AutoServerHandler(Context ctx) {
        // 主线程 Handler：仅构造时使用 Context，不持有引用
        mMainHandler = new MainThreadHandler(ctx.getMainLooper());
    }

    /**
     * 停止处理器：关闭线程池并清理主线程待执行消息
     */
    public void shutdown() {
        mThreadPool.shutdownNow();
        mMainHandler.removeCallbacksAndMessages(null);
    }

    @Override
    protected void handleData(TcpChannel channel, CmdMessage msg) {
        LogUtils.i("received: " + msg + " from: " + channel.getRemoteAddress());
        try {
            processMsg(msg.getContent());
        } catch (Exception e) { // 命令解析 / 派发本身出错，断开该连接以免解析状态错乱
            LogUtils.e(ExceptionUtil.getExceptionStack(e));
            channel.close();
        }
    }

    /**
     * 解析并派发命令帧
     */
    private void processMsg(String msg) {
        List<Command> commands;
        try {
            commands = ProtocolUtil.parseMethodInfo(msg);
        } catch (InvalidFormatException e) {
            LogUtils.e(ExceptionUtil.getExceptionStack(e));
            ChannelCache.sendErrorInfo(KEY_PARSE_ERROR, ExceptionUtil.getShortExceptionStack(e));
            return; // 解析失败，退出执行
        }
        exeCommand(commands);
    }

    /**
     * 派发命令：把同一帧内「连续的同线程模式」命令合并为一个任务，
     * 组内顺序执行；线程模式变化时切换任务（组间可能并发）
     */
    private void exeCommand(List<Command> commands) {
        int i = 0;
        while (i < commands.size()) {
            ThreadMode mode = commands.get(i).getThreadMode();
            int j = i;
            while (j < commands.size() && commands.get(j).getThreadMode() == mode) {
                j++;
            }
            List<Command> group = new ArrayList<>(commands.subList(i, j));
            // 关键路径打点：命令派发（组内命令数 + 线程模式）
            LogUtils.i("dispatch " + group.size() + " command(s), thread mode: " + mode);
            if (mode == ThreadMode.MAIN) {
                // 主线程执行
                Message message = Message.obtain();
                message.obj = group;
                mMainHandler.sendMessage(message);
            } else {
                // 子线程执行
                mThreadPool.execute(new CommandTask(group));
            }
            i = j;
        }
    }

    /**
     * 单组命令的执行任务：按序执行组内全部命令
     */
    private class CommandTask implements Runnable {

        private final List<Command> mCommands;

        CommandTask(List<Command> commands) {
            mCommands = commands;
        }

        @Override
        public void run() {
            for (Command cmd : mCommands) {
                executeSingle(cmd);
            }
        }
    }

    /**
     * 执行单条命令（方法调用或成员变量修改），异常经 sendErrorInfo 返回堆栈
     */
    private void executeSingle(Command cmd) {
        try {
            if (cmd.getCmdType() == CmdType.METHOD_TYPE) { // 执行方法
                exeMethod(cmd);
            } else if (cmd.getCmdType() == CmdType.FILED_TYPE) { // 修改成员变量
                exeSetFiled(cmd);
            }
        } catch (Exception e) {
            // 按线协议 §5 回 error 帧后继续服务：被测 SDK 抛异常是探针的观测结果，
            // 不应中断连接，更不能杀死宿主进程（宿主可能是正在跑用例的业务 App）
            String stack = ExceptionUtil.getExceptionStack(e);
            LogUtils.e(stack);
            ChannelCache.sendErrorInfo(cmd.getCommandName(), stack);
        }
    }

    /**
     * 执行方法：
     * ver=2 交给 CustomInvocation；否则反射执行（静态方法优先，其次对象缓存中的实例）
     */
    private void exeMethod(Command method) throws Exception {
        if (method.getVersion() == 2) { // 将方法调用交给上层业务处理
            CustomInvocation invocation = ObjectHelper.getInstance().getCustomInvocation();
            if (invocation == null) {
                ChannelCache.sendErrorInfo("exeError", "CustomInvocation对象未初始化");
            } else {
                invocation.callMethod(method.getCommandName(), method.getParamsMap());
            }
            return;
        }

        String className;
        String methodName = method.getCommandName();
        int index = method.getCommandName().lastIndexOf(".");
        if (index > 0) {
            // 取最后一个 "." 之前为类名，其后为方法名
            className = method.getCommandName().substring(0, index);
            methodName = method.getCommandName().substring(index + 1);
        } else {
            // 不含 "." 时，使用 ObjectManager 提供的默认包名
            BaseObjectManager manager = ObjectHelper.getInstance().getObjecManager();
            className = manager == null ? null : manager.getSDKPackageName();
        }

        try {
            // 静态方法优先
            Class<?> clazz = Class.forName(className);
            Class<?>[] argTypes = method.getClassArray();
            if (argTypes == null) {
                argTypes = new Class<?>[0];
            }
            Method staticMethod = clazz.getDeclaredMethod(methodName, argTypes);
            if (staticMethod != null && Modifier.isStatic(staticMethod.getModifiers())) {
                // 静态方法不需要实例
                LogUtils.i("invoke method:" + method.getCommandName());
                Object[] argValues = method.getParamValues();
                if (argValues == null) {
                    argValues = new Object[0];
                }
                Object result = staticMethod.invoke(null, argValues);
                sendInvokeResult(method, staticMethod.getReturnType(), result);
                return;
            }
        } catch (Exception ex) {
            // 类或静态方法不存在，继续尝试对象缓存中的实例
            LogUtils.e(String.valueOf(ex.getMessage()));
        }

        // 从对象缓存中取已存在的对象
        Object obj;
        Class<?> cls;
        try {
            obj = ObjectHelper.getInstance().getObjecManager().getObject(className);
            cls = obj.getClass();
        } catch (Exception e) {
            // 异常后判断是否静态方法调用
            BaseObjectManager manager = ObjectHelper.getInstance().getObjecManager();
            String errMsg = manager != null && manager.isInitialize()
                    ? ExceptionUtil.getShortExceptionStack(e) : "sdk did not initialize";
            throw new ExeCmdException(methodName, errMsg);
        }

        // step1: 查找实例方法
        Class<?>[] instArgTypes = method.getClassArray();
        if (instArgTypes == null) {
            instArgTypes = new Class<?>[0];
        }
        Method declaredMethod = cls.getDeclaredMethod(methodName, instArgTypes);

        // step2: 反射执行
        LogUtils.i("invoke method:" + method.getCommandName());
        Object[] instArgValues = method.getParamValues();
        if (instArgValues == null) {
            instArgValues = new Object[0];
        }
        Object result = declaredMethod.invoke(obj, instArgValues);
        sendInvokeResult(method, declaredMethod.getReturnType(), result);
    }

    /**
     * 回传方法调用结果：void 方法返回 "void"，null 返回 "null"，其余取 toString()
     */
    private void sendInvokeResult(Command method, Class<?> returnType, Object result) {
        if (returnType == void.class) {
            ChannelCache.sendMethodReturn(method.getCommandName(), "void");
        } else {
            LogUtils.i(String.format("api:%s result:%s", method,
                    result == null ? "null" : result.toString()));
            ChannelCache.sendMethodReturn(method.getCommandName(),
                    result == null ? "null" : result.toString());
        }
    }

    /**
     * 修改类的静态成员变量；每改一个字段回一条 return（value=void），
     * 与 iOS setter 回包粒度对齐，便于客户端按字段数 wait_response。
     */
    private void exeSetFiled(Command command) throws Exception {
        Class<?> filedClass = Class.forName(command.getCommandName());
        for (Parameter param : command.getParameterList()) {
            Field nameField = filedClass.getDeclaredField(param.getName());
            nameField.setAccessible(true);
            nameField.set(filedClass, param.getTargetTypeValue());
            LogUtils.i(String.format("set %s field:%s", command.getCommandName(), param.getName()));
            ChannelCache.sendMethodReturn(param.getName(), "void");
        }
    }

    /**
     * 主线程命令执行 Handler：按序执行同一组内的命令
     */
    private class MainThreadHandler extends Handler {

        MainThreadHandler(Looper looper) {
            super(looper);
        }

        @Override
        @SuppressWarnings("unchecked")
        public void handleMessage(Message msg) {
            List<Command> commands = (List<Command>) msg.obj;
            for (Command cmd : commands) {
                executeSingle(cmd);
            }
        }
    }
}
