﻿#include "Main.h"

void DeleteBase(PlayerBase *base) {
    // If there are players online and in the base then force them to launch to
    // space
    struct PlayerData *pd = 0;
    while (pd = Players.traverse_active(pd)) {
        uint client = pd->iOnlineID;
        if (HkIsInCharSelectMenu(client))
            continue;

        // If this player is in space, set the reputations.
        if (!pd->iShipID)
            continue;

        // Get state if player is in player base and  reset the commodity list
        // and send a dummy entry if there are no commodities in the market
        if (clients[client].player_base == base->base) {
            // Force the ship to launch to space as the base has been destroyed
            DeleteDockState(client);
            SendResetMarketOverride(client);
            ForceLaunch(client);
        }
    }

    // Remove the base.
    _unlink(base->path.c_str());
    player_bases.erase(base->base);
    delete base;
}

void LoadDockState(uint client) {
    clients[client].player_base = HkGetCharacterIniUint(client, L"base.player_base");
    clients[client].last_player_base =
        HkGetCharacterIniUint(client, L"base.last_player_base");
}

void SaveDockState(uint client) {
    HkSetCharacterIni(client, L"base.player_base", std::to_wstring(clients[client].player_base));
    HkSetCharacterIni(client, L"base.last_player_base",
                     std::to_wstring(clients[client].last_player_base));
}

void DeleteDockState(uint client) {
    HkSetCharacterIni(client, L"base.player_base", L"0");
    HkSetCharacterIni(client, L"base.last_player_base", L"0");
}
