#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <iostream>
#include <string>
#include <print>
#include <string_view>
#include <chrono>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <mutex>

#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.data.xml.dom.h>
#include <winrt/windows.ui.notifications.h>
#include <winrt/windows.ui.notifications.management.h>
#include <winrt/windows.applicationmodel.h>
#include <winrt/windows.management.deployment.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace fs = std::filesystem;

std::wstring ToWideChar(std::string_view multibyteStr)
{
    if (multibyteStr.empty())
    {
        return std::wstring();
    }

    int length = static_cast<int>(multibyteStr.length());

    int count = MultiByteToWideChar(CP_UTF8, 0, multibyteStr.data(), length, nullptr, 0);

    if (count == 0)
    {
        return std::wstring();
    }

    std::wstring str(count, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, multibyteStr.data(), length, &str[0], count);

    return str;
}

std::string ToMultibyte(std::wstring_view wideCharStr)
{
    if (wideCharStr.empty())
    {
        return std::string();
    }

    int length = static_cast<int>(wideCharStr.length());

    int count = WideCharToMultiByte(CP_UTF8, 0, wideCharStr.data(), length, nullptr, 0, nullptr, nullptr);

    if (count == 0)
    {
        return std::string();
    }

    std::string str(count, '\0');

    WideCharToMultiByte(CP_UTF8, 0, wideCharStr.data(), length, &str[0], count, nullptr, nullptr);

    return str;
}

std::wstring StripBidiControlChars(const std::wstring& input)
{
    std::wstring result;
    result.reserve(input.size());

    for (wchar_t ch : input)
    {
        // U+2066~U+2069: LRI, RLI, FSI, PDI (양방향 isolate 제어 문자)
        // U+200E, U+200F: LRM, RLM (양방향 마크 문자)
        if ((ch >= 0x2066 && ch <= 0x2069) || ch == 0x200E || ch == 0x200F)
        {
            continue;
        }
        result += ch;
    }

    return result;
}

std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>> g_packageInfoCache;

bool GetPackageFullNameAndName(const std::wstring& packageFamilyName, std::wstring& outFullName, std::wstring& outName)
{
    auto it = g_packageInfoCache.find(packageFamilyName);
    if (it != g_packageInfoCache.end())
    {
        outFullName = it->second.first;
        outName = it->second.second;
        return true;
    }

    try
    {
        using namespace winrt::Windows::Management::Deployment;
        PackageManager packageManager;

        for (const auto& pkg : packageManager.FindPackagesForUser(L"", packageFamilyName))
        {
            outFullName = std::wstring(pkg.Id().FullName());
            outName = std::wstring(pkg.Id().Name());
            g_packageInfoCache[packageFamilyName] = { outFullName, outName };
            return true;
        }
    }
    catch (...)
    {

    }

    return false;
}

std::wstring ResolveMsResource(const std::wstring& resourceUri, const std::wstring& packageFamilyName)
{
    static constexpr std::wstring_view prefix = L"ms-resource:";
    if (resourceUri.size() < prefix.size() || resourceUri.compare(0, prefix.size(), prefix) != 0)
    {
        return resourceUri; // ms-resource: 형식이 아니면 그대로 반환
    }

    if (packageFamilyName.empty())
    {
        return resourceUri;
    }

    std::wstring key = resourceUri.substr(prefix.size());

    // 이미 절대 경로(맨 앞 '/')면 그대로, 아니면 기본 "Resources/" 네임스페이스로 가정
    if (!key.empty() && key.front() == L'/')
    {
        key.erase(key.begin());
    }
    else
    {
        key = L"Resources/" + key;
    }

    std::wstring fullName, packageName;
    if (!GetPackageFullNameAndName(packageFamilyName, fullName, packageName))
    {
        return resourceUri;
    }

    std::wstring indirect = L"@{" + fullName + L"?ms-resource://" + packageName + L"/" + key + L"}";

    wchar_t buffer[1024]{};
    HRESULT hr = SHLoadIndirectString(indirect.c_str(), buffer, ARRAYSIZE(buffer), nullptr);
    if (SUCCEEDED(hr) && buffer[0] != L'\0')
    {
        return buffer;
    }

    return resourceUri; // 실패 시 원본 반환
}

std::wstring GetDocumentsFolderPath()
{
    PWSTR path = nullptr;
    std::wstring result;

    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path)))
    {
        result = path;
        CoTaskMemFree(path);
    }

    return result;
}

