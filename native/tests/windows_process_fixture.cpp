// A real Unicode argv/pipe probe and a descendant that outlives its parent.
#include <windows.h>

#include <iostream>
#include <string>
#include <string_view>

int wmain(int argc, wchar_t** argv) {
    if (argc == 5 && std::wstring_view(argv[1]) == L"echo") {
        if (std::wstring_view(argv[2]) != L"空 白" || std::wstring_view(argv[3]) != L"" ||
            std::wstring_view(argv[4]) != LR"(a "quote" and tail\)") return 2;
        std::cout << std::cin.rdbuf();
        std::cerr << "stderr";
        return 23;
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"hold") {
        Sleep(30000);
        return 0;
    }
    if (argc != 2 || std::wstring_view(argv[1]) != L"spawn") return 3;
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    std::wstring command = L"\"" + std::wstring(argv[0]) + L"\" hold";
    if (!CreateProcessW(argv[0], command.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &child)) return 4;
    std::cout << child.dwProcessId << std::flush;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return 0;
}
