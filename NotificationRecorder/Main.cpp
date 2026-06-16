#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iostream>
#include <string>
#include <print>
#include <string_view>
#include <chrono>

#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.data.xml.dom.h>
#include <winrt/windows.ui.notifications.h>
#include <winrt/windows.ui.notifications.management.h>
#include <winrt/windows.applicationmodel.datatransfer.h>
#include <winrt/windows.globalization.datetimeformatting.h>

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

                        std::wstring appName = userNoti.AppInfo().DisplayInfo().DisplayName().c_str();
                        std::wstring desc = userNoti.AppInfo().DisplayInfo().Description().c_str();

                        //__debugbreak();

                        //winrt::Windows::Globalization::DateTimeFormatting::DateTimeFormatter formatter{ L"year.month.day hour:minute:second" };
                        //std::wstring time = formatter.Format(userNoti.CreationTime()).c_str();

                        //auto a = userNoti.CreationTime().time_since_epoch().count();
                        //winrt::clock::to_time_t(userNoti.CreationTime());

                        std::chrono::system_clock::time_point tp = winrt::clock::to_sys(userNoti.CreationTime());
                        auto local_tp = std::chrono::zoned_time{ std::chrono::current_zone(), tp };
                        std::string time_str = std::format("{:%Y.%m.%d %H:%M:%S}", local_tp);

                        std::println("{} {} {}", ToMultibyte(appName), ToMultibyte(desc), time_str);
                    }
                });

            std::cin.get();

            listener.NotificationChanged(token);
        }
    }


    winrt::uninit_apartment();
}