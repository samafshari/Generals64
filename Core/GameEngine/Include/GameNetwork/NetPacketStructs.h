// Command & Conquer Generals Zero Hour - Generals Remastered
// Copyright (C) 2026 Generals Remastered project.
// This file is derived from EA's GPL v3 Zero Hour source release
// (https://github.com/electronicarts/CnC_Generals_Zero_Hour)
// and is redistributed under the same GNU General Public License v3.

// Packed struct definitions for network packet serialization.

#pragma once

#include "GameNetwork/NetworkDefs.h"

// Ensure structs are packed to 1-byte alignment for network protocol compatibility
#pragma pack(push, 1)

////////////////////////////////////////////////////////////////////////////////
// Network packet field type definitions
////////////////////////////////////////////////////////////////////////////////

typedef UnsignedByte NetPacketFieldType;

namespace NetPacketFieldTypes {
	constexpr const NetPacketFieldType CommandType = 'T';
	constexpr const NetPacketFieldType Relay = 'R';
	constexpr const NetPacketFieldType Frame = 'F';
	constexpr const NetPacketFieldType PlayerId = 'P';
	constexpr const NetPacketFieldType CommandId = 'C';
	constexpr const NetPacketFieldType Data = 'D';
}

////////////////////////////////////////////////////////////////////////////////
// Common packet field structures
////////////////////////////////////////////////////////////////////////////////

struct NetPacketCommandTypeField {
	NetPacketCommandTypeField() : fieldType(NetPacketFieldTypes::CommandType) {}
	char fieldType;
	UnsignedByte commandType;
};

struct NetPacketRelayField {
	NetPacketRelayField() : fieldType(NetPacketFieldTypes::Relay) {}
	char fieldType;
	UnsignedByte relay;
};

struct NetPacketFrameField {
	NetPacketFrameField() : fieldType(NetPacketFieldTypes::Frame) {}
	char fieldType;
	UnsignedInt frame;
};

struct NetPacketPlayerIdField {
	NetPacketPlayerIdField() : fieldType(NetPacketFieldTypes::PlayerId) {}
	char fieldType;
	UnsignedByte playerId;
};

struct NetPacketCommandIdField {
	NetPacketCommandIdField() : fieldType(NetPacketFieldTypes::CommandId) {}
	char fieldType;
	UnsignedShort commandId;
};

struct NetPacketDataField {
	NetPacketDataField() : fieldType(NetPacketFieldTypes::Data) {}
	char fieldType;
};

////////////////////////////////////////////////////////////////////////////////
// Packed Network structures
////////////////////////////////////////////////////////////////////////////////

struct NetPacketAckCommand {
	NetPacketCommandTypeField commandType;
	NetPacketPlayerIdField playerId;
	NetPacketDataField dataHeader;
	UnsignedShort commandId;           // Command ID being acknowledged
	UnsignedByte originalPlayerId;     // Original player who sent the command
};

struct NetPacketFrameCommand {
	NetPacketCommandTypeField commandType;
	NetPacketFrameField frame;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedShort commandCount;
};

struct NetPacketPlayerLeaveCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketFrameField frame;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedByte leavingPlayerId;
};

struct NetPacketRunAheadMetricsCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	Real averageLatency;
	UnsignedShort averageFps;
};

struct NetPacketRunAheadCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketFrameField frame;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedShort runAhead;
	UnsignedByte frameRate;
};

struct NetPacketDestroyPlayerCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketFrameField frame;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedInt playerIndex;
};

struct NetPacketKeepAliveCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketDataField dataHeader;
};

struct NetPacketDisconnectKeepAliveCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketDataField dataHeader;
};

struct NetPacketDisconnectPlayerCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedByte slot;
	UnsignedInt disconnectFrame;
};

struct NetPacketRouterQueryCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketDataField dataHeader;
};

struct NetPacketRouterAckCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketDataField dataHeader;
};

struct NetPacketDisconnectVoteCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedByte slot;
	UnsignedInt voteFrame;
};

struct NetPacketChatCommand {
	NetPacketCommandTypeField commandType;
	NetPacketFrameField frame;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedByte textLength;
	// Variable fields: WideChar text[textLength] + Int playerMask

	enum { MaxTextLen = 255 };
	static Int getUsableTextLength(const UnicodeString& text) { return min(text.getLength(), (Int)MaxTextLen); }
};

struct NetPacketDisconnectChatCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketDataField dataHeader;
	UnsignedByte textLength;
	// Variable fields: WideChar text[textLength]

	enum { MaxTextLen = 255 };
	static Int getUsableTextLength(const UnicodeString& text) { return min(text.getLength(), (Int)MaxTextLen); }
};

struct NetPacketGameCommand {
	NetPacketCommandTypeField commandType;
	NetPacketFrameField frame;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	// Variable fields: GameMessage type + argument types + argument data
};

struct NetPacketWrapperCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedShort wrappedCommandId;
	UnsignedInt chunkNumber;
	UnsignedInt numChunks;
	UnsignedInt totalDataLength;
	UnsignedInt dataLength;
	UnsignedInt dataOffset;
};

struct NetPacketFileCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	// Variable fields: null-terminated filename + UnsignedInt fileDataLength + file data
};

struct NetPacketFileAnnounceCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	// Variable fields: null-terminated filename + UnsignedShort fileID + UnsignedByte playerMask
};

struct NetPacketFileProgressCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedShort fileId;
	Int progress;
};

struct NetPacketProgressCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketDataField dataHeader;
	UnsignedByte percentage;
};

struct NetPacketLoadCompleteCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
};

struct NetPacketTimeOutGameStartCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
};

struct NetPacketDisconnectFrameCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedInt disconnectFrame;
};

struct NetPacketDisconnectScreenOffCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedInt newFrame;
};

struct NetPacketFrameResendRequestCommand {
	NetPacketCommandTypeField commandType;
	NetPacketRelayField relay;
	NetPacketPlayerIdField playerId;
	NetPacketCommandIdField commandId;
	NetPacketDataField dataHeader;
	UnsignedInt frameToResend;
};

// Restore normal struct packing
#pragma pack(pop)
