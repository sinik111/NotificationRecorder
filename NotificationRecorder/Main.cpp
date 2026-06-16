#include <iostream>
#include <string>

#include <winrt/windows.foundation.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/windows.data.xml.dom.h>
#include <winrt/windows.ui.notifications.h>
#include <winrt/windows.ui.notifications.management.h>
#include <winrt/windows.applicationmodel.datatransfer.h>

int main()
{

    winrt::init_apartment();

    {
        using namespace winrt::Windows::UI::Notifications::Management;

        // 알림 리스너 인스턴스
        UserNotificationListener listener = UserNotificationListener::Current();

        std::cout << "알림 리스너 접근 권한 요청중...\n";

        // 알림 접근 권한 요청
        auto requestOp = listener.RequestAccessAsync();
        UserNotificationListenerAccessStatus accessStatus = requestOp.get();

        // 허용됨
        if (accessStatus == UserNotificationListenerAccessStatus::Allowed)
        {
            using namespace winrt::Windows::UI::Notifications;

            std::cout << "알림 리스너 접근 권한이 허용되었습니다.\n";
            std::cout << "실시간 알림 모니터링 및 파일 로그 기록 중... (종료하려면 exit을 입력하세요)\n\n";

            auto token = listener.NotificationChanged([](UserNotificationListener const& sender, UserNotificationChangedEventArgs args)
                {
                    //if (args.ChangeKind() == )
                });
        }
    }


    winrt::uninit_apartment();
}