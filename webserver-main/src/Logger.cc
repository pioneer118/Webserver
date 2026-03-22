#include "Logger.h"        // 包含头文件
#include "CurrentThread.h" // 线程局部存储工具（本文件未直接使用，为扩展预留）

// 线程局部存储命名空间：每个线程独立的缓冲区/变量，避免多线程竞争
namespace ThreadInfo
{
    thread_local char t_errnobuf[512]; // 存储错误信息的缓冲区（每个线程一份）
    thread_local char t_timer[64];     // 存储格式化时间的缓冲区（每个线程一份）
    thread_local time_t t_lastSecond;  // 记录上次格式化时间的秒数（减少重复计算）
}

// 获取错误码对应的描述字符串（线程安全版）
// 参数：保存的错误码（如errno）
// 返回：错误描述字符串（如"Connection refused"）
const char *getErrnoMsg(int savedErrno)
{
    // strerror_r：线程安全的strerror，将错误描述写入t_errnobuf
    return strerror_r(savedErrno, ThreadInfo::t_errnobuf, sizeof(ThreadInfo::t_errnobuf));
}

// 日志等级名称数组：与Logger::LogLevel枚举一一对应（方便快速查找）
const char *getLevelName[Logger::LogLevel::LEVEL_COUNT]{
    "TRACE ", // 长度6（与其他等级对齐，保证日志格式整齐）
    "DEBUG ",
    "INFO  ",
    "WARN  ",
    "ERROR ",
    "FATAL ",
};

/**
 * 默认日志输出函数：将日志写入标准输出（stdout）
 * 参数：
 *   data - 日志数据指针
 *   len  - 日志数据长度
 */
static void defaultOutput(const char *data, int len)
{
    // fwrite：按字节写入，len个char，每个char占1字节
    fwrite(data, len, sizeof(char), stdout);
}

/**
 * 默认刷新函数：刷新标准输出缓冲区（确保日志立即显示）
 * 场景：FATAL错误时强制刷新，避免日志滞留缓冲区
 */
static void defaultFlush()
{
    fflush(stdout); // 刷新stdout缓冲区到终端
}

// 全局日志输出回调（默认指向defaultOutput）
Logger::OutputFunc g_output = defaultOutput;
// 全局日志刷新回调（默认指向defaultFlush）
Logger::FlushFunc g_flush = defaultFlush;

// Logger::Impl构造函数：初始化日志头部
// 参数：level-日志等级，savedErrno-错误码，filename-文件路径，line-行号
Logger::Impl::Impl(Logger::LogLevel level, int savedErrno, const char *filename, int line)
    : time_(Timestamp::now()), // 记录当前时间戳
      stream_(),               // 初始化日志流
      level_(level),           // 日志等级
      line_(line),             // 行号
      basename_(filename)      // 提取纯文件名（SourceFile构造）
{
    formatTime(); // 格式化时间字符串，拼接到日志开头
    // 写入日志等级（如"INFO  "），GeneralTemplate保证固定长度6
    stream_ << GeneralTemplate(getLevelName[level], 6);
    // 如果有错误码，拼接错误信息（如"Connection refused (errno=111) "）
    if (savedErrno != 0)
    {
        stream_ << getErrnoMsg(savedErrno) << " (errno=" << savedErrno << ") ";
    }
}

// 格式化时间字符串（拼接到日志开头）
// 输出格式：YYYY/MM/DD HH:MM:SS + 微秒（如2025/03/20 15:30:20 123456）
void Logger::Impl::formatTime()
{
    Timestamp now = Timestamp::now(); // 获取当前时间戳（微秒级）
    // 转换为秒数（1秒=1e6微秒）
    time_t seconds = static_cast<time_t>(now.microSecondsSinceEpoch() / Timestamp::kMicroSecondsPerSecond);
    // 剩余微秒数（0~999999）
    int microseconds = static_cast<int>(now.microSecondsSinceEpoch() % Timestamp::kMicroSecondsPerSecond);

    // 转换为本地时间（tm结构体：年/月/日/时/分/秒）
    struct tm *tm_timer = localtime(&seconds);

    // 格式化时间到t_timer缓冲区（格式：YYYY/MM/DD HH:MM:SS）
    snprintf(ThreadInfo::t_timer, sizeof(ThreadInfo::t_timer), "%4d/%02d/%02d %02d:%02d:%02d",
             tm_timer->tm_year + 1900, // 年份：tm_year是从1900开始的偏移量
             tm_timer->tm_mon + 1,     // 月份：tm_mon是0~11，+1转为1~12
             tm_timer->tm_mday,        // 日期：1~31
             tm_timer->tm_hour,        // 小时：0~23
             tm_timer->tm_min,         // 分钟：0~59
             tm_timer->tm_sec);        // 秒：0~59
    // 更新上次格式化时间的秒数（减少后续重复计算）
    ThreadInfo::t_lastSecond = seconds;

    // 格式化微秒数（6位，不足补0）
    char buf[32] = {0};
    snprintf(buf, sizeof(buf), "%06d ", microseconds);

    // 将时间字符串拼接到日志流：
    // GeneralTemplate(t_timer,17) → 固定17字符（YYYY/MM/DD HH:MM:SS）
    // GeneralTemplate(buf,7) → 固定7字符（微秒+空格）
    stream_ << GeneralTemplate(ThreadInfo::t_timer, 17) << GeneralTemplate(buf, 7);
}

// 完成日志拼接：添加文件名+行号+换行
void Logger::Impl::finish()
{
    // 格式：" - 文件名:行号\n"
    // 示例：" - Logger.cc:45\n"
    stream_ << " - " << GeneralTemplate(basename_.data_, basename_.size_)
            << ':' << line_ << '\n';
}

// Logger构造函数：创建Impl对象（无错误码）
// 参数：filename-文件路径，line-行号，level-日志等级
Logger::Logger(const char *filename, int line, LogLevel level) : impl_(level, 0, filename, line)
{
    // 核心逻辑在Impl构造函数中，此处仅初始化impl_
}

// Logger析构函数：触发日志输出（核心）
Logger::~Logger()
{
    impl_.finish(); // 拼接日志尾部（文件名+行号+换行）
    // 获取日志流的缓冲区（包含完整日志内容）
    const LogStream::Buffer &buffer = stream().buffer();
    // 调用全局输出函数（默认输出到终端，可自定义为文件/网络）
    g_output(buffer.data(), buffer.length());

    // 如果是FATAL等级：强制刷新缓冲区 + 终止程序
    if (impl_.level_ == FATAL)
    {
        g_flush(); // 确保FATAL日志立即输出
        abort();   // 终止程序（生成core dump，便于调试）
    }
}

// 设置自定义日志输出函数（替换g_output）
void Logger::setOutput(OutputFunc out)
{
    g_output = out;
}

// 设置自定义日志刷新函数（替换g_flush）
void Logger::setFlush(FlushFunc flush)
{
    g_flush = flush;
}
