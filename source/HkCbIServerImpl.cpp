﻿#include "wildcards.hh"
#include "hook.h"

namespace HkIServerImpl {
/**************************************************************************************************************
Called when player ship was created in space (after undock or login)
**************************************************************************************************************/



/**************************************************************************************************************
Called when one player hits a target with a gun
<Parameters>
ci:  only figured out where dwTargetShip is ...
**************************************************************************************************************/



/**************************************************************************************************************
Called when player moves his ship
**************************************************************************************************************/



/**************************************************************************************************************
Called when player has undocked and is now ready to fly
**************************************************************************************************************/

void LaunchComplete__Inner(unsigned int iBaseID, unsigned int iShip) {
    TRY_HOOK {
        uint iClientID = HkGetClientIDByShip(iShip);
        if (iClientID) {
            ClientInfo[iClientID].tmSpawnTime =
                timeInMS(); // save for anti-dockkill
            // is there spawnprotection?
            if (set_iAntiDockKill > 0)
                ClientInfo[iClientID].bSpawnProtected = true;
            else
                ClientInfo[iClientID].bSpawnProtected = false;
        }

        // event
        ProcessEvent(
            L"launch char=%s id=%d base=%s system=%s",
            (wchar_t *)Players.GetActiveCharacterName(iClientID), iClientID,
            HkGetBaseNickByID(ClientInfo[iClientID].iLastExitedBaseID).c_str(),
            HkGetPlayerSystem(iClientID).c_str());
    }
    CATCH_HOOK({})
}

/**************************************************************************************************************
Called when player selects a character
**************************************************************************************************************/

std::wstring g_wscCharBefore;
bool CharacterSelect__Inner(const CHARACTER_ID&cId, unsigned int clientID) {
    try {
        const wchar_t *wszCharname = (wchar_t *)Players.GetActiveCharacterName(clientID);
        g_wscCharBefore = wszCharname ? (wchar_t *)Players.GetActiveCharacterName(clientID) : L"";
        ClientInfo[clientID].iLastExitedBaseID = 0;
        ClientInfo[clientID].iTradePartner = 0;
    } catch (...) {
        HkAddKickLog(clientID, L"Corrupt charfile?");
        HkKick(ARG_CLIENTID(clientID));
        return false;
    }

    return true;
}

void CharacterSelect__InnerAfter(const CHARACTER_ID& cId, unsigned int clientID) {
    TRY_HOOK {
        std::wstring wscCharname = (wchar_t *)Players.GetActiveCharacterName(clientID);

        if (g_wscCharBefore.compare(wscCharname) != 0) {
            LoadUserCharSettings(clientID);

            if (set_bUserCmdHelp)
                PrintUserCmdText(clientID,
                                 L"To get a list of available commands, type "
                                 L"\"/help\" in chat.");

            // anti-cheat check
            std::list<CARGO_INFO> lstCargo;
            int iHold;
            HkEnumCargo(ARG_CLIENTID(clientID), lstCargo, iHold);
            for (auto &cargo : lstCargo) {
                if (cargo.iCount < 0) {
                    HkAddCheaterLog(wscCharname,
                                    L"Negative good-count, likely to have "
                                    L"cheated in the past");

                    wchar_t wszBuf[256];
                    swprintf_s(wszBuf, L"Possible cheating detected (%s)",
                               wscCharname.c_str());
                    HkMsgU(wszBuf);
                    HkBan(ARG_CLIENTID(clientID), true);
                    HkKick(ARG_CLIENTID(clientID));
                    return;
                }
            }

            // event
            CAccount *acc = Players.FindAccountFromClientID(clientID);
            std::wstring wscDir;
            HkGetAccountDirName(acc, wscDir);
            HKPLAYERINFO pi;
            HkGetPlayerInfo(ARG_CLIENTID(clientID), pi, false);
            ProcessEvent(L"login char=%s accountdirname=%s id=%d ip=%s",
                         wscCharname.c_str(), wscDir.c_str(), clientID,
                         pi.wscIP.c_str());
        }
    }
    CATCH_HOOK({})
}

void BaseEnter__Inner(unsigned int iBaseID, unsigned int iClientID) {
    TRY_HOOK {
        // autobuy
        if (set_bAutoBuy)
            HkPlayerAutoBuy(iClientID, iBaseID);
    }
    CATCH_HOOK({ AddLog("Exception in " __FUNCTION__ " on autobuy"); })
}
    
void BaseEnter__InnerAfter(unsigned int iBaseID, unsigned int iClientID) {
    TRY_HOOK {
        // adjust cash, this is necessary when cash was added while use was in
        // charmenu/had other char selected
        std::wstring wscCharname =
            ToLower((wchar_t *)Players.GetActiveCharacterName(iClientID));
        for (auto &i : ClientInfo[iClientID].lstMoneyFix) {
            if (!i.wscCharname.compare(wscCharname)) {
                HkAddCash(wscCharname, i.iAmount);
                ClientInfo[iClientID].lstMoneyFix.remove(i);
                break;
            }
        }

        // anti base-idle
        ClientInfo[iClientID].iBaseEnterTime = (uint)time(0);

        // event
        ProcessEvent(L"baseenter char=%s id=%d base=%s system=%s",
                     (wchar_t *)Players.GetActiveCharacterName(iClientID),
                     iClientID, HkGetBaseNickByID(iBaseID).c_str(),
                     HkGetPlayerSystem(iClientID).c_str());
    }
    CATCH_HOOK({})
}

/**************************************************************************************************************
Called when player exits base
**************************************************************************************************************/

void BaseExit__Inner(unsigned int iBaseID, unsigned int iClientID) {
    TRY_HOOK {
        ClientInfo[iClientID].iBaseEnterTime = 0;
        ClientInfo[iClientID].iLastExitedBaseID = iBaseID;
    }
    CATCH_HOOK({})
}

void BaseExit__InnerAfter(unsigned int iBaseID, unsigned int iClientID) {
    TRY_HOOK {
        const wchar_t *wszCharname =
            (wchar_t *)Players.GetActiveCharacterName(iClientID);

        // event
        ProcessEvent(L"baseexit char=%s id=%d base=%s system=%s",
                     (wchar_t *)Players.GetActiveCharacterName(iClientID),
                     iClientID, HkGetBaseNickByID(iBaseID).c_str(),
                     HkGetPlayerSystem(iClientID).c_str());
    }
    CATCH_HOOK({})
}
/**************************************************************************************************************
Called when player connects
**************************************************************************************************************/

void __stdcall OnConnect(unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    TRY_HOOK {
        // If ID is too high due to disconnect buffer time then manually drop
        // the connection.
        if (iClientID > MAX_CLIENT_ID) {
            AddLog("INFO: Blocking connect in " __FUNCTION__ " due to invalid "
                                                             "id, id=%u",
                   iClientID);
            CDPClientProxy *cdpClient = g_cClientProxyArray[iClientID - 1];
            if (!cdpClient)
                return;
            cdpClient->Disconnect();
            return;
        }

        // If this client is in the anti-F1 timeout then force the disconnect.
        if (ClientInfo[iClientID].tmF1TimeDisconnect > timeInMS()) {
            // manual disconnect
            CDPClientProxy *cdpClient = g_cClientProxyArray[iClientID - 1];
            if (!cdpClient)
                return;
            cdpClient->Disconnect();
            return;
        }

        ClientInfo[iClientID].iConnects++;
        ClearClientInfo(iClientID);
    }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_OnConnect, __stdcall,
                   (unsigned int iClientID), (iClientID));

