#include "ChatServer.h"
#include "NetLibrary/Logger/Logger.h"
#include <vector>

Player* ChatServer::findPlayerOrNull(const uint64_t sessionID)
{
	Player* player = nullptr;

	auto found = mPlayerMap.find(sessionID);
	if (found != mPlayerMap.end())
	{
		player = found->second;
	}

	return player;
}

void ChatServer::OnAccept(const uint64_t sessionID)
{
	mPlayerMapLock.Lock();
	{
		Player* newPlayer = mPlayerPool.Alloc();
		newPlayer->Init(sessionID);

		mPlayerMap.insert(std::make_pair(sessionID, newPlayer));
	}
	mPlayerMapLock.Unlock();
}

void ChatServer::OnRelease(const uint64_t sessionID)
{
	mPlayerMapLock.Lock();
	{
		Player* deletePlayer;

		auto found = mPlayerMap.find(sessionID);
		deletePlayer = found->second;

		if (deletePlayer->IsSectorIn())
		{
			uint16_t sectorX = deletePlayer->GetSectorX();
			uint16_t sectorY = deletePlayer->GetSectorY();

			mSectorLock[sectorY][sectorX].Lock();
			{
				mSector[sectorY][sectorX].erase(deletePlayer->GetSessionID());
			}
			mSectorLock[sectorY][sectorX].Unlock();
		}

		mPlayerMap.erase(found);

		mPlayerPool.Free(deletePlayer);
	}
	mPlayerMapLock.Unlock();
}

void ChatServer::OnReceive(const uint64_t sessionID, Serializer* packet)
{
	WORD messageType;

	*packet >> messageType;

	switch (messageType)
	{
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_LOGIN:
	{
		int64_t accountNo;
		WCHAR id[20]{};
		WCHAR nickName[20]{};
		char sessionKey[64]{};

		constexpr uint32_t PACKET_SIZE = sizeof(messageType) + sizeof(accountNo) + sizeof(id) + sizeof(nickName) + sizeof(sessionKey);
		if (packet->GetUseSize() != PACKET_SIZE)
		{
			Disconnect(sessionID);
			break;
		}

		*packet >> accountNo;
		packet->GetByte((char*)id, sizeof(id));
		packet->GetByte((char*)nickName, sizeof(nickName));
		packet->GetByte((char*)sessionKey, sizeof(sessionKey));

		Process_CS_CHAT_REQ_LOGIN(sessionID, accountNo, id, nickName, sessionKey);
	}
	break;
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
	{
		int64_t accountNo;
		WORD sectorX;
		WORD sectorY;

		constexpr uint32_t PACKET_SIZE = sizeof(messageType) + sizeof(accountNo) + sizeof(sectorX) + sizeof(sectorY);
		if (packet->GetUseSize() != PACKET_SIZE)
		{
			Disconnect(sessionID);
			break;
		}

		*packet >> accountNo >> sectorX >> sectorY;

		Process_CS_CHAT_REQ_SECTOR_MOVE(sessionID, accountNo, sectorX, sectorY);
	}
	break;
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_MESSAGE:
	{
		int64_t accountNo;
		WORD messageLen;
		thread_local WCHAR message[UINT16_MAX / 2];

		constexpr uint32_t PACKET_MIN_SIZE = sizeof(messageType) + sizeof(accountNo) + sizeof(messageLen);
		if (packet->GetUseSize() < PACKET_MIN_SIZE)
		{
			Disconnect(sessionID);
			break;
		}

		*packet >> accountNo >> messageLen;

		if (packet->GetUseSize() != PACKET_MIN_SIZE + messageLen)
		{
			Disconnect(sessionID);
			break;
		}

		packet->GetByte((char*)message, messageLen);

		Process_CS_CHAT_REQ_MESSAGE(sessionID, accountNo, messageLen, message);
	}
	break;
	case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_HEARTBEAT:
	{
		constexpr uint32_t PACKET_SIZE = sizeof(messageType);
		if (packet->GetUseSize() != PACKET_SIZE)
		{
			Disconnect(sessionID);
			break;
		}

		Process_CS_CHAT_REQ_HEARTBEAT(sessionID);
	}
	break;
	default:
		Disconnect(sessionID);
	}

	packet->DecrementRefCount();
}

