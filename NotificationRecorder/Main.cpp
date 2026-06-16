#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlwapi.h>
#include <iostream>
#include <string>
#include <print>
#include <string_view>
#include <chrono>
#include <unordered_map>

#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.data.xml.dom.h>
#include <winrt/windows.ui.notifications.h>
#include <winrt/windows.ui.notifications.management.h>
#include <winrt/windows.applicationmodel.h>
#include <winrt/windows.management.deployment.h>

#pragma comment(lib, "Shlwapi.lib")

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

        for (auto const& pkg : packageManager.FindPackagesForUser(L"", packageFamilyName))
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

            auto token = listener.NotificationChanged([](UserNotificationListener const& sender, UserNotificationChangedEventArgs args)
                {
                    if (args.ChangeKind() == UserNotificationChangedKind::Added)
                    {
                        using namespace winrt::Windows::Foundation;

                        UserNotification userNoti = sender.GetNotification(args.UserNotificationId());

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

                            title = ToMultibyte(ResolveMsResource(rawTitle, packageFamilyName));
                            body = ToMultibyte(ResolveMsResource(rawBody, packageFamilyName));
                        }

                        //__debugbreak();

                        std::chrono::system_clock::time_point tp = winrt::clock::to_sys(userNoti.CreationTime());
                        auto localTp = std::chrono::zoned_time{ std::chrono::current_zone(), tp };
                        std::string timeStr = std::format("{:%Y.%m.%d %H:%M:%S}", localTp);

                        std::println("{} / {} / {} / {}", appName, title, body, timeStr);
                    }
                });

            std::cin.get();

            listener.NotificationChanged(token);
        }
    }


    winrt::uninit_apartment();
}