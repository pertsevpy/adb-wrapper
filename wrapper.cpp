#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;

// ============================================================
// Helpers
// ============================================================

static std::wstring GetTimestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    std::wstringstream ss;
    ss << std::setfill(L'0')
       << st.wYear << L'-'
       << std::setw(2) << st.wMonth << L'-'
       << std::setw(2) << st.wDay << L' '
       << std::setw(2) << st.wHour << L':'
       << std::setw(2) << st.wMinute << L':'
       << std::setw(2) << st.wSecond << L'.'
       << std::setw(3) << st.wMilliseconds;

    return ss.str();
}

static std::wstring QuoteArg(const std::wstring& arg)
{
    // Простое и корректное quoting для логирования.
    // Сам процесс получает argv через обычную Windows command line.
    if (arg.empty())
        return L"\"\"";

    bool needQuotes = false;

    for (wchar_t c : arg)
    {
        if (c == L' ' || c == L'\t' || c == L'"')
        {
            needQuotes = true;
            break;
        }
    }

    if (!needQuotes)
        return arg;

    std::wstring result;
    result += L'"';

    size_t backslashes = 0;

    for (wchar_t c : arg)
    {
        if (c == L'\\')
        {
            ++backslashes;
            continue;
        }

        if (c == L'"')
        {
            // Перед кавычкой все backslash нужно удвоить,
            // плюс один backslash для экранирования самой кавычки.
            result.append(backslashes * 2 + 1, L'\\');
            result += L'"';
            backslashes = 0;
            continue;
        }

        result.append(backslashes, L'\\');
        backslashes = 0;
        result += c;
    }

    // Перед закрывающей кавычкой backslash удваиваются.
    result.append(backslashes * 2, L'\\');
    result += L'"';

    return result;
}

static std::wstring GetParsedCommandLine()
{
    int argc = 0;

    LPWSTR* argv = CommandLineToArgvW(
        GetCommandLineW(),
        &argc
    );

    if (!argv)
        return L"<CommandLineToArgvW failed>";

    std::wstring result;

    for (int i = 0; i < argc; ++i)
    {
        if (i != 0)
            result += L' ';

        result += QuoteArg(argv[i]);
    }

    LocalFree(argv);

    return result;
}

static std::wstring GetExecutableName()
{
    wchar_t buffer[MAX_PATH]{};

    DWORD len = GetModuleFileNameW(
        nullptr,
        buffer,
        MAX_PATH
    );

    if (len == 0)
        return L"unknown.exe";

    return fs::path(buffer).filename().wstring();
}

static fs::path GetExecutableDirectory()
{
    wchar_t buffer[MAX_PATH]{};

    DWORD len = GetModuleFileNameW(
        nullptr,
        buffer,
        MAX_PATH
    );

    if (len == 0)
        return fs::current_path();

    return fs::path(std::wstring(buffer, len)).parent_path();
}

static std::wstring GetCurrentDirectoryString()
{
    DWORD size = GetCurrentDirectoryW(0, nullptr);

    if (size == 0)
        return L"<GetCurrentDirectory failed>";

    std::vector<wchar_t> buffer(size);

    DWORD result = GetCurrentDirectoryW(
        size,
        buffer.data()
    );

    if (result == 0)
        return L"<GetCurrentDirectory failed>";

    return std::wstring(buffer.data(), result);
}

static void LogLine(
    const fs::path& logFile,
    const std::wstring& line
)
{
    // UTF-8 лог.
    std::wofstream file(
        logFile,
        std::ios::app
    );

    if (!file)
        return;

    file.imbue(
        std::locale(
            "",
            std::locale::ctype
        )
    );

    file << line << L'\n';
}