void ChatServer::Process_CS_CHAT_REQ_LOGIN(const uint64_t sessionID, const int64_t accountNo, const WCHAR id[], const WCHAR nickName[], const char sessionKey[])
{
	mPlayerMapLock.ReadLock();
	{
		Player* player = findPlayerOrNull(sessionID);
		if (player == nullptr)
		{
			mPlayerMapLock.Unlock();
			return;
		}

		player->UpdateLastRecvTick();

		player->Lock();
		{
			player->LogIn(accountNo, id, nickName, sessionKey);
		}
		player->Unlock();
	}
	mPlayerMapLock.ReadUnlock();

	Serializer* packet = CreateMessage_CS_CHAT_RES_LOGIN(1, accountNo);

	SendPacket(sessionID, packet);

	packet->DecrementRefCount();
}

void ChatServer::Process_CS_CHAT_REQ_SECTOR_MOVE(const uint64_t sessionID, const int64_t accountNo, const WORD sectorX, const WORD sectorY)
{
	ASSERT_LIVE(sectorX >= 0 && sectorX < 50, L"CS_CHAT_REQ_SECTOR_MOVE invalid sectorX received");
	ASSERT_LIVE(sectorY >= 0 && sectorY < 50, L"CS_CHAT_REQ_SECTOR_MOVE invalid sectorY received");

	int64_t playerAccountNo;

	mPlayerMapLock.ReadLock();
	{
		Player* player = findPlayerOrNull(sessionID);
		if (player == nullptr)
		{
			mPlayerMapLock.Unlock();
			return;
		}

		player->UpdateLastRecvTick();

		player->Lock();
		{
			uint16_t playerPrevSectorX = player->GetSectorX();
			uint16_t playerPrevSectorY = player->GetSectorY();

			playerAccountNo = player->GetAccountNo();

			if (player->IsSectorIn())
			{
				if (sectorY == playerPrevSectorY && sectorX == playerPrevSectorX)
				{
					// do nothing
				}
				else if (sectorY > playerPrevSectorY || (sectorY == playerPrevSectorY && sectorX > playerPrevSectorX))
				{
					mSectorLock[playerPrevSectorY][playerPrevSectorX].Lock();
					mSectorLock[sectorY][sectorX].Lock();
					{
						mSector[playerPrevSectorY][playerPrevSectorX].erase(player->GetSessionID());
						mSector[sectorY][sectorX].insert(player->GetSessionID());
					}
					mSectorLock[sectorY][sectorX].Unlock();
					mSectorLock[playerPrevSectorY][playerPrevSectorX].Unlock();
				}
				else
				{
					mSectorLock[sectorY][sectorX].Lock();
					mSectorLock[playerPrevSectorY][playerPrevSectorX].Lock();
					{
						mSector[playerPrevSectorY][playerPrevSectorX].erase(player->GetSessionID());
						mSector[sectorY][sectorX].insert(player->GetSessionID());
					}
					mSectorLock[playerPrevSectorY][playerPrevSectorX].Unlock();
					mSectorLock[sectorY][sectorX].Unlock();
				}
			}
			else
			{
				mSectorLock[sectorY][sectorX].Lock();
				{
					mSector[sectorY][sectorX].insert(player->GetSessionID());
				}
				mSectorLock[sectorY][sectorX].Unlock();
			}

			player->MoveSector(sectorX, sectorY);
		}
		player->Unlock();
	}
	mPlayerMapLock.ReadUnlock();

	Serializer* packet = CreateMessage_CS_CHAT_RES_SECTOR_MOVE(playerAccountNo, sectorX, sectorY);

	SendPacket(sessionID, packet);

	packet->DecrementRefCount();
}

