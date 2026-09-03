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
#include <cwctype>

#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;


// ============================================================
// Timestamp
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


// ============================================================
// Quote argument for logging / CreateProcess command line
// ============================================================

static std::wstring QuoteArg(const std::wstring& arg)
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
            result.append(backslashes * 2 + 1, L'\\');
            result += L'"';
            backslashes = 0;
            continue;
        }

        result.append(backslashes, L'\\');
        backslashes = 0;

        result += c;
    }

    result.append(backslashes * 2, L'\\');
    result += L'"';

    return result;
}


// ============================================================
// Raw command line
// ============================================================

static std::wstring GetRawCommandLine()
{
    return GetCommandLineW();
}


// ============================================================
// Parsed command line
// ============================================================

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


// ============================================================
// Current executable
// ============================================================

static fs::path GetExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    while (true)
    {
        DWORD len = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size())
        );

        if (len == 0)
            return {};

        if (len < buffer.size() - 1)
            return fs::path(
                std::wstring(buffer.data(), len)
            );

        buffer.resize(buffer.size() * 2);
    }
}


static std::wstring GetExecutableName()
{
    fs::path path = GetExecutablePath();

    if (path.empty())
        return L"unknown.exe";

    return path.filename().wstring();
}


// ============================================================
// Current working directory
// ============================================================

static std::wstring GetCurrentDirectoryString()
{
    DWORD size = GetCurrentDirectoryW(
        0,
        nullptr
    );

    if (size == 0)
        return L"<GetCurrentDirectory failed>";

    std::vector<wchar_t> buffer(size);

    DWORD result = GetCurrentDirectoryW(
        size,
        buffer.data()
    );

    if (result == 0)
        return L"<GetCurrentDirectory failed>";

    return std::wstring(
        buffer.data(),
        result
    );
}


// ============================================================
// Logging
// ============================================================