// 문서 폴더 경로는 실행 중 안 바뀌니 한 번만 계산해서 캐싱
fs::path GetLogBaseDirectory()
{
    static fs::path baseDir = []
        {
            fs::path dir = fs::path(GetDocumentsFolderPath()) / L"NotificationRecorder";
            std::error_code ec;
            fs::create_directories(dir, ec); // 이미 있어도 에러 안 남
            return dir;
        }();

    return baseDir;
}

std::wstring GetTodayDateString()
{
    auto now = std::chrono::system_clock::now();
    auto localNow = std::chrono::zoned_time{ std::chrono::current_zone(), now };
    return std::format(L"{:%Y-%m-%d}", localNow);
}

std::mutex g_logMutex;

void AppendLogLine(const std::string& line)
{
    std::lock_guard<std::mutex> lock(g_logMutex);

    fs::path filePath = GetLogBaseDirectory() / (GetTodayDateString() + L".txt");

    bool isNewFile = !fs::exists(filePath);

    std::ofstream ofs(filePath, std::ios::app);
    if (!ofs)
    {
        return;
    }

    if (isNewFile)
    {
        unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        ofs.write(reinterpret_cast<char const*>(bom), sizeof(bom));
    }

    ofs << line << "\n";
}

int main()
{

    winrt::init_apartment();

    {
        using namespace winrt::Windows::UI::Notifications::Management;

        // 알림 리스너 인스턴스
        UserNotificationListener listener = UserNotificationListener::Current();

        std::println("알림 리스너 접근 권한 요청중...");

        // 알림 접근 권한 요청
        auto requestOp = listener.RequestAccessAsync();
        UserNotificationListenerAccessStatus accessStatus = requestOp.get();

        // 허용됨
        if (accessStatus == UserNotificationListenerAccessStatus::Allowed)
        {
            using namespace winrt::Windows::UI::Notifications;

            std::println("알림 리스너 접근 권한이 허용되었습니다.");
            std::println("실시간 알림 모니터링 및 파일 로그 기록 중... (종료하려면 exit을 입력하세요)");

            auto token = listener.NotificationChanged([](const UserNotificationListener& sender, UserNotificationChangedEventArgs args)
                {
                    if (args.ChangeKind() == UserNotificationChangedKind::Added)
                    {
                        using namespace winrt::Windows::Foundation;

                        winrt::Windows::UI::Notifications::UserNotification userNoti = sender.GetNotification(args.UserNotificationId());

                        if (!userNoti)
                        {
                            return;
                        }

                        std::wstring packageFamilyName;
                        try
                        {
                            packageFamilyName = std::wstring(userNoti.AppInfo().PackageFamilyName());
                        }
                        catch (...)
                        {
                            packageFamilyName = L"";
                        }

                        std::string appName = ToMultibyte(userNoti.AppInfo().DisplayInfo().DisplayName().c_str());
                        std::string title;
                        std::string body;

                        NotificationBinding toastBinding = userNoti.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
                        if (toastBinding)
                        {
                            auto textElements = toastBinding.GetTextElements();

                            std::wstring rawTitle = textElements.Size() > 0 ? std::wstring(textElements.GetAt(0).Text()) : L"";
                            std::wstring rawBody = textElements.Size() > 1 ? std::wstring(textElements.GetAt(1).Text()) : L"";

                            rawTitle = ResolveMsResource(rawTitle, packageFamilyName);
                            rawBody = ResolveMsResource(rawBody, packageFamilyName);

                            rawTitle = StripBidiControlChars(rawTitle);
                            rawBody = StripBidiControlChars(rawBody);

                            title = ToMultibyte(ResolveMsResource(rawTitle, packageFamilyName));
                            body = ToMultibyte(ResolveMsResource(rawBody, packageFamilyName));
                        }

                        //__debugbreak();

                        auto tp = winrt::clock::to_sys(userNoti.CreationTime());
                        auto localTp = std::chrono::zoned_time{ std::chrono::current_zone(), tp };
                        std::string timeStr = std::format("{:%Y.%m.%d %H:%M:%S}", localTp);

                        std::string logLine = std::format("{} / {} / {} / {}", appName, title, body, timeStr);

                        std::println("{}", logLine);
                        AppendLogLine(logLine);
                    }
                });

            std::cin.get();

            listener.NotificationChanged(token);
        }
    }


    winrt::uninit_apartment();
}