// ============================================================
// Main
// ============================================================

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int
)
{
    const DWORD wrapperPid = GetCurrentProcessId();

    const fs::path wrapperDir = GetExecutableDirectory();
    const std::wstring exeName = GetExecutableName();

    // --------------------------------------------------------
    // Определяем, под каким именем нас запустили:
    //
    // adb.exe       -> adb.real.exe
    // fastboot.exe  -> fastboot.real.exe
    // --------------------------------------------------------

    std::wstring lowerName = exeName;

    for (auto& c : lowerName)
        c = static_cast<wchar_t>(towlower(c));

    fs::path realExecutable;

    if (lowerName == L"adb.exe")
    {
        realExecutable = wrapperDir / L"adb.real.exe";
    }
    else if (lowerName == L"fastboot.exe")
    {
        realExecutable = wrapperDir / L"fastboot.real.exe";
    }
    else
    {
        // На случай запуска wrapper.exe вручную.
        // Это удобно для диагностики.
        realExecutable = wrapperDir / L"adb.real.exe";
    }

    // --------------------------------------------------------
    // Log file
    // --------------------------------------------------------

    const fs::path logFile = wrapperDir / L"adb_trace.log";

    // --------------------------------------------------------
    // Получаем исходную командную строку.
    //
    // Это важно: здесь сохраняется именно тот raw command line,
    // который получил wrapper.
    // --------------------------------------------------------

    const std::wstring rawCommandLine =
        GetCommandLineW();

    const std::wstring parsedCommandLine =
        GetParsedCommandLine();

    const std::wstring cwd =
        GetCurrentDirectoryString();

    // --------------------------------------------------------
    // Проверяем существование оригинального executable
    // --------------------------------------------------------

    if (!fs::exists(realExecutable))
    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" ERROR=real_executable_not_found"
           << L" PATH=\"" << realExecutable.wstring() << L"\"";

        LogLine(logFile, ss.str());

        return ERROR_FILE_NOT_FOUND;
    }

    // --------------------------------------------------------
    // Получаем стандартные дескрипторы текущего процесса.
    //
    // Если родительское приложение запустило нас с redirected stdin /
    // stdout / stderr, здесь будут именно его pipe handles.
    //
    // Мы НЕ создаём собственные pipe и НЕ читаем эти потоки.
    // Просто передаём их дальше дочернему процессу.
    // --------------------------------------------------------

    HANDLE hStdIn =
        GetStdHandle(STD_INPUT_HANDLE);

    HANDLE hStdOut =
        GetStdHandle(STD_OUTPUT_HANDLE);

    HANDLE hStdErr =
        GetStdHandle(STD_ERROR_HANDLE);

    // --------------------------------------------------------
    // Проверяем возможность наследования.
    //
    // Для обычного запуска это не меняет поведение.
    // Если дескриптор существует, делаем его inheritable.
    // --------------------------------------------------------

    auto MakeInheritable = [](HANDLE h)
    {
        if (h == nullptr ||
            h == INVALID_HANDLE_VALUE)
        {
            return;
        }

        SetHandleInformation(
            h,
            HANDLE_FLAG_INHERIT,
            HANDLE_FLAG_INHERIT
        );
    };

    MakeInheritable(hStdIn);
    MakeInheritable(hStdOut);
    MakeInheritable(hStdErr);

    // --------------------------------------------------------
    // Формируем командную строку дочернего процесса.
    //
    // Важно: имя executable указывается явно через
    // lpApplicationName, поэтому не зависит от PATH.
    //
    // Аргументы берём из исходной командной строки wrapper.
    // --------------------------------------------------------

    int argc = 0;

    LPWSTR* argv = CommandLineToArgvW(
        rawCommandLine.c_str(),
        &argc
    );

    if (!argv)
    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" ERROR=CommandLineToArgvW"
           << L" CODE=" << GetLastError();

        LogLine(logFile, ss.str());

        return ERROR_INVALID_PARAMETER;
    }

    std::wstring childCommandLine;

    // Первый argv — имя wrapper.
    // Для дочернего процесса вместо него используем
    // имя реального executable.

    childCommandLine += QuoteArg(
        realExecutable.wstring()
    );

    for (int i = 1; i < argc; ++i)
    {
        childCommandLine += L' ';
        childCommandLine += QuoteArg(argv[i]);
    }

    LocalFree(argv);

    // --------------------------------------------------------
    // CreateProcessW требует writable command line buffer.
    // --------------------------------------------------------

    std::vector<wchar_t> commandBuffer(
        childCommandLine.begin(),
        childCommandLine.end()
    );

    commandBuffer.push_back(L'\0');

    // --------------------------------------------------------
    // STARTUPINFO
    // --------------------------------------------------------

    STARTUPINFOW si{};
    si.cb = sizeof(si);

    si.dwFlags |= STARTF_USESTDHANDLES;

    si.hStdInput  = hStdIn;
    si.hStdOutput = hStdOut;
    si.hStdError  = hStdErr;

    PROCESS_INFORMATION pi{};

    // --------------------------------------------------------
    // Log START
    // --------------------------------------------------------

    {
        std::wstringstream ss;

        ss << L""
           << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" START"
           << L" TYPE=" << lowerName
           << L" CWD=\"" << cwd << L"\""
           << L" REAL=\"" << realExecutable.wstring() << L"\""
           << L" RAW_CMD=\"" << rawCommandLine << L"\""
           << L" CMD=\"" << parsedCommandLine << L"\"";

        LogLine(logFile, ss.str());
    }

    // --------------------------------------------------------
    // CreateProcess
    //
    // nullptr environment:
    //   наследуем environment текущего процесса.
    //
    // nullptr current directory:
    //   наследуем CWD родителя.
    //
    // TRUE:
    //   разрешаем наследование std handles.
    // --------------------------------------------------------

    BOOL created = CreateProcessW(
        realExecutable.wstring().c_str(),
        commandBuffer.data(),

        nullptr,    // lpProcessAttributes
        nullptr,    // lpThreadAttributes

        TRUE,       // bInheritHandles

        0,          // dwCreationFlags

        nullptr,    // lpEnvironment
        nullptr,    // lpCurrentDirectory

        &si,
        &pi
    );

    if (!created)
    {
        const DWORD error = GetLastError();

        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CREATE_PROCESS_FAILED"
           << L" CODE=" << error
           << L" REAL=\"" << realExecutable.wstring() << L"\"";

        LogLine(logFile, ss.str());

        return static_cast<int>(error);
    }

    // --------------------------------------------------------
    // Теперь оригинальный adb/fastboot запущен.
    // --------------------------------------------------------

    const DWORD childPid = pi.dwProcessId;

    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID=" << childPid
           << L" RUNNING";

        LogLine(logFile, ss.str());
    }

    // Thread handle больше не нужен.
    CloseHandle(pi.hThread);

    // --------------------------------------------------------
    // Ждём завершения adb/fastboot.
    //
    // НИЧЕГО не читаем из stdin/stdout/stderr.
    //
    // Потоки напрямую подключены к дочернему процессу.
    // --------------------------------------------------------

    DWORD waitResult = WaitForSingleObject(
        pi.hProcess,
        INFINITE
    );

    if (waitResult != WAIT_OBJECT_0)
    {
        const DWORD error = GetLastError();

        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID=" << childPid
           << L" WAIT_FAILED"
           << L" CODE=" << error;

        LogLine(logFile, ss.str());

        CloseHandle(pi.hProcess);

        return static_cast<int>(error);
    }

    // --------------------------------------------------------
    // Получаем exit code оригинального процесса.
    // --------------------------------------------------------

    DWORD exitCode = 0;

    if (!GetExitCodeProcess(
            pi.hProcess,
            &exitCode))
    {
        const DWORD error = GetLastError();

        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID=" << childPid
           << L" GET_EXIT_CODE_FAILED"
           << L" CODE=" << error;

        LogLine(logFile, ss.str());

        CloseHandle(pi.hProcess);

        return static_cast<int>(error);
    }

    CloseHandle(pi.hProcess);

    // --------------------------------------------------------
    // Финальный лог.
    // --------------------------------------------------------

    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID=" << childPid
           << L" EXIT=" << exitCode;

        LogLine(logFile, ss.str());
    }

    // --------------------------------------------------------
    //
    // Возвращаем ровно exit code adb/fastboot.
    //
    // Приложение увидит тот же код, который получило бы
    // при непосредственном запуске оригинального executable.
    // --------------------------------------------------------

    return static_cast<int>(exitCode);
}