void ChatServer::Process_CS_CHAT_REQ_MESSAGE(const uint64_t sessionID, const int64_t accountNo, const WORD messageLen, const WCHAR message[])
{
	Serializer* packet;

	std::vector<uint64_t> sendOtherSessions;

	mPlayerMapLock.ReadLock();
	{
		Player* player = findPlayerOrNull(sessionID);
		if (player == nullptr)
		{
			mPlayerMapLock.Unlock();
			return;
		}

		player->UpdateLastRecvTick();

		player->Lock();
		{
			ASSERT_LIVE(player->GetSessionID() == sessionID, L"CS_CHAT_REQ_MESSAGE player->GetSessionID() != sessionID");
			ASSERT_LIVE(player->IsSectorIn(), L"CS_CHAT_REQ_MESSAGE player is not in any sector");

			packet = CreateMessage_CS_CHAT_RES_MESSAGE(player->GetAccountNo(), player->GetID(), player->GetNickName(), messageLen, message);

			// Lock
			if (player->GetSectorY() > 0)
			{
				if (player->GetSectorX() > 0)
				{
					mSectorLock[player->GetSectorY() - 1][player->GetSectorX() - 1].ReadLock();
				}
				{
					mSectorLock[player->GetSectorY() - 1][player->GetSectorX()].ReadLock();
				}
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					mSectorLock[player->GetSectorY() - 1][player->GetSectorX() + 1].ReadLock();
				}
			}
			{
				if (player->GetSectorX() > 0)
				{
					mSectorLock[player->GetSectorY()][player->GetSectorX() - 1].ReadLock();
				}
				{
					mSectorLock[player->GetSectorY()][player->GetSectorX()].ReadLock();
				}
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					mSectorLock[player->GetSectorY()][player->GetSectorX() + 1].ReadLock();
				}
			}
			if (player->GetSectorY() < SECTOR_WIDTH_AND_HEIGHT - 1)
			{
				if (player->GetSectorX() > 0)
				{
					mSectorLock[player->GetSectorY() + 1][player->GetSectorX() - 1].ReadLock();
				}
				{
					mSectorLock[player->GetSectorY() + 1][player->GetSectorX()].ReadLock();
				}
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					mSectorLock[player->GetSectorY() + 1][player->GetSectorX() + 1].ReadLock();
				}
			}

			// get session IDs
			if (player->GetSectorY() > 0)
			{
				if (player->GetSectorX() > 0)
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY() - 1][player->GetSectorX() - 1])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY() - 1][player->GetSectorX()])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY() - 1][player->GetSectorX() + 1])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
			}
			{
				if (player->GetSectorX() > 0)
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY()][player->GetSectorX() - 1])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY()][player->GetSectorX()])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY()][player->GetSectorX() + 1])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
			}
			if (player->GetSectorY() < SECTOR_WIDTH_AND_HEIGHT - 1)
			{
				if (player->GetSectorX() > 0)
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY() + 1][player->GetSectorX() - 1])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY() + 1][player->GetSectorX()])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					for (const uint64_t otherSession : mSector[player->GetSectorY() + 1][player->GetSectorX() + 1])
					{
						sendOtherSessions.push_back(otherSession);
					}
				}
			}

			// unlock
			if (player->GetSectorY() < SECTOR_WIDTH_AND_HEIGHT - 1)
			{
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					mSectorLock[player->GetSectorY() + 1][player->GetSectorX() + 1].ReadUnlock();
				}
				{
					mSectorLock[player->GetSectorY() + 1][player->GetSectorX()].ReadUnlock();
				}
				if (player->GetSectorX() > 0)
				{
					mSectorLock[player->GetSectorY() + 1][player->GetSectorX() - 1].ReadUnlock();
				}
			}
			{
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					mSectorLock[player->GetSectorY()][player->GetSectorX() + 1].ReadUnlock();
				}
				{
					mSectorLock[player->GetSectorY()][player->GetSectorX()].ReadUnlock();
				}
				if (player->GetSectorX() > 0)
				{
					mSectorLock[player->GetSectorY()][player->GetSectorX() - 1].ReadUnlock();
				}
			}
			if (player->GetSectorY() > 0)
			{
				if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
				{
					mSectorLock[player->GetSectorY() - 1][player->GetSectorX() + 1].ReadUnlock();
				}
				{
					mSectorLock[player->GetSectorY() - 1][player->GetSectorX()].ReadUnlock();
				}
				if (player->GetSectorX() > 0)
				{
					mSectorLock[player->GetSectorY() - 1][player->GetSectorX() - 1].ReadUnlock();
				}
			}
		}
		player->Unlock();
	}
	mPlayerMapLock.ReadUnlock();

	for (const uint64_t otherSession : sendOtherSessions)
	{
		SendPacket(otherSession, packet);
	}

	packet->DecrementRefCount();
}

void ChatServer::Process_CS_CHAT_REQ_HEARTBEAT(const uint64_t sessionID)
{
	mPlayerMapLock.ReadLock();
	{
		Player* player = findPlayerOrNull(sessionID);
		if (player == nullptr)
		{
			mPlayerMapLock.Unlock();
			return;
		}

		player->UpdateLastRecvTick();
	}
	mPlayerMapLock.ReadUnlock();
}