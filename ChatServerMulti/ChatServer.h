#pragma once

#include "NetLibrary/NetServer/NetServer.h"
#include "NetLibrary/DataStructure/LockFreeQueue.h"
#include "Work.h"
#include "Protocol.h"

#include <map>
#include <set>

#include "Lock.h"
#include "Player.h"
#include "NetLibrary/Memory/ObjectPool.h"

class ChatServer : public NetServer
{
public:

	virtual void OnAccept(const uint64_t sessionID) override;
	virtual void OnRelease(const uint64_t sessionID) override;
	virtual void OnReceive(const uint64_t sessionID, Serializer* packet) override;

public:

	ChatServer() = default;
	virtual ~ChatServer() override { if (IsRunning()) Shutdown(); }

public:

	inline uint32_t GetPlayerPoolSize(void) const { return mPlayerPool.GetTotalCreatedObjectCount(); }
	inline size_t GetPlayerCount(void) const { return mPlayerMap.size(); }

public:

	void Process_CS_CHAT_REQ_LOGIN(const uint64_t sessionID, const int64_t accountNo, const WCHAR id[], const WCHAR nickName[], const char sessionKey[]);
	void Process_CS_CHAT_REQ_SECTOR_MOVE(const uint64_t sessionID, const int64_t accountNo, const WORD sectorX, const WORD sectorY);
	void Process_CS_CHAT_REQ_MESSAGE(const uint64_t sessionID, const int64_t accountNo, const WORD messageLen, const WCHAR message[]);
	void Process_CS_CHAT_REQ_HEARTBEAT(const uint64_t sessionID);

	static Serializer* CreateMessage_CS_CHAT_RES_LOGIN(const BYTE Status, const int64_t AccountNo)
	{
		Serializer* packet = Serializer::Alloc();

		*packet << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << Status << AccountNo;

		return packet;
	}

	static Serializer* CreateMessage_CS_CHAT_RES_SECTOR_MOVE(const int64_t AccountNo, const WORD sectorX, const WORD sectorY)
	{
		Serializer* packet = Serializer::Alloc();

		*packet << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE << AccountNo << sectorX << sectorY;

		return packet;
	}

	static Serializer* CreateMessage_CS_CHAT_RES_MESSAGE(const int64_t AccountNo, const WCHAR id[], const WCHAR nickName[], const WORD messageLen, const WCHAR message[])
	{
		Serializer* packet = Serializer::Alloc();

		*packet << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE << AccountNo;
		packet->InsertByte((const char*)id, sizeof(WCHAR) * 20);
		packet->InsertByte((const char*)nickName, sizeof(WCHAR) * 20);

		*packet << messageLen;
		packet->InsertByte((const char*)message, messageLen);

		return packet;
	}

private:

	Player* findPlayerOrNull(const uint64_t sessionID);

private:
	enum
	{
		SECTOR_WIDTH_AND_HEIGHT = 50,
		TIMEOUT_CHECK_INTERVAL = 1'000,
		TIMEOUT_LOGGED_IN = 40'000,
		TIMEOUT_NOT_LOGGED_IN = 10'000
	};

	std::map<uint64_t, Player*> mPlayerMap;
	inline static OBJECT_POOL<Player> mPlayerPool;
	std::set<uint64_t> mSector[SECTOR_WIDTH_AND_HEIGHT][SECTOR_WIDTH_AND_HEIGHT];
	SrwLock mPlayerMapLock;
	SrwLock mSectorLock[SECTOR_WIDTH_AND_HEIGHT][SECTOR_WIDTH_AND_HEIGHT];
};