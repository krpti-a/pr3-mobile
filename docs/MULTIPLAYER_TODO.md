# Multiplayer / online — decoded, not wired up (TODO for a later round)

Out of scope for the first release (the Sparkworkz servers are gone), but all of the original online code is
**recompiled and present** in `PR3Core/generated` (nothing was stripped except the Facebook/Twitter/OAuth/Tapjoy
SDK classes, see `skip_prefixes` in `tools/recomp/full.json`). Today it fails cleanly and the game uses its own
`OfflineSocket`. This file records what was learned so the work can be picked up later.

## Architecture (from the symbols and strings)

```
Sparkworkz (HTTP, XML "DataAccess" calls)      account: CreateUser / Login -> login token
   hosts in the binary: api.sparkworkz.com, www.sparkworkz.com/platformracing3/, dev./qa.sparkworkz.com (test)
        │  Sparkworkz::Initialize 0x5b1b0, Login 0x5a744, DataAccess 0x593ec, DataAccessCallback 0x59b3c
        ▼
SocketManager 0x21ebc (IsOffline 0x21ee0, InitOffline 0x22e4c, Connect(ServerInfo*) 0x2230c, StartVerifiedLogin 0x225a8)
   owns one of:  PR3Socket (online)   |   OfflineSocket (what runs today, 0xfae80..0xfcf7c)
        ▼
PR3Socket 0x61d80  : BlossomSocket 0x25530  : CommandSocket (RequestManyBlocks 0x276c8)  : BasicSocket 0x244c4  (TCP; JSON messages via cJSON)
```

* `BlossomSocket` speaks JSON over a TCP socket: rooms (`JoinRoom` 0x261c8, `LeaveRoom`, `SendToRoom` 0x25770, `GetRooms`),
  ping/clock sync (`SendPing`, `ReceivePing`, `CheckTime`), room variables (`ManageVars`), `LoginSuccessHandler`.
* `PR3Socket` adds the game protocol: `PR3Login` 0x62568, `GetLevelList` 0x62054, `GetUserList`, `GetMemberList`,
  `CreateMatch` 0x61aa8, `StartQuickJoin`/`StopQuickJoin`, `RequestMatches`, `ForceMatchStart`, `LeaveLobby`, `FinishMatch`,
  `FinishDrawing`, `WinHat`/`GetHat`/`LoseHat`, `UpdateCoins`, `RateLevel`, `EditUserList` (friends/ignore), `SaveAccountSettings`,
  `GetUserBans`, `GetUserPage`, `UnpublishLevel`.
* Message type strings found in the binary: `login`, `login_token`, `ping`, `chat`, `message(s)`, `matches`, `match_listing`,
  `create_match`, `ready`, `finish`, `finish_time`, `finish_match`, `finish_drawing`, `get_hat`, `get_level_list`,
  `get_member_list`, `get_user_list`, `get_user_page`, `get_user_bans`, `leave_lobby`, `start_quick_join`, `room_name`,
  `room_type`, `send_to_self`, `set_account_settings`.
* Game side: `MatchPage` (42 functions, the multiplayer race screen; `RemotePlayer`/`RecordedPlayer` replay other
  players' position updates), `MatchList`/`MatchListing`, `Chat`/`LobbyChat`/`FullChat`, `UserPopup`/`UserList(s)`,
  `MultiplayerMenu`, `WaitingMenu`, `Levels` (online level browser), `BlossomRoom`.
* `OfflineSocket` (already in the game) answers the same `RequestLevel` / `RequestBlock(s)` / `Send` interface from local
  files — a good reference for the message shapes (`OfflineSocket::RespondToMessage` 0xfc6b0, `CreateLevelData`,
  `CreateLevelList`, `CreateLoginMessage`).

## What the runtime does today (`PR3Core/runtime`)

* BSD `socket/connect/send/recv`, `pthread_create` return failure; `NSURLConnection`/`NSXMLParser` classes are absent,
  so `Sparkworkz::DataAccess` never gets a reply. Search for `TODO(multiplayer)` in `hle_stdio.c`.
* `SocketManager::IsOffline` returns true, which is why every menu takes the offline path.

## To do when this is picked up

1. Decide on a server: the original is gone. Options are a community re-implementation speaking the same protocol, or a
   small local/LAN server written from the message list above (use `OfflineSocket::RespondToMessage` as the template).
2. HLE work: pass BSD sockets through (`socket/connect/send/recv/select/close`, non-blocking mode, `getaddrinfo`),
   a real `pthread_create` (the reader thread calls back into guest code, so the recompiled code needs a lock around
   `rt_call`/the heap, or marshal the callbacks onto the main loop), `NSURLConnection` + `NSXMLParser` via `URLSession`
   (retain/release/autorelease are already modelled, see `hle_objc.c`).
3. Point `Sparkworkz::Initialize` at the new endpoint (the base URL strings live in `__cstring`; patch them in the
   code image copy rather than in the generated C).
4. Extend `tools/verify/survey.py` with a scripted MatchPage session against a stub server.
