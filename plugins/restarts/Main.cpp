// Restarts plugin - Feb 2010 by Cannon
//
// Ported by Raikkonen & Nen 2022
//
// This is free software; you can redistribute it and/or modify it as
// you wish without restriction. If you do then I would appreciate
// being notified and/or mentioned somewhere.

#include "Main.h"

namespace Plugins::Restart
{
	const std::unique_ptr<Global> global = std::make_unique<Global>();

	void LoadSettings()
	{
		auto config = Serializer::JsonToObject<Config>();

		global->config = std::make_unique<Config>(config);
	}

	/* User Commands */

	void UserCmd_ShowRestarts(ClientId& client, const std::wstring_view& wscParam)
	{
		if (global->config->availableRestarts.empty())
		{
			PrintUserCmdText(client, L"There are no restarts available.");
			return;
		}

		PrintUserCmdText(client, L"You can use these restarts:");
		for (const auto& [key, value] : global->config->availableRestarts)
		{
			if (global->config->enableRestartCost)
			{
				PrintUserCmdText(client, L"%s - $%i", key.c_str(), value);
			}
			else
			{
				PrintUserCmdText(client, L"%s", key.c_str());
			}
		}
	}

	void UserCmd_Restart(ClientId& client, const std::wstring_view& wscParam)
	{
		std::wstring restartTemplate = GetParam(wscParam, ' ', 0);
		if (!restartTemplate.length())
		{
			PrintUserCmdText(client, L"ERR Invalid parameters");
			PrintUserCmdText(client, L"/restart <template>");
		}

		// Get the character name for this connection.
		Restart restart;
		restart.characterName = reinterpret_cast<const wchar_t*>(Players.GetActiveCharacterName(client));

		// Searching restart

		for (const auto& entity : std::filesystem::directory_iterator("flhook_plugins/restart"))
		{
			if (entity.is_directory() || entity.path().extension().string() != ".fl")
			{
				continue;
			}
			if (entity.path().filename().string() == wstos(restartTemplate + L".fl"))
			{
				restart.restartFile = entity.path().string();
			}
		}
		if (restart.restartFile.empty())
		{
			PrintUserCmdText(client, L"ERR Template does not exist");
			return;
		}

		// Saving the characters forces an anti-cheat checks and fixes
		// up a multitude of other problems.
		SaveChar(client);
		if (!IsValidClientID(client))
			return;

		uint iBaseId;
		pub::Player::GetBase(client, iBaseId);
		if (!iBaseId)
		{
			PrintUserCmdText(client, L"ERR Not in base");
			return;
		}

		if (global->config->maxRank != 0)
		{
			int rank = 0;
			GetRank(restart.characterName, rank);
			if (rank == 0 || rank > global->config->maxRank)
			{
				PrintUserCmdText(client,
				    L"ERR You must create a new char to "
				    L"restart. Your rank is too high");
				return;
			}
		}

		Error err;
		int cash = 0;
		if ((err = GetCash(restart.characterName, cash)) != E_OK)
		{
			PrintUserCmdText(client, L"ERR " + ErrGetText(err));
			return;
		}

		if (global->config->maxCash != 0 && cash > global->config->maxCash)
		{
			PrintUserCmdText(client,
			    L"ERR You must create a new char to "
			    L"restart. Your cash is too high");
			return;
		}

		if (global->config->enableRestartCost)
		{
			if (cash < global->config->availableRestarts[restartTemplate])
			{
				PrintUserCmdText(
				    client, L"You need $" + std::to_wstring(global->config->availableRestarts[restartTemplate] - cash) + L" more credits to use this template");
				return;
			}
			restart.cash = cash - global->config->availableRestarts[restartTemplate];
		}
		else
			restart.cash = cash;

		if (CAccount* acc = Players.FindAccountFromClientID(client))
		{
			GetAccountDirName(acc, restart.directory);
			GetCharFileName(restart.characterName, restart.characterFile);
			global->pendingRestarts.push_back(restart);
			KickReason(restart.characterName, L"Updating character, please wait 10 seconds before reconnecting");
		}
		return;
	}

	/* Hooks */

	void OneSecondTimer()
	{
		while (global->pendingRestarts.size())
		{
			Restart restart = global->pendingRestarts.back();
			if (GetClientIdFromCharname(restart.characterName) != -1)
				return;

			global->pendingRestarts.pop_back();

			try
			{
				// Overwrite the existing character file
				std::string scCharFile = scAcctPath + wstos(restart.directory) + "\\" + wstos(restart.characterFile) + ".fl";
				std::string scTimeStampDesc = IniGetS(scCharFile, "Player", "description", "");
				std::string scTimeStamp = IniGetS(scCharFile, "Player", "tstamp", "0");
				if (!::CopyFileA(restart.restartFile.c_str(), scCharFile.c_str(), FALSE))
					throw("copy template");

				flc_decode(scCharFile.c_str(), scCharFile.c_str());
				IniWriteW(scCharFile, "Player", "name", restart.characterName);
				IniWrite(scCharFile, "Player", "description", scTimeStampDesc);
				IniWrite(scCharFile, "Player", "tstamp", scTimeStamp);
				IniWrite(scCharFile, "Player", "money", std::to_string(restart.cash));

				if (!FLHookConfig::i()->general.disableCharfileEncryption)
					flc_encode(scCharFile.c_str(), scCharFile.c_str());

				AddLog(LogType::Normal, LogLevel::Info, L"NOTICE: User restart %s for %s", restart.restartFile.c_str(), wstos(restart.characterName).c_str());
			}
			catch (char* err)
			{
				AddLog(LogType::Normal, LogLevel::Info, L"ERROR: User restart failed (%s) for %s", err, wstos(restart.characterName).c_str());
			}
			catch (...)
			{
				AddLog(LogType::Normal, LogLevel::Info, L"ERROR: User restart failed for %s", wstos(restart.characterName).c_str());
			}
		}
	}

	// Client command processing
	const std::vector commands = {{
	    CreateUserCommand(L"/restart", L"<name>", UserCmd_Restart, L"Restart with a template. This wipes your character!"),
	    CreateUserCommand(L"/showrestarts", L"", UserCmd_ShowRestarts, L"Shows the available restarts on the server."),
	}};
} // namespace Plugins::Restart
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

using namespace Plugins::Restart;

REFL_AUTO(type(Config), field(maxCash), field(maxRank), field(enableRestartCost), field(availableRestarts))

DefaultDllMainSettings(LoadSettings)

    extern "C" EXPORT void ExportPluginInfo(PluginInfo* pi)
{
	pi->name("Restarts");
	pi->shortName("restarts");
	pi->mayUnload(true);
	pi->commands(commands);
	pi->returnCode(&global->returnCode);
	pi->versionMajor(PluginMajorVersion::VERSION_04);
	pi->versionMinor(PluginMinorVersion::VERSION_00);
	pi->emplaceHook(HookedCall::FLHook__LoadSettings, &LoadSettings, HookStep::After);
	pi->emplaceHook(HookedCall::FLHook__TimerCheckKick, &OneSecondTimer);
}