    EXECUTE_SERVER_CALL(Server.OnConnect(iClientID));

    TRY_HOOK {
        // event
        std::wstring wscIP;
        HkGetPlayerIP(iClientID, wscIP);
        ProcessEvent(L"connect id=%d ip=%s", iClientID, wscIP.c_str());
    }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_OnConnect_AFTER, __stdcall,
                   (unsigned int iClientID), (iClientID));
}

/**************************************************************************************************************
Called when player disconnects
**************************************************************************************************************/

void __stdcall DisConnect(unsigned int iClientID, enum EFLConnection p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_UI(p2);

    std::wstring wscCharname;
    TRY_HOOK {
        if (!ClientInfo[iClientID].bDisconnected) {
            ClientInfo[iClientID].bDisconnected = true;
            ClientInfo[iClientID].lstMoneyFix.clear();
            ClientInfo[iClientID].iTradePartner = 0;

            // event
            const wchar_t *wszCharname =
                (const wchar_t *)Players.GetActiveCharacterName(iClientID);
            if (wszCharname)
                wscCharname = wszCharname;
            ProcessEvent(L"disconnect char=%s id=%d", wscCharname.c_str(),
                         iClientID);

            CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DisConnect, __stdcall,
                           (unsigned int iClientID, enum EFLConnection p2),
                           (iClientID, p2));
            EXECUTE_SERVER_CALL(Server.DisConnect(iClientID, p2));
            CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DisConnect_AFTER, __stdcall,
                           (unsigned int iClientID, enum EFLConnection p2),
                           (iClientID, p2));
        }
    }
    CATCH_HOOK({
        AddLog("ERROR: Exception in " __FUNCTION__ "@loc2 charname=%s "
                                                   "iClientID=%u",
               wstos(wscCharname).c_str(), iClientID);
    })
}

/**************************************************************************************************************
Called when trade is being terminated
**************************************************************************************************************/

void __stdcall TerminateTrade(unsigned int iClientID, int iAccepted) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_I(iAccepted);

    CHECK_FOR_DISCONNECT

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TerminateTrade, __stdcall,
                   (unsigned int iClientID, int iAccepted),
                   (iClientID, iAccepted));

    EXECUTE_SERVER_CALL(Server.TerminateTrade(iClientID, iAccepted));

    TRY_HOOK {
        if (iAccepted) { // save both chars to prevent cheating in case of
                         // server crash
            HkSaveChar(ARG_CLIENTID(iClientID));
            if (ClientInfo[iClientID].iTradePartner)
                HkSaveChar(ARG_CLIENTID(ClientInfo[iClientID].iTradePartner));
        }

        if (ClientInfo[iClientID].iTradePartner)
            ClientInfo[ClientInfo[iClientID].iTradePartner].iTradePartner = 0;
        ClientInfo[iClientID].iTradePartner = 0;
    }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TerminateTrade_AFTER, __stdcall,
                   (unsigned int iClientID, int iAccepted),
                   (iClientID, iAccepted));
}

/**************************************************************************************************************
Called when new trade request
**************************************************************************************************************/

void __stdcall InitiateTrade(unsigned int iClientID1, unsigned int iClientID2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID1);
    ISERVER_LOGARG_UI(iClientID2);

    TRY_HOOK {
        // save traders client-ids
        ClientInfo[iClientID1].iTradePartner = iClientID2;
        ClientInfo[iClientID2].iTradePartner = iClientID1;
    }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InitiateTrade, __stdcall,
                   (unsigned int iClientID1, unsigned int iClientID2),
                   (iClientID1, iClientID2));

    EXECUTE_SERVER_CALL(Server.InitiateTrade(iClientID1, iClientID2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InitiateTrade_AFTER, __stdcall,
                   (unsigned int iClientID1, unsigned int iClientID2),
                   (iClientID1, iClientID2));
}

/**************************************************************************************************************
Called when equipment is being activated/disabled
**************************************************************************************************************/

void __stdcall ActivateEquip(unsigned int iClientID,
                             struct XActivateEquip const &aq) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CHECK_FOR_DISCONNECT

    TRY_HOOK {

        std::list<CARGO_INFO> lstCargo;
        int iRem;
        HkEnumCargo(ARG_CLIENTID(iClientID), lstCargo, iRem);

        for (auto &cargo : lstCargo) {
            if (cargo.iID == aq.sID) {
                Archetype::Equipment *eq =
                    Archetype::GetEquipment(cargo.iArchID);
                EQ_TYPE eqType = HkGetEqType(eq);

                if (eqType == ET_ENGINE) {
                    ClientInfo[iClientID].bEngineKilled = !aq.bActivate;
                    if (!aq.bActivate)
                        ClientInfo[iClientID].bCruiseActivated =
                            false; // enginekill enabled
                }
            }
        }
    }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateEquip, __stdcall,
                   (unsigned int iClientID, struct XActivateEquip const &aq),
                   (iClientID, aq));

    EXECUTE_SERVER_CALL(Server.ActivateEquip(iClientID, aq));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateEquip_AFTER, __stdcall,
                   (unsigned int iClientID, struct XActivateEquip const &aq),
                   (iClientID, aq));
}

/**************************************************************************************************************
Called when cruise engine is being activated/disabled
**************************************************************************************************************/

void __stdcall ActivateCruise(unsigned int iClientID,
                              struct XActivateCruise const &ac) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CHECK_FOR_DISCONNECT

    TRY_HOOK { ClientInfo[iClientID].bCruiseActivated = ac.bActivate; }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateCruise, __stdcall,
                   (unsigned int iClientID, struct XActivateCruise const &ac),
                   (iClientID, ac));

    EXECUTE_SERVER_CALL(Server.ActivateCruise(iClientID, ac));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ActivateCruise_AFTER, __stdcall,
                   (unsigned int iClientID, struct XActivateCruise const &ac),
                   (iClientID, ac));
}

/**************************************************************************************************************
Called when thruster is being activated/disabled
**************************************************************************************************************/

void __stdcall ActivateThrusters(unsigned int iClientID,
                                 struct XActivateThrusters const &at) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CHECK_FOR_DISCONNECT

    TRY_HOOK { ClientInfo[iClientID].bThrusterActivated = at.bActivate; }
    CATCH_HOOK({})

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_ActivateThrusters, __stdcall,
        (unsigned int iClientID, struct XActivateThrusters const &at),
        (iClientID, at));

    EXECUTE_SERVER_CALL(Server.ActivateThrusters(iClientID, at));

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_ActivateThrusters_AFTER, __stdcall,
        (unsigned int iClientID, struct XActivateThrusters const &at),
        (iClientID, at));
}

