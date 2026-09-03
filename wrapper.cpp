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

// ------------------------------------------------------------
// Получить имя текущего EXE.
// Один wrapper используется и как adb.exe, и как fastboot.exe.
// ------------------------------------------------------------
static std::wstring GetExeName()
{
    wchar_t buffer[MAX_PATH]{};

    DWORD len = GetModuleFileNameW(
        nullptr,
        buffer,
        MAX_PATH
    );

    if (len == 0 || len >= MAX_PATH)
        return L"unknown.exe";

    return fs::path(buffer).filename().wstring();
}

// ------------------------------------------------------------
// Каталог, в котором находится wrapper.
// ------------------------------------------------------------
static fs::path GetWrapperDirectory()
{
    wchar_t buffer[MAX_PATH]{};

    DWORD len = GetModuleFileNameW(
        nullptr,
        buffer,
        MAX_PATH
    );

    if (len == 0 || len >= MAX_PATH)
        return fs::current_path();

    return fs::path(buffer).parent_path();
}

// ------------------------------------------------------------
// Текущий рабочий каталог процесса.
// ------------------------------------------------------------
static std::wstring GetCurrentDirectoryString()
{
    DWORD size = GetCurrentDirectoryW(0, nullptr);

    if (size == 0)
        return L"";

    std::vector<wchar_t> buffer(size);

    DWORD result = GetCurrentDirectoryW(
        size,
        buffer.data()
    );

    if (result == 0)
        return L"";

    return std::wstring(buffer.data(), result);
}

// ------------------------------------------------------------
// Timestamp с миллисекундами.
// ------------------------------------------------------------
static std::wstring Timestamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    std::wstringstream ss;

    ss << std::setfill(L'0')
       << std::setw(4) << st.wYear << L"-"
       << std::setw(2) << st.wMonth << L"-"
       << std::setw(2) << st.wDay << L" "
       << std::setw(2) << st.wHour << L":"
       << std::setw(2) << st.wMinute << L":"
       << std::setw(2) << st.wSecond << L"."
       << std::setw(3) << st.wMilliseconds;

    return ss.str();
}

// ------------------------------------------------------------
// Преобразование аргумента в безопасную Windows command-line
// форму.
// ------------------------------------------------------------
static std::wstring QuoteArgument(const std::wstring& arg)
{
    if (arg.empty())
        return L"\"\"";

    bool needQuotes = false;

    for (wchar_t c : arg)
    {
        if (c == L' ' ||
            c == L'\t' ||
            c == L'"')
        {
            needQuotes = true;
            break;
        }
    }

    if (!needQuotes)
        return arg;

    std::wstring result = L"\"";

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
            result.append(backslashes * 2 + 1, L'\\');
            result += L'"';
            backslashes = 0;
            continue;
        }

        result.append(backslashes, L'\\');
        backslashes = 0;
        result += c;
    }

    // Backslashes перед закрывающей кавычкой удваиваются.
    result.append(backslashes * 2, L'\\');

    result += L'"';

    return result;
}

// ------------------------------------------------------------
// Получить аргументы текущего процесса.
// ------------------------------------------------------------
static std::wstring GetArguments()
{
    int argc = 0;

    LPWSTR* argv = CommandLineToArgvW(
        GetCommandLineW(),
        &argc
    );

    if (!argv)
        return L"";

    std::wstring result;

    for (int i = 1; i < argc; ++i)
    {
        if (!result.empty())
            result += L" ";

        result += QuoteArgument(argv[i]);
    }

    LocalFree(argv);

    return result;
}

// ------------------------------------------------------------
// Запись строки в лог.
// ------------------------------------------------------------
static void Log(const std::wstring& text)
{
    const fs::path logFile =
        GetWrapperDirectory() / L"adb_trace.log";

    std::wofstream out(
        logFile,
        std::ios::app
    );

    if (!out)
        return;

    out.imbue(std::locale(""));

    out << Timestamp()
        << L" | "
        << text
        << std::endl;
}

