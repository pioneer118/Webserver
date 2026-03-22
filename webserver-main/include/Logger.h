#pragma once // 防止头文件重复包含（替代传统的#ifndef+#define+#endif）

#include <stdio.h>     // 标准输入输出（如fflush/stdout）
#include <string.h>    // 字符串操作（如strrchr/strlen）
#include <string>      // std::string类
#include <errno.h>     // 错误码定义（如errno）
#include "LogStream.h" // 日志流处理（封装缓冲区和输出运算符）
#include <functional>  // 函数对象（std::function，用于日志输出/刷新回调）
#include "Timestamp.h" // 时间戳工具类（获取当前时间）

#define OPEN_LOGGING // 日志功能开关（定义后启用LOG_XXX宏）

// SourceFile类：提取文件名（剥离路径，只保留纯文件名）
class SourceFile
{
public:
    // 构造函数：传入完整文件路径（如"/home/test/Logger.cc"）
    explicit SourceFile(const char *filename)
        : data_(filename) // 初始化data_为完整路径
    {
        /**
         * strrchr：从右往左查找第一个'/'的位置
         * 示例："2022/10/26/test.log" → 找到最后一个'/'，data_指向"test.log"
         * 若没有'/'（如"Logger.cc"），则data_保持原指针
         */
        const char *slash = strrchr(filename, '/');
        if (slash)
        {
            data_ = slash + 1; // 跳过'/'，指向纯文件名
        }
        size_ = static_cast<int>(strlen(data_)); // 纯文件名长度（如"Logger.cc"长度为8）
    }

    const char *data_; // 纯文件名指针（如"Logger.cc"）
    int size_;         // 纯文件名长度
};

class Logger
{
public:
    // 日志等级枚举（从低到高，FATAL为致命错误，触发程序终止）
    enum LogLevel
    {
        TRACE,       // 追踪（最细粒度，调试用）
        DEBUG,       // 调试（开发阶段调试信息）
        INFO,        // 信息（正常运行状态）
        WARN,        // 警告（非致命问题）
        ERROR,       // 错误（功能异常，但程序可继续）
        FATAL,       // 致命（核心错误，程序终止）
        LEVEL_COUNT, // 等级总数（用于数组边界检查）
    };

    // 构造函数：创建日志对象（指定文件、行号、日志等级）
    Logger(const char *filename, int line, LogLevel level);
    // 析构函数：完成日志拼接，触发输出（核心逻辑）
    ~Logger();

    // 获取日志流对象（用于拼接日志内容，如LOG_INFO << "xxx"）
    // 流对象会修改内部状态，因此返回非const引用
    LogStream &stream() { return impl_.stream_; }

    // 定义日志输出/刷新回调类型（可自定义输出目标：终端/文件/网络）
    using OutputFunc = std::function<void(const char *msg, int len)>;
    using FlushFunc = std::function<void()>;
    // 设置自定义输出函数（替换默认的终端输出）
    static void setOutput(OutputFunc);
    // 设置自定义刷新函数（替换默认的fflush(stdout)）
    static void setFlush(FlushFunc);

private:
    // 内部实现类（Pimpl模式：隐藏实现细节，减少头文件依赖）
    class Impl
    {
    public:
        // 复用外部Logger的LogLevel枚举
        using LogLevel = Logger::LogLevel;

        // Impl构造函数：初始化日志头部（时间+等级+错误信息）
        // 参数：日志等级、错误码（0表示无错误）、文件路径、行号
        Impl(LogLevel level, int savedErrno, const char *filename, int line);

        // 格式化时间字符串（拼接到日志开头）
        void formatTime();
        // 完成日志拼接（添加文件名+行号+换行）
        void finish();

        Timestamp time_;      // 日志生成时间戳
        LogStream stream_;    // 日志流（拼接日志内容）
        LogLevel level_;      // 日志等级
        int line_;            // 日志所在行号
        SourceFile basename_; // 日志所在文件（纯文件名）
    };

private:
    Impl impl_; // 内部实现对象（Logger的核心逻辑都在Impl中）
};

// 获取系统错误码对应的描述字符串（线程安全版）
const char *getErrnoMsg(int savedErrno);

/**
 * 日志宏定义：简化调用，自动填充文件/行号
 * 原理：创建Logger临时对象 → 调用stream()获取日志流 → 拼接内容 → 析构时输出
 * 示例：LOG_INFO << "Server start on port " << 8080;
 */
#ifdef OPEN_LOGGING
#define LOG_DEBUG Logger(__FILE__, __LINE__, Logger::DEBUG).stream()
#define LOG_INFO Logger(__FILE__, __LINE__, Logger::INFO).stream()
#define LOG_WARN Logger(__FILE__, __LINE__, Logger::WARN).stream()
#define LOG_ERROR Logger(__FILE__, __LINE__, Logger::ERROR).stream()
#define LOG_FATAL Logger(__FILE__, __LINE__, Logger::FATAL).stream()
#else
// 关闭日志时，返回空的LogStream（无输出）
#define LOG(level) LogStream()
#endif