/**************************************************************************************************************
Called when player sells good on a base
**************************************************************************************************************/

void __stdcall GFGoodSell(struct SGFGoodSellInfo const &gsi,
                          unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CHECK_FOR_DISCONNECT

    TRY_HOOK {
        // anti-cheat check
        std::list<CARGO_INFO> lstCargo;
        int iHold;
        HkEnumCargo(ARG_CLIENTID(iClientID), lstCargo, iHold);
        bool bLegalSell = false;
        for (auto &cargo : lstCargo) {
            if (cargo.iArchID == gsi.iArchID) {
                bLegalSell = true;
                if (abs(gsi.iCount) > cargo.iCount) {
                    wchar_t wszBuf[1000];

                    const wchar_t *wszCharname =
                        (wchar_t *)Players.GetActiveCharacterName(iClientID);
                    swprintf_s(
                        wszBuf,
                        L"Sold more good than possible item=%08x count=%u",
                        gsi.iArchID, gsi.iCount);
                    HkAddCheaterLog(wszCharname, wszBuf);

                    swprintf_s(wszBuf, L"Possible cheating detected (%s)",
                               wszCharname);
                    HkMsgU(wszBuf);
                    HkBan(ARG_CLIENTID(iClientID), true);
                    HkKick(ARG_CLIENTID(iClientID));
                    return;
                }
                break;
            }
        }
        if (!bLegalSell) {
            wchar_t wszBuf[1000];
            const wchar_t *wszCharname =
                (wchar_t *)Players.GetActiveCharacterName(iClientID);
            swprintf_s(
                wszBuf,
                L"Sold good player does not have (buggy test), item=%08x",
                gsi.iArchID);
            HkAddCheaterLog(wszCharname, wszBuf);

            // swprintf(wszBuf, L"Possible cheating detected (%s)",
            // wszCharname); HkMsgU(wszBuf); HkBan(ARG_CLIENTID(iClientID),
            // true); HkKick(ARG_CLIENTID(iClientID));
            return;
        }
    }
    CATCH_HOOK({
        AddLog("Exception in %s (iClientID=%u (%x))", __FUNCTION__, iClientID,
               Players.GetActiveCharacterName(iClientID));
    })

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodSell, __stdcall,
                   (struct SGFGoodSellInfo const &gsi, unsigned int iClientID),
                   (gsi, iClientID));

    EXECUTE_SERVER_CALL(Server.GFGoodSell(gsi, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodSell_AFTER, __stdcall,
                   (struct SGFGoodSellInfo const &gsi, unsigned int iClientID),
                   (gsi, iClientID));
}

/**************************************************************************************************************
Called when player connects or pushes f1
**************************************************************************************************************/

void __stdcall CharacterInfoReq(unsigned int iClientID, bool p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_UI(p2);

    CHECK_FOR_DISCONNECT

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CharacterInfoReq, __stdcall,
                   (unsigned int iClientID, bool p2), (iClientID, p2));

    try {
        if (!ClientInfo[iClientID].bCharSelected)
            ClientInfo[iClientID].bCharSelected = true;
        else { // pushed f1
            uint iShip = 0;
            pub::Player::GetShip(iClientID, iShip);
            if (iShip) { // in space
                ClientInfo[iClientID].tmF1Time = timeInMS() + set_iAntiF1;
                return;
            }
        }

        Server.CharacterInfoReq(iClientID, p2);
    } catch (...) { // something is wrong with charfile
        HkAddKickLog(iClientID, L"Corrupt charfile?");
        HkKick(ARG_CLIENTID(iClientID));
        return;
    }

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_CharacterInfoReq_AFTER, __stdcall,
                   (unsigned int iClientID, bool p2), (iClientID, p2));
}

/**************************************************************************************************************
Called when player jumps in system
**************************************************************************************************************/

void __stdcall JumpInComplete(unsigned int iSystemID, unsigned int iShip) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iSystemID);
    ISERVER_LOGARG_UI(iShip);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JumpInComplete, __stdcall,
                   (unsigned int iSystemID, unsigned int iShip),
                   (iSystemID, iShip));

    EXECUTE_SERVER_CALL(Server.JumpInComplete(iSystemID, iShip));

    TRY_HOOK {
        uint iClientID = HkGetClientIDByShip(iShip);
        if (!iClientID)
            return;

        // event
        ProcessEvent(L"jumpin char=%s id=%d system=%s",
                     (wchar_t *)Players.GetActiveCharacterName(iClientID),
                     iClientID, HkGetSystemNickByID(iSystemID).c_str());
    }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JumpInComplete_AFTER, __stdcall,
                   (unsigned int iSystemID, unsigned int iShip),
                   (iSystemID, iShip));
}

/**************************************************************************************************************
Called when player jumps out of system
**************************************************************************************************************/

void __stdcall SystemSwitchOutComplete(unsigned int iShip,
                                       unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iShip);
    ISERVER_LOGARG_UI(iClientID);

    CHECK_FOR_DISCONNECT

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SystemSwitchOutComplete, __stdcall,
                   (unsigned int iShip, unsigned int iClientID),
                   (iShip, iClientID));

    std::wstring wscSystem = HkGetPlayerSystem(iClientID);

    EXECUTE_SERVER_CALL(Server.SystemSwitchOutComplete(iShip, iClientID));

    TRY_HOOK {
        // event
        ProcessEvent(L"switchout char=%s id=%d system=%s",
                     (wchar_t *)Players.GetActiveCharacterName(iClientID),
                     iClientID, wscSystem.c_str());
    }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SystemSwitchOutComplete_AFTER,
                   __stdcall, (unsigned int iShip, unsigned int iClientID),
                   (iShip, iClientID));
}

/**************************************************************************************************************
Called when player logs in
**************************************************************************************************************/