// ------------------------------------------------------------
// Основная функция.
// ------------------------------------------------------------
int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR,
    int)
{
    const std::wstring wrapperName = GetExeName();

    // Определяем, какой executable был запущен.
    const bool fastboot =
        (_wcsicmp(
            wrapperName.c_str(),
            L"fastboot.exe"
        ) == 0);

    const std::wstring realName =
        fastboot
            ? L"fastboot.real.exe"
            : L"adb.real.exe";

    const fs::path wrapperDir =
        GetWrapperDirectory();

    const fs::path realExe =
        wrapperDir / realName;

    const DWORD parentPid =
        GetCurrentProcessId();

    const std::wstring args =
        GetArguments();

    // --------------------------------------------------------
    // Записываем входящий запрос.
    // --------------------------------------------------------

    Log(
        L"PID=" +
        std::to_wstring(parentPid) +
        L" | "
        L"CWD=" +
        GetCurrentDirectoryString() +
        L" | "
        L"EXE=" +
        wrapperName +
        L" | "
        L"CMD=" +
        args
    );

    // --------------------------------------------------------
    // Проверяем оригинальный executable.
    // --------------------------------------------------------

    if (!fs::exists(realExe))
    {
        Log(
            L"PID=" +
            std::to_wstring(parentPid) +
            L" | ERROR=real executable not found | PATH=" +
            realExe.wstring()
        );

        return ERROR_FILE_NOT_FOUND;
    }

    // --------------------------------------------------------
    // Формируем command line оригинального executable.
    // --------------------------------------------------------

    std::wstring commandLine =
        L"\"" +
        realExe.wstring() +
        L"\"";

    if (!args.empty())
    {
        commandLine += L" ";
        commandLine += args;
    }

    std::vector<wchar_t> mutableCommandLine(
        commandLine.begin(),
        commandLine.end()
    );

    mutableCommandLine.push_back(L'\0');

    // --------------------------------------------------------
    // Запускаем настоящий adb/fastboot.
    //
    // TRUE для bInheritHandles сохраняет возможность работы
    // с наследуемыми handles.
    // --------------------------------------------------------

    STARTUPINFOW si{};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi{};

    BOOL created = CreateProcessW(
        realExe.c_str(),
        mutableCommandLine.data(),

        nullptr,
        nullptr,

        TRUE,

        0,

        nullptr,

        nullptr,

        &si,
        &pi
    );

    if (!created)
    {
        const DWORD error =
            GetLastError();

        Log(
            L"PID=" +
            std::to_wstring(parentPid) +
            L" | ERROR=CreateProcessW | CODE=" +
            std::to_wstring(error)
        );

        return error;
    }

    // --------------------------------------------------------
    // Реальный child PID.
    // --------------------------------------------------------

    Log(
        L"PID=" +
        std::to_wstring(parentPid) +
        L" | CHILD_PID=" +
        std::to_wstring(pi.dwProcessId) +
        L" | STARTED"
    );

    // --------------------------------------------------------
    // Ждём завершения adb/fastboot.
    // --------------------------------------------------------

    WaitForSingleObject(
        pi.hProcess,
        INFINITE
    );

    DWORD exitCode = 0;

    if (!GetExitCodeProcess(
            pi.hProcess,
            &exitCode))
    {
        exitCode = GetLastError();
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // --------------------------------------------------------
    // Записываем exit code.
    // --------------------------------------------------------

    Log(
        L"PID=" +
        std::to_wstring(parentPid) +
        L" | CHILD_PID=" +
        std::to_wstring(pi.dwProcessId) +
        L" | EXIT=" +
        std::to_wstring(exitCode)
    );

    // --------------------------------------------------------
    // КРИТИЧЕСКИ ВАЖНО:
    // возвращаем HavalFirmwareTool тот же exit code.
    // --------------------------------------------------------

    return static_cast<int>(exitCode);
}