static void LogLine(
    const fs::path& logFile,
    const std::wstring& line
)
{
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
// HANDLE information
//
// Только для диагностики.
//
// ВАЖНО:
// Мы НЕ изменяем HANDLE.
// Просто записываем их состояние в лог.
// ============================================================

static std::wstring DescribeHandle(HANDLE h)
{
    if (h == nullptr)
        return L"NULL";

    if (h == INVALID_HANDLE_VALUE)
        return L"INVALID_HANDLE_VALUE";

    DWORD flags = 0;

    if (GetHandleInformation(h, &flags))
    {
        std::wstringstream ss;

        ss << L"VALID"
           << L",inherit="
           << ((flags & HANDLE_FLAG_INHERIT) ? L"1" : L"0");

        return ss.str();
    }

    std::wstringstream ss;

    ss << L"INVALID,error="
       << GetLastError();

    return ss.str();
}


// ============================================================
// Main
// ============================================================

int wmain(int argc, wchar_t* argv[])
{
    const DWORD wrapperPid =
        GetCurrentProcessId();

    // --------------------------------------------------------
    // Paths
    // --------------------------------------------------------

    const fs::path executablePath =
        GetExecutablePath();

    if (executablePath.empty())
    {
        return ERROR_FILE_NOT_FOUND;
    }

    const fs::path wrapperDir =
        executablePath.parent_path();

    const std::wstring executableName =
        executablePath.filename().wstring();

    const fs::path logFile =
        wrapperDir / L"adb_trace.log";


    // --------------------------------------------------------
    // Determine mode
    // --------------------------------------------------------

    std::wstring lowerName =
        executableName;

    for (wchar_t& c : lowerName)
    {
        c = static_cast<wchar_t>(
            std::towlower(c)
        );
    }


    fs::path realExecutable;

    if (lowerName == L"adb.exe")
    {
        realExecutable =
            wrapperDir / L"adb.real.exe";
    }
    else if (lowerName == L"fastboot.exe")
    {
        realExecutable =
            wrapperDir / L"fastboot.real.exe";
    }
    else
    {
        // Manual diagnostic launch:
        // wrapper.exe -> adb.real.exe

        realExecutable =
            wrapperDir / L"adb.real.exe";
    }


    // --------------------------------------------------------
    // CWD
    // --------------------------------------------------------

    const std::wstring cwd =
        GetCurrentDirectoryString();


    // --------------------------------------------------------
    // Raw command line
    // --------------------------------------------------------

    const std::wstring rawCommandLine =
        GetRawCommandLine();

    const std::wstring parsedCommandLine =
        GetParsedCommandLine();


    // --------------------------------------------------------
    // Standard handles
    //
    // We DO NOT modify them.
    //
    // This is important because program may provide
    // pipes here.
    // --------------------------------------------------------

    HANDLE hStdIn =
        GetStdHandle(STD_INPUT_HANDLE);

    HANDLE hStdOut =
        GetStdHandle(STD_OUTPUT_HANDLE);

    HANDLE hStdErr =
        GetStdHandle(STD_ERROR_HANDLE);


    // --------------------------------------------------------
    // Initial log
    // --------------------------------------------------------

    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" START"
           << L" TYPE=" << lowerName
           << L" CWD=\"" << cwd << L"\""
           << L" REAL=\""
           << realExecutable.wstring()
           << L"\""
           << L" RAW_CMD=\""
           << rawCommandLine
           << L"\""
           << L" CMD=\""
           << parsedCommandLine
           << L"\""
           << L" STDIN={"
           << DescribeHandle(hStdIn)
           << L"}"
           << L" STDOUT={"
           << DescribeHandle(hStdOut)
           << L"}"
           << L" STDERR={"
           << DescribeHandle(hStdErr)
           << L"}";

        LogLine(
            logFile,
            ss.str()
        );
    }


    // --------------------------------------------------------
    // Check real executable
    // --------------------------------------------------------

    if (!fs::exists(realExecutable))
    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" ERROR=REAL_EXECUTABLE_NOT_FOUND"
           << L" PATH=\""
           << realExecutable.wstring()
           << L"\"";

        LogLine(
            logFile,
            ss.str()
        );

        return ERROR_FILE_NOT_FOUND;
    }


    // --------------------------------------------------------
    // Build child command line
    // --------------------------------------------------------

    std::wstring childCommandLine;

    childCommandLine +=
        QuoteArg(realExecutable.wstring());

    for (int i = 1; i < argc; ++i)
    {
        childCommandLine += L' ';
        childCommandLine +=
            QuoteArg(argv[i]);
    }


    // --------------------------------------------------------
    // Create writable command line buffer
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

    si.cb =
        sizeof(si);

    si.dwFlags =
        STARTF_USESTDHANDLES;

    si.hStdInput =
        hStdIn;

    si.hStdOutput =
        hStdOut;

    si.hStdError =
        hStdErr;


    PROCESS_INFORMATION pi{};


    // --------------------------------------------------------
    // CreateProcess
    //
    // IMPORTANT:
    //
    // TRUE = child may inherit handles.
    //
    // nullptr environment = inherit environment.
    //
    // nullptr current directory = inherit CWD.
    //
    // No CREATE_NEW_CONSOLE.
    // No CREATE_NO_WINDOW.
    // No CREATE_NEW_PROCESS_GROUP.
    //
    // Thus adb/fastboot should receive the same execution
    // environment as directly launched child process.
    // --------------------------------------------------------

    BOOL created = CreateProcessW(
        realExecutable.wstring().c_str(),

        commandBuffer.data(),

        nullptr,          // process attributes
        nullptr,          // thread attributes
        TRUE,             // inherit handles
        CREATE_NO_WINDOW, // creation flags
        nullptr,          // environment
        nullptr,          // current directory
        &si,
        &pi
    );


    // --------------------------------------------------------
    // CreateProcess failed
    // --------------------------------------------------------

    if (!created)
    {
        DWORD error =
            GetLastError();

        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CREATE_PROCESS_FAILED"
           << L" CODE=" << error
           << L" REAL=\""
           << realExecutable.wstring()
           << L"\"";

        LogLine(
            logFile,
            ss.str()
        );

        return static_cast<int>(
            error
        );
    }


    // --------------------------------------------------------
    // Child PID
    // --------------------------------------------------------

    const DWORD childPid =
        pi.dwProcessId;


    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID="
           << childPid
           << L" RUNNING";

        LogLine(
            logFile,
            ss.str()
        );
    }


    // Thread handle no longer needed.
    CloseHandle(pi.hThread);


    // --------------------------------------------------------
    // Wait for adb / fastboot
    // --------------------------------------------------------

    DWORD waitResult =
        WaitForSingleObject(
            pi.hProcess,
            INFINITE
        );


    if (waitResult != WAIT_OBJECT_0)
    {
        DWORD error =
            GetLastError();

        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID="
           << childPid
           << L" WAIT_FAILED"
           << L" CODE="
           << error;

        LogLine(
            logFile,
            ss.str()
        );

        CloseHandle(
            pi.hProcess
        );

        return static_cast<int>(
            error
        );
    }


    // --------------------------------------------------------
    // Get original exit code
    // --------------------------------------------------------

    DWORD exitCode = 0;

    if (!GetExitCodeProcess(
            pi.hProcess,
            &exitCode))
    {
        DWORD error =
            GetLastError();

        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID="
           << childPid
           << L" GET_EXIT_CODE_FAILED"
           << L" CODE="
           << error;

        LogLine(
            logFile,
            ss.str()
        );

        CloseHandle(
            pi.hProcess
        );

        return static_cast<int>(
            error
        );
    }


    CloseHandle(
        pi.hProcess
    );


    // --------------------------------------------------------
    // Final log
    // --------------------------------------------------------

    {
        std::wstringstream ss;

        ss << GetTimestamp()
           << L" PID=" << wrapperPid
           << L" CHILD_PID="
           << childPid
           << L" EXIT="
           << exitCode;

        LogLine(
            logFile,
            ss.str()
        );
    }


    // --------------------------------------------------------
    // Return EXACT original exit code.
    // --------------------------------------------------------

    return static_cast<int>(
        exitCode
    );
}