void __stdcall Login(struct SLoginInfo const &li, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_WS(&li);
    ISERVER_LOGARG_UI(iClientID);

    TRY_HOOK {
        CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Login_BEFORE, __stdcall,
                       (struct SLoginInfo const &li, unsigned int iClientID),
                       (li, iClientID));

        Server.Login(li, iClientID);

        if (iClientID > MAX_CLIENT_ID)
            return; // lalala DisconnectDelay bug

        if (!HkIsValidClientID(iClientID))
            return; // player was kicked

        // Kick the player if the account ID doesn't exist. This is caused
        // by a duplicate log on.
        CAccount *acc = Players.FindAccountFromClientID(iClientID);
        if (acc && !acc->wszAccID) {
            acc->ForceLogout();
            return;
        }

        CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Login, __stdcall,
                       (struct SLoginInfo const &li, unsigned int iClientID),
                       (li, iClientID));

        // check for ip ban
        std::wstring wscIP;
        HkGetPlayerIP(iClientID, wscIP);

        for (auto &ban : set_setBans) {
            if (Wildcard::wildcardfit(wstos(ban).c_str(),
                                      wstos(wscIP).c_str())) {
                HkAddKickLog(iClientID, L"IP/Hostname ban(%s matches %s)",
                             wscIP.c_str(), ban.c_str());
                if (set_bBanAccountOnMatch)
                    HkBan(ARG_CLIENTID(iClientID), true);
                HkKick(ARG_CLIENTID(iClientID));
            }
        }

        // resolve
        RESOLVE_IP rip;
        rip.wscIP = wscIP;
        rip.wscHostname = L"";
        rip.iConnects =
            ClientInfo[iClientID].iConnects; // security check so that wrong
                                             // person doesnt get banned
        rip.iClientID = iClientID;
        EnterCriticalSection(&csIPResolve);
        g_lstResolveIPs.push_back(rip);
        LeaveCriticalSection(&csIPResolve);

        // count players
        struct PlayerData *pPD = 0;
        uint iPlayers = 0;
        while (pPD = Players.traverse_active(pPD))
            iPlayers++;

        if (iPlayers >
            (Players.GetMaxPlayerCount() -
             set_iReservedSlots)) { // check if player has a reserved slot
            CAccount *acc = Players.FindAccountFromClientID(iClientID);
            std::wstring wscDir;
            HkGetAccountDirName(acc, wscDir);
            std::string scUserFile =
                scAcctPath + wstos(wscDir) + "\\flhookuser.ini";

            bool bReserved =
                IniGetB(scUserFile, "Settings", "ReservedSlot", false);
            if (!bReserved) {
                HkKick(acc);
                return;
            }
        }

        LoadUserSettings(iClientID);

        // log
        if (set_bLogConnects)
            HkAddConnectLog(iClientID, wscIP);
    }
    CATCH_HOOK({
        CAccount *acc = Players.FindAccountFromClientID(iClientID);
        if (acc) {
            acc->ForceLogout();
        }
    })

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Login_AFTER, __stdcall,
                   (struct SLoginInfo const &li, unsigned int iClientID),
                   (li, iClientID));
}

/**************************************************************************************************************
Called on item spawn
**************************************************************************************************************/

