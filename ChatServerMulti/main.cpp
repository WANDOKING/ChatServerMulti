#include <conio.h>
#include <Windows.h>
#include <process.h>

#include "NetLibrary/Tool/ConfigReader.h"
#include "NetLibrary/Logger/Logger.h"
#include "NetLibrary/Profiler/Profiler.h"

#include "ChatServer.h"

ChatServer myChatServer;

int main(void)
{
    // Set Log Level
    Logger::SetLogLevel(ELogLevel::System);

    // config input
    uint32_t inputPortNumber;
    uint32_t inputMaxSessionCount;
    uint32_t inputConcurrentThreadCount;
    uint32_t inputWorkerThreadCount;
    uint32_t inputSetTcpNodelay;
    uint32_t inputSetSendBufZero;

    ASSERT_LIVE(ConfigReader::GetInt("ChatServer.config", "PORT", &inputPortNumber), L"ERROR: config file read failed (PORT)");
    ASSERT_LIVE(ConfigReader::GetInt("ChatServer.config", "MAX_SESSION_COUNT", &inputMaxSessionCount), L"ERROR: config file read failed (MAX_SESSION_COUNT)");
    ASSERT_LIVE(ConfigReader::GetInt("ChatServer.config", "CONCURRENT_THREAD_COUNT", &inputConcurrentThreadCount), L"ERROR: config file read failed (CONCURRENT_THREAD_COUNT)");
    ASSERT_LIVE(ConfigReader::GetInt("ChatServer.config", "WORKER_THREAD_COUNT", &inputWorkerThreadCount), L"ERROR: config file read failed (WORKER_THREAD_COUNT)");
    ASSERT_LIVE(ConfigReader::GetInt("ChatServer.config", "TCP_NODELAY", &inputSetTcpNodelay), L"ERROR: config file read failed (TCP_NODELAY)");
    ASSERT_LIVE(ConfigReader::GetInt("ChatServer.config", "SND_BUF_ZERO", &inputSetSendBufZero), L"ERROR: config file read failed (SND_BUF_ZERO)");

    LOGF(ELogLevel::System, L"CONCURRENT_THREAD_COUNT = %u", inputConcurrentThreadCount);
    LOGF(ELogLevel::System, L"WORKER_THREAD_COUNT = %u", inputWorkerThreadCount);

    if (inputSetTcpNodelay != 0)
    {
        myChatServer.SetTcpNodelay(true);
        LOGF(ELogLevel::System, L"myChatServer.SetTcpNodelay(true)");
    }

    if (inputSetSendBufZero != 0)
    {
        myChatServer.SetSendBufferSizeToZero(true);
        LOGF(ELogLevel::System, L"myChatServer.SetSendBufferSizeToZero(true)");
    }
    
    myChatServer.SetMaxPayloadLength(INT16_MAX);

    // Server Run
    myChatServer.Start(static_cast<uint16_t>(inputPortNumber), inputMaxSessionCount, inputConcurrentThreadCount, inputWorkerThreadCount);

    while (true)
    {
        if (_kbhit())
        {
            int input = _getch();
            if (input == 'Q' || input == 'q')
            {
                myChatServer.Shutdown();
                break;
            }
#ifdef PROFILE_ON
            else if (input == 'S' || input == 's')
            {
                PROFILE_SAVE(L"Profile_ChatServer.txt");
                LOGF(ELogLevel::System, L"Profile_ChatServer.txt saved");
            }
#endif
        }

        MonitoringVariables monitoringInfo = myChatServer.GetMonitoringInfo();

        LOG_MONITOR(L"\n");
        LOG_CURRENT_TIME();
        LOG_MONITOR(L"[ ChatServer Running (S: profile save) (Q: quit)]");
        LOG_MONITOR(L"=================================================");
        LOG_MONITOR(L"Session Count        = %u / %u", myChatServer.GetSessionCount(), myChatServer.GetMaxSessionCount());
        LOG_MONITOR(L"Accept Total         = %llu", myChatServer.GetTotalAcceptCount());
        LOG_MONITOR(L"Disconnected Total   = %llu", myChatServer.GetTotalDisconnectCount());
        LOG_MONITOR(L"Packet Pool Size     = %u", Serializer::GetTotalPacketCount());
        LOG_MONITOR(L"---------------------- TPS ----------------------");
        LOG_MONITOR(L"Accept TPS           = %9u (Avg: %9u)", monitoringInfo.AcceptTPS, monitoringInfo.AverageAcceptTPS);
        LOG_MONITOR(L"Send Message TPS     = %9u (Avg: %9u)", monitoringInfo.SendMessageTPS, monitoringInfo.AverageSendMessageTPS);
        LOG_MONITOR(L"Recv Message TPS     = %9u (Avg: %9u)", monitoringInfo.RecvMessageTPS, monitoringInfo.AverageRecvMessageTPS);
        LOG_MONITOR(L"Send Pending TPS     = %9u (Avg: %9u)", monitoringInfo.SendPendingTPS, monitoringInfo.AverageSendPendingTPS);
        LOG_MONITOR(L"Recv Pending TPS     = %9u (Avg: %9u)", monitoringInfo.RecvPendingTPS, monitoringInfo.AverageRecvPendingTPS);
        LOG_MONITOR(L"----------------------- CPU ---------------------");
        LOG_MONITOR(L"Total  = Processor: %6.3f / Process: %6.3f", monitoringInfo.ProcessorTimeTotal, monitoringInfo.ProcessTimeTotal);
        LOG_MONITOR(L"User   = Processor: %6.3f / Process: %6.3f", monitoringInfo.ProcessorTimeUser, monitoringInfo.ProcessTimeUser);
        LOG_MONITOR(L"Kernel = Processor: %6.3f / Process: %6.3f", monitoringInfo.ProcessorTimeKernel, monitoringInfo.ProcessTimeKernel);
        LOG_MONITOR(L"=================================================");
        LOG_MONITOR(L"Player Count       = %llu / %u", myChatServer.GetPlayerCount(), myChatServer.GetPlayerPoolSize());

        Sleep(1'000);
    }
}