void __stdcall MineAsteroid(unsigned int p1, class Vector const &vPos,
                            unsigned int iLookID, unsigned int iGoodID,
                            unsigned int iCount, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_UI(vPos);
    ISERVER_LOGARG_UI(iLookID);
    ISERVER_LOGARG_UI(iGoodID);
    ISERVER_LOGARG_UI(iCount);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_MineAsteroid, __stdcall,
                   (unsigned int p1, class Vector const &vPos,
                    unsigned int iLookID, unsigned int iGoodID,
                    unsigned int iCount, unsigned int iClientID),
                   (p1, vPos, iLookID, iGoodID, iCount, iClientID));

    EXECUTE_SERVER_CALL(
        Server.MineAsteroid(p1, vPos, iLookID, iGoodID, iCount, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_MineAsteroid_AFTER, __stdcall,
                   (unsigned int p1, class Vector const &vPos,
                    unsigned int iLookID, unsigned int iGoodID,
                    unsigned int iCount, unsigned int iClientID),
                   (p1, vPos, iLookID, iGoodID, iCount, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall GoTradelane(unsigned int iClientID,
                           struct XGoTradelane const &gtl) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    TRY_HOOK { ClientInfo[iClientID].bTradelane = true; }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GoTradelane, __stdcall,
                   (unsigned int iClientID, struct XGoTradelane const &gtl),
                   (iClientID, gtl));

    TRY_HOOK { Server.GoTradelane(iClientID, gtl); }
    CATCH_HOOK({
        uint iSystem;
        pub::Player::GetSystem(iClientID, iSystem);
        AddLog("ERROR: Exception in HkIServerImpl::GoTradelane charname=%s "
               "sys=%08x arch=%08x arch2=%08x",
               wstos((const wchar_t *)Players.GetActiveCharacterName(iClientID))
                   .c_str(),
               iSystem, gtl.iTradelaneSpaceObj1, gtl.iTradelaneSpaceObj2);
    })

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GoTradelane_AFTER, __stdcall,
                   (unsigned int iClientID, struct XGoTradelane const &gtl),
                   (iClientID, gtl));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall StopTradelane(unsigned int iClientID, unsigned int p2,
                             unsigned int p3, unsigned int p4) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_UI(p2);
    ISERVER_LOGARG_UI(p3);
    ISERVER_LOGARG_UI(p4);

    TRY_HOOK { ClientInfo[iClientID].bTradelane = false; }
    CATCH_HOOK({})

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradelane, __stdcall,
                   (unsigned int iClientID, unsigned int p2, unsigned int p3,
                    unsigned int p4),
                   (iClientID, p2, p3, p4));

    EXECUTE_SERVER_CALL(Server.StopTradelane(iClientID, p2, p3, p4));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradelane_AFTER, __stdcall,
                   (unsigned int iClientID, unsigned int p2, unsigned int p3,
                    unsigned int p4),
                   (iClientID, p2, p3, p4));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall AbortMission(unsigned int iClientID, unsigned int p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_UI(p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AbortMission, __stdcall,
                   (unsigned int iClientID, unsigned int p2), (iClientID, p2));

    EXECUTE_SERVER_CALL(Server.AbortMission(iClientID, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AbortMission_AFTER, __stdcall,
                   (unsigned int iClientID, unsigned int p2), (iClientID, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall AcceptTrade(unsigned int iClientID, bool p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_UI(p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AcceptTrade, __stdcall,
                   (unsigned int iClientID, bool p2), (iClientID, p2));

    EXECUTE_SERVER_CALL(Server.AcceptTrade(iClientID, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AcceptTrade_AFTER, __stdcall,
                   (unsigned int iClientID, bool p2), (iClientID, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall AddTradeEquip(unsigned int iClientID,
                             struct EquipDesc const &ed) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AddTradeEquip, __stdcall,
                   (unsigned int iClientID, struct EquipDesc const &ed),
                   (iClientID, ed));

    EXECUTE_SERVER_CALL(Server.AddTradeEquip(iClientID, ed));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_AddTradeEquip_AFTER, __stdcall,
                   (unsigned int iClientID, struct EquipDesc const &ed),
                   (iClientID, ed));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall BaseInfoRequest(unsigned int p1, unsigned int p2, bool p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);
    ISERVER_LOGARG_UI(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseInfoRequest, __stdcall,
                   (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.BaseInfoRequest(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_BaseInfoRequest_AFTER, __stdcall,
                   (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall CharacterSkipAutosave(unsigned int iClientID) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall CommComplete(unsigned int p1, unsigned int p2, unsigned int p3,
                            enum CommResult cr) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall CreateNewCharacter(struct SCreateCharacterInfo const &scci,
                                  unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_CreateNewCharacter, __stdcall,
        (struct SCreateCharacterInfo const &scci, unsigned int iClientID),
        (scci, iClientID));

    EXECUTE_SERVER_CALL(Server.CreateNewCharacter(scci, iClientID));

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_CreateNewCharacter_AFTER, __stdcall,
        (struct SCreateCharacterInfo const &scci, unsigned int iClientID),
        (scci, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall DelTradeEquip(unsigned int iClientID,
                             struct EquipDesc const &ed) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DelTradeEquip, __stdcall,
                   (unsigned int iClientID, struct EquipDesc const &ed),
                   (iClientID, ed));

    EXECUTE_SERVER_CALL(Server.DelTradeEquip(iClientID, ed));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DelTradeEquip_AFTER, __stdcall,
                   (unsigned int iClientID, struct EquipDesc const &ed),
                   (iClientID, ed));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall DestroyCharacter(struct CHARACTER_ID const &cId,
                                unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_S(&cId);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DestroyCharacter, __stdcall,
                   (struct CHARACTER_ID const &cId, unsigned int iClientID),
                   (cId, iClientID));

    EXECUTE_SERVER_CALL(Server.DestroyCharacter(cId, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_DestroyCharacter_AFTER, __stdcall,
                   (struct CHARACTER_ID const &cId, unsigned int iClientID),
                   (cId, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall Dock(unsigned int const &p1, unsigned int const &p2) {
    // anticheat - never let the client manually dock somewhere
    return;
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall DumpPacketStats(char const *p1) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ElapseTime(float p1) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall GFGoodBuy(struct SGFGoodBuyInfo const &gbi,
                         unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodBuy, __stdcall,
                   (struct SGFGoodBuyInfo const &gbi, unsigned int iClientID),
                   (gbi, iClientID));

    EXECUTE_SERVER_CALL(Server.GFGoodBuy(gbi, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFGoodBuy_AFTER, __stdcall,
                   (struct SGFGoodBuyInfo const &gbi, unsigned int iClientID),
                   (gbi, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall GFGoodVaporized(struct SGFGoodVaporizedInfo const &gvi,
                               unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_GFGoodVaporized, __stdcall,
        (struct SGFGoodVaporizedInfo const &gvi, unsigned int iClientID),
        (gvi, iClientID));

    EXECUTE_SERVER_CALL(Server.GFGoodVaporized(gvi, iClientID));

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_GFGoodVaporized_AFTER, __stdcall,
        (struct SGFGoodVaporizedInfo const &gvi, unsigned int iClientID),
        (gvi, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall GFObjSelect(unsigned int p1, unsigned int p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFObjSelect, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));

    EXECUTE_SERVER_CALL(Server.GFObjSelect(p1, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_GFObjSelect_AFTER, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

unsigned int __stdcall GetServerID(void) {
    ISERVER_LOG();

    return Server.GetServerID();
}

/**************************************************************************************************************
**************************************************************************************************************/

char const *__stdcall GetServerSig(void) {
    ISERVER_LOG();

    return Server.GetServerSig();
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall GetServerStats(struct ServerStats &ss) {
    ISERVER_LOG();

    Server.GetServerStats(ss);
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall Hail(unsigned int p1, unsigned int p2, unsigned int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);
    ISERVER_LOGARG_UI(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Hail, __stdcall,
                   (unsigned int p1, unsigned int p2, unsigned int p3),
                   (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.Hail(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Hail_AFTER, __stdcall,
                   (unsigned int p1, unsigned int p2, unsigned int p3),
                   (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall InterfaceItemUsed(unsigned int p1, unsigned int p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InterfaceItemUsed, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));

    EXECUTE_SERVER_CALL(Server.InterfaceItemUsed(p1, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_InterfaceItemUsed_AFTER, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall JettisonCargo(unsigned int iClientID,
                             struct XJettisonCargo const &jc) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JettisonCargo, __stdcall,
                   (unsigned int iClientID, struct XJettisonCargo const &jc),
                   (iClientID, jc));

    EXECUTE_SERVER_CALL(Server.JettisonCargo(iClientID, jc));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_JettisonCargo_AFTER, __stdcall,
                   (unsigned int iClientID, struct XJettisonCargo const &jc),
                   (iClientID, jc));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall LocationEnter(unsigned int p1, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationEnter, __stdcall,
                   (unsigned int p1, unsigned int iClientID), (p1, iClientID));

    EXECUTE_SERVER_CALL(Server.LocationEnter(p1, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationEnter_AFTER, __stdcall,
                   (unsigned int p1, unsigned int iClientID), (p1, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall LocationExit(unsigned int p1, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationExit, __stdcall,
                   (unsigned int p1, unsigned int iClientID), (p1, iClientID));

    EXECUTE_SERVER_CALL(Server.LocationExit(p1, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationExit_AFTER, __stdcall,
                   (unsigned int p1, unsigned int iClientID), (p1, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall LocationInfoRequest(unsigned int p1, unsigned int p2, bool p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);
    ISERVER_LOGARG_UI(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationInfoRequest, __stdcall,
                   (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.LocationInfoRequest(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_LocationInfoRequest_AFTER, __stdcall,
                   (unsigned int p1, unsigned int p2, bool p3), (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall MissionResponse(unsigned int p1, unsigned long p2, bool p3,
                               unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);
    ISERVER_LOGARG_UI(p3);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_MissionResponse, __stdcall,
        (unsigned int p1, unsigned long p2, bool p3, unsigned int iClientID),
        (p1, p2, p3, iClientID));

    EXECUTE_SERVER_CALL(Server.MissionResponse(p1, p2, p3, iClientID));

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_MissionResponse_AFTER, __stdcall,
        (unsigned int p1, unsigned long p2, bool p3, unsigned int iClientID),
        (p1, p2, p3, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall MissionSaveB(unsigned int iClientID, unsigned long p2) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall PopUpDialog(unsigned int p1, unsigned int p2) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall RTCDone(unsigned int p1, unsigned int p2) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqAddItem(unsigned int p1, char const *p2, int p3, float p4,
                          bool p5, unsigned int p6) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_UI(p3);
    ISERVER_LOGARG_F(p4);
    ISERVER_LOGARG_UI(p5);
    ISERVER_LOGARG_UI(p6);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqAddItem, __stdcall,
                   (unsigned int p1, char const *p2, int p3, float p4, bool p5,
                    unsigned int p6),
                   (p1, p2, p3, p4, p5, p6));

    EXECUTE_SERVER_CALL(Server.ReqAddItem(p1, p2, p3, p4, p5, p6));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqAddItem_AFTER, __stdcall,
                   (unsigned int p1, char const *p2, int p3, float p4, bool p5,
                    unsigned int p6),
                   (p1, p2, p3, p4, p5, p6));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqChangeCash(int p1, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqChangeCash, __stdcall,
                   (int p1, unsigned int iClientID), (p1, iClientID));

    EXECUTE_SERVER_CALL(Server.ReqChangeCash(p1, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqChangeCash_AFTER, __stdcall,
                   (int p1, unsigned int iClientID), (p1, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqCollisionGroups(
    class st6::list<struct CollisionGroupDesc,
                    class st6::allocator<struct CollisionGroupDesc>> const &p1,
    unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_ReqCollisionGroups, __stdcall,
        (class st6::list<struct CollisionGroupDesc,
                         class st6::allocator<struct CollisionGroupDesc>> const
             &p1,
         unsigned int iClientID),
        (p1, iClientID));

    EXECUTE_SERVER_CALL(Server.ReqCollisionGroups(p1, iClientID));

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_ReqCollisionGroups_AFTER, __stdcall,
        (class st6::list<struct CollisionGroupDesc,
                         class st6::allocator<struct CollisionGroupDesc>> const
             &p1,
         unsigned int iClientID),
        (p1, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqDifficultyScale(float p1, unsigned int iClientID) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqEquipment(class EquipDescList const &edl,
                            unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqEquipment, __stdcall,
                   (class EquipDescList const &edl, unsigned int iClientID),
                   (edl, iClientID));

    EXECUTE_SERVER_CALL(Server.ReqEquipment(edl, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqEquipment_AFTER, __stdcall,
                   (class EquipDescList const &edl, unsigned int iClientID),
                   (edl, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqHullStatus(float p1, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_F(p1);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqHullStatus, __stdcall,
                   (float p1, unsigned int iClientID), (p1, iClientID));

    EXECUTE_SERVER_CALL(Server.ReqHullStatus(p1, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqHullStatus_AFTER, __stdcall,
                   (float p1, unsigned int iClientID), (p1, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqModifyItem(unsigned short p1, char const *p2, int p3,
                             float p4, bool p5, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);
    ISERVER_LOGARG_F(p4);
    ISERVER_LOGARG_UI(p5);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqModifyItem, __stdcall,
                   (unsigned short p1, char const *p2, int p3, float p4,
                    bool p5, unsigned int iClientID),
                   (p1, p2, p3, p4, p5, iClientID));

    EXECUTE_SERVER_CALL(Server.ReqModifyItem(p1, p2, p3, p4, p5, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqModifyItem_AFTER, __stdcall,
                   (unsigned short p1, char const *p2, int p3, float p4,
                    bool p5, unsigned int iClientID),
                   (p1, p2, p3, p4, p5, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqRemoveItem(unsigned short p1, int p2,
                             unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_I(p2);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqRemoveItem, __stdcall,
                   (unsigned short p1, int p2, unsigned int iClientID),
                   (p1, p2, iClientID));

    EXECUTE_SERVER_CALL(Server.ReqRemoveItem(p1, p2, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqRemoveItem_AFTER, __stdcall,
                   (unsigned short p1, int p2, unsigned int iClientID),
                   (p1, p2, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqSetCash(int p1, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_I(p1);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqSetCash, __stdcall,
                   (int p1, unsigned int iClientID), (p1, iClientID));

    EXECUTE_SERVER_CALL(Server.ReqSetCash(p1, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqSetCash_AFTER, __stdcall,
                   (int p1, unsigned int iClientID), (p1, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall ReqShipArch(unsigned int p1, unsigned int p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqShipArch, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));

    EXECUTE_SERVER_CALL(Server.ReqShipArch(p1, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_ReqShipArch_AFTER, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall RequestBestPath(unsigned int p1, unsigned char *p2, int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestBestPath, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.RequestBestPath(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestBestPath_AFTER, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

// Cancel a ship maneuver (goto, dock, formation).
// p1 = iType? ==0 if docking, ==1 if formation
void __stdcall RequestCancel(int iType, unsigned int iShip, unsigned int p3,
                             unsigned long p4, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_I(iType);
    ISERVER_LOGARG_UI(iShip);
    ISERVER_LOGARG_UI(p3);
    ISERVER_LOGARG_UI(p4);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCancel, __stdcall,
                   (int iType, unsigned int iShip, unsigned int p3,
                    unsigned long p4, unsigned int iClientID),
                   (iType, iShip, p3, p4, iClientID));

    EXECUTE_SERVER_CALL(Server.RequestCancel(iType, iShip, p3, p4, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCancel_AFTER, __stdcall,
                   (int iType, unsigned int iShip, unsigned int p3,
                    unsigned long p4, unsigned int iClientID),
                   (iType, iShip, p3, p4, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall RequestCreateShip(unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCreateShip, __stdcall,
                   (unsigned int iClientID), (iClientID));

    EXECUTE_SERVER_CALL(Server.RequestCreateShip(iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestCreateShip_AFTER, __stdcall,
                   (unsigned int iClientID), (iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

/// Called upon flight maneuver (goto, dock, formation).
/// p1 = iType? ==0 if docking, ==1 if formation
/// p2 = iShip of person docking
/// p3 = iShip of dock/formation target
/// p4 seems to be 0 all the time
/// p5 seems to be 0 all the time
/// p6 = iClientID
void __stdcall RequestEvent(int iType, unsigned int iShip,
                            unsigned int iShipTarget, unsigned int p4,
                            unsigned long p5, unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_I(iType);
    ISERVER_LOGARG_UI(iShip);
    ISERVER_LOGARG_UI(iShipTarget);
    ISERVER_LOGARG_UI(p4);
    ISERVER_LOGARG_UI(p5);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestEvent, __stdcall,
                   (int iType, unsigned int iShip, unsigned int iShipTarget,
                    unsigned int p4, unsigned long p5, unsigned int iClientID),
                   (iType, iShip, iShipTarget, p4, p5, iClientID));

    EXECUTE_SERVER_CALL(
        Server.RequestEvent(iType, iShip, iShipTarget, p4, p5, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestEvent_AFTER, __stdcall,
                   (int iType, unsigned int iShip, unsigned int iShipTarget,
                    unsigned int p4, unsigned long p5, unsigned int iClientID),
                   (iType, iShip, iShipTarget, p4, p5, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall RequestGroupPositions(unsigned int p1, unsigned char *p2,
                                     int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestGroupPositions, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.RequestGroupPositions(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestGroupPositions_AFTER, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall RequestPlayerStats(unsigned int p1, unsigned char *p2, int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestPlayerStats, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.RequestPlayerStats(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestPlayerStats_AFTER, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall RequestRankLevel(unsigned int p1, unsigned char *p2, int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestRankLevel, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.RequestRankLevel(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestRankLevel_AFTER, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall RequestTrade(unsigned int p1, unsigned int p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestTrade, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));

    EXECUTE_SERVER_CALL(Server.RequestTrade(p1, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_RequestTrade_AFTER, __stdcall,
                   (unsigned int p1, unsigned int p2), (p1, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SPBadLandsObjCollision(
    struct SSPBadLandsObjCollisionInfo const &p1, unsigned int iClientID) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

/// Called when ship starts jump gate/hole acceleration but before system switch
/// out.
void __stdcall SPRequestInvincibility(unsigned int iShip, bool p2,
                                      enum InvincibilityReason p3,
                                      unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iShip);
    ISERVER_LOGARG_UI(p2);
    ISERVER_LOGARG_UI(p3);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestInvincibility, __stdcall,
                   (unsigned int iShip, bool p2, enum InvincibilityReason p3,
                    unsigned int iClientID),
                   (iShip, p2, p3, iClientID));

    EXECUTE_SERVER_CALL(
        Server.SPRequestInvincibility(iShip, p2, p3, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestInvincibility_AFTER, __stdcall,
                   (unsigned int iShip, bool p2, enum InvincibilityReason p3,
                    unsigned int iClientID),
                   (iShip, p2, p3, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SPRequestUseItem(struct SSPUseItem const &p1,
                                unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestUseItem, __stdcall,
                   (struct SSPUseItem const &p1, unsigned int iClientID),
                   (p1, iClientID));

    EXECUTE_SERVER_CALL(Server.SPRequestUseItem(p1, iClientID));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SPRequestUseItem_AFTER, __stdcall,
                   (struct SSPUseItem const &p1, unsigned int iClientID),
                   (p1, iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SPScanCargo(unsigned int const &p1, unsigned int const &p2,
                           unsigned int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    ISERVER_LOGARG_UI(p2);
    ISERVER_LOGARG_UI(p3);

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_SPScanCargo, __stdcall,
        (unsigned int const &p1, unsigned int const &p2, unsigned int p3),
        (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.SPScanCargo(p1, p2, p3));

    CALL_PLUGINS_V(
        PLUGIN_HkIServerImpl_SPScanCargo_AFTER, __stdcall,
        (unsigned int const &p1, unsigned int const &p2, unsigned int p3),
        (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SaveGame(struct CHARACTER_ID const &cId,
                        unsigned short const *p2, unsigned int p3) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SetInterfaceState(unsigned int p1, unsigned char *p2, int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(p1);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetInterfaceState, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));

    EXECUTE_SERVER_CALL(Server.SetInterfaceState(p1, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetInterfaceState_AFTER, __stdcall,
                   (unsigned int p1, unsigned char *p2, int p3), (p1, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SetManeuver(unsigned int iClientID,
                           struct XSetManeuver const &p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetManeuver, __stdcall,
                   (unsigned int iClientID, struct XSetManeuver const &p2),
                   (iClientID, p2));

    EXECUTE_SERVER_CALL(Server.SetManeuver(iClientID, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetManeuver_AFTER, __stdcall,
                   (unsigned int iClientID, struct XSetManeuver const &p2),
                   (iClientID, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SetMissionLog(unsigned int iClientID, unsigned char *p2,
                             int p3) {
    return; // not used
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SetTarget(unsigned int iClientID, struct XSetTarget const &p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTarget, __stdcall,
                   (unsigned int iClientID, struct XSetTarget const &p2),
                   (iClientID, p2));

    EXECUTE_SERVER_CALL(Server.SetTarget(iClientID, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTarget_AFTER, __stdcall,
                   (unsigned int iClientID, struct XSetTarget const &p2),
                   (iClientID, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SetTradeMoney(unsigned int iClientID, unsigned long p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    ISERVER_LOGARG_UI(p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTradeMoney, __stdcall,
                   (unsigned int iClientID, unsigned long p2), (iClientID, p2));

    EXECUTE_SERVER_CALL(Server.SetTradeMoney(iClientID, p2));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetTradeMoney_AFTER, __stdcall,
                   (unsigned int iClientID, unsigned long p2), (iClientID, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SetVisitedState(unsigned int iClientID, unsigned char *p2,
                               int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetVisitedState, __stdcall,
                   (unsigned int iClientID, unsigned char *p2, int p3),
                   (iClientID, p2, p3));

    EXECUTE_SERVER_CALL(Server.SetVisitedState(iClientID, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetVisitedState_AFTER, __stdcall,
                   (unsigned int iClientID, unsigned char *p2, int p3),
                   (iClientID, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall SetWeaponGroup(unsigned int iClientID, unsigned char *p2,
                              int p3) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);
    //	ISERVER_LOGARG_S(p2);
    ISERVER_LOGARG_I(p3);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetWeaponGroup, __stdcall,
                   (unsigned int iClientID, unsigned char *p2, int p3),
                   (iClientID, p2, p3));

    EXECUTE_SERVER_CALL(Server.SetWeaponGroup(iClientID, p2, p3));

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_SetWeaponGroup_AFTER, __stdcall,
                   (unsigned int iClientID, unsigned char *p2, int p3),
                   (iClientID, p2, p3));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall Shutdown(void) {
    ISERVER_LOG();

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_Shutdown, __stdcall, (), ());

    Server.Shutdown();

    FLHookShutdown();
}

/**************************************************************************************************************
**************************************************************************************************************/

bool __stdcall Startup(struct SStartupInfo const &p1) {
    FLHookInit_Pre();

    // The maximum number of players we can support is MAX_CLIENT_ID
    // Add one to the maximum number to allow renames
    int iMaxPlayers = MAX_CLIENT_ID + 1;

    // Startup the server with this number of players.
    char *pAddress = ((char *)hModServer + ADDR_SRV_PLAYERDBMAXPLAYERSPATCH);
    char szNOP[] = {'\x90'};
    char szMOVECX[] = {'\xB9'};
    WriteProcMem(pAddress, szMOVECX, sizeof(szMOVECX));
    WriteProcMem(pAddress + 1, &iMaxPlayers, sizeof(iMaxPlayers));
    WriteProcMem(pAddress + 5, szNOP, sizeof(szNOP));

    CALL_PLUGINS_NORET(PLUGIN_HkIServerImpl_Startup, __stdcall,
                       (struct SStartupInfo const &p1), (p1));

    bool bRet = Server.Startup(p1);

    // Patch to set maximum number of players to connect. This is normally
    // less than MAX_CLIENT_ID
    pAddress = ((char *)hModServer + ADDR_SRV_PLAYERDBMAXPLAYERS);
    WriteProcMem(pAddress, (void *)&p1.iMaxPlayers, sizeof(iMaxPlayers));

    // read base market data from ini
    HkLoadBaseMarket();

    ISERVER_LOG();

    CALL_PLUGINS_NORET(PLUGIN_HkIServerImpl_Startup_AFTER, __stdcall,
                       (struct SStartupInfo const &p1), (p1));

    return bRet;
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall StopTradeRequest(unsigned int iClientID) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradeRequest, __stdcall,
                   (unsigned int iClientID), (iClientID));

    Server.StopTradeRequest(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_StopTradeRequest_AFTER, __stdcall,
                   (unsigned int iClientID), (iClientID));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall TractorObjects(unsigned int iClientID,
                              struct XTractorObjects const &p2) {
    ISERVER_LOG();
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TractorObjects, __stdcall,
                   (unsigned int iClientID, struct XTractorObjects const &p2),
                   (iClientID, p2));

    Server.TractorObjects(iClientID, p2);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TractorObjects_AFTER, __stdcall,
                   (unsigned int iClientID, struct XTractorObjects const &p2),
                   (iClientID, p2));
}

/**************************************************************************************************************
**************************************************************************************************************/

void __stdcall TradeResponse(unsigned char const *p1, int p2,
                             unsigned int iClientID) {
    ISERVER_LOG();
    ///	ISERVER_LOGARG_S(p1);
    ISERVER_LOGARG_I(p2);
    ISERVER_LOGARG_UI(iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TradeResponse, __stdcall,
                   (unsigned char const *p1, int p2, unsigned int iClientID),
                   (p1, p2, iClientID));

    Server.TradeResponse(p1, p2, iClientID);

    CALL_PLUGINS_V(PLUGIN_HkIServerImpl_TradeResponse_AFTER, __stdcall,
                   (unsigned char const *p1, int p2, unsigned int iClientID),
                   (p1, p2, iClientID));
}

/**************************************************************************************************************
IServImpl hook entries
**************************************************************************************************************/

HOOKENTRY hookEntries[85] = {
    {(FARPROC)SubmitChat, -0x08, 0},
    {(FARPROC)FireWeapon, 0x000, 0},
    {(FARPROC)ActivateEquip, 0x004, 0},
    {(FARPROC)ActivateCruise, 0x008, 0},
    {(FARPROC)ActivateThrusters, 0x00C, 0},
    {(FARPROC)SetTarget, 0x010, 0},
    {(FARPROC)TractorObjects, 0x014, 0},
    {(FARPROC)GoTradelane, 0x018, 0},
    {(FARPROC)StopTradelane, 0x01C, 0},
    {(FARPROC)JettisonCargo, 0x020, 0},
    {(FARPROC)ElapseTime, 0x030, 0},
    {(FARPROC)DisConnect, 0x040, 0},
    {(FARPROC)OnConnect, 0x044, 0},
    {(FARPROC)Login, 0x048, 0},
    {(FARPROC)CharacterInfoReq, 0x04C, 0},
    {(FARPROC)CharacterSelect, 0x050, 0},
    {(FARPROC)CreateNewCharacter, 0x058, 0},
    {(FARPROC)DestroyCharacter, 0x05C, 0},
    {(FARPROC)CharacterSkipAutosave, 0x060, 0},
    {(FARPROC)ReqShipArch, 0x064, 0},
    {(FARPROC)ReqHullStatus, 0x068, 0},
    {(FARPROC)ReqCollisionGroups, 0x06C, 0},
    {(FARPROC)ReqEquipment, 0x070, 0},
    {(FARPROC)ReqAddItem, 0x078, 0},
    {(FARPROC)ReqRemoveItem, 0x07C, 0},
    {(FARPROC)ReqModifyItem, 0x080, 0},
    {(FARPROC)ReqSetCash, 0x084, 0},
    {(FARPROC)ReqChangeCash, 0x088, 0},
    {(FARPROC)BaseEnter, 0x08C, 0},
    {(FARPROC)BaseExit, 0x090, 0},
    {(FARPROC)LocationEnter, 0x094, 0},
    {(FARPROC)LocationExit, 0x098, 0},
    {(FARPROC)BaseInfoRequest, 0x09C, 0},
    {(FARPROC)LocationInfoRequest, 0x0A0, 0},
    {(FARPROC)GFObjSelect, 0x0A4, 0},
    {(FARPROC)GFGoodVaporized, 0x0A8, 0},
    {(FARPROC)MissionResponse, 0x0AC, 0},
    {(FARPROC)TradeResponse, 0x0B0, 0},
    {(FARPROC)GFGoodBuy, 0x0B4, 0},
    {(FARPROC)GFGoodSell, 0x0B8, 0},
    {(FARPROC)SystemSwitchOutComplete, 0x0BC, 0},
    {(FARPROC)PlayerLaunch, 0x0C0, 0},
    {(FARPROC)LaunchComplete, 0x0C4, 0},
    {(FARPROC)JumpInComplete, 0x0C8, 0},
    {(FARPROC)Hail, 0x0CC, 0},
    {(FARPROC)SPObjUpdate, 0x0D0, 0},
    {(FARPROC)SPMunitionCollision, 0x0D4, 0},
    {(FARPROC)SPBadLandsObjCollision, 0x0D8, 0},
    {(FARPROC)SPObjCollision, 0x0DC, 0},
    {(FARPROC)SPRequestUseItem, 0x0E0, 0},
    {(FARPROC)SPRequestInvincibility, 0x0E4, 0},
    {(FARPROC)SaveGame, 0x0E8, 0},
    {(FARPROC)MissionSaveB, 0x0EC, 0},
    {(FARPROC)RequestEvent, 0x0F0, 0},
    {(FARPROC)RequestCancel, 0x0F4, 0},
    {(FARPROC)MineAsteroid, 0x0F8, 0},
    {(FARPROC)CommComplete, 0x0FC, 0},
    {(FARPROC)RequestCreateShip, 0x100, 0},
    {(FARPROC)SPScanCargo, 0x104, 0},
    {(FARPROC)SetManeuver, 0x108, 0},
    {(FARPROC)InterfaceItemUsed, 0x10C, 0},
    {(FARPROC)AbortMission, 0x110, 0},
    {(FARPROC)RTCDone, 0x114, 0},
    {(FARPROC)SetWeaponGroup, 0x118, 0},
    {(FARPROC)SetVisitedState, 0x11C, 0},
    {(FARPROC)RequestBestPath, 0x120, 0},
    {(FARPROC)RequestPlayerStats, 0x124, 0},
    {(FARPROC)PopUpDialog, 0x128, 0},
    {(FARPROC)RequestGroupPositions, 0x12C, 0},
    {(FARPROC)SetMissionLog, 0x130, 0},
    {(FARPROC)SetInterfaceState, 0x134, 0},
    {(FARPROC)RequestRankLevel, 0x138, 0},
    {(FARPROC)InitiateTrade, 0x13C, 0},
    {(FARPROC)TerminateTrade, 0x140, 0},
    {(FARPROC)AcceptTrade, 0x144, 0},
    {(FARPROC)SetTradeMoney, 0x148, 0},
    {(FARPROC)AddTradeEquip, 0x14C, 0},
    {(FARPROC)DelTradeEquip, 0x150, 0},
    {(FARPROC)RequestTrade, 0x154, 0},
    {(FARPROC)StopTradeRequest, 0x158, 0},
    {(FARPROC)ReqDifficultyScale, 0x15C, 0},
    {(FARPROC)GetServerID, 0x160, 0},
    {(FARPROC)GetServerSig, 0x164, 0},
    {(FARPROC)DumpPacketStats, 0x168, 0},
    {(FARPROC)Dock, 0x16C, 0},
};

} // namespace HkIServerImpl
