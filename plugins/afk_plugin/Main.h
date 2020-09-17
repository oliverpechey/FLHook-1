#pragma once

#include <windows.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <math.h>
#include <map>
#include <FLHook.h>
#include <plugin.h>
#include "PluginUtilities.h"

#include "../hookext_plugin/hookext_exports.h"

static int set_iPluginDebug = 0;
PLUGIN_RETURNCODE returncode;

typedef bool (*_UserCmdProc)(uint, const std::wstring&, const std::wstring&, const wchar_t*);

struct USERCMD
{
	wchar_t* wszCmd;
	_UserCmdProc proc;
	wchar_t* usage;
};

#define IS_CMD(a) !wscCmd.compare(L##a)

void AddExceptionInfoLog(struct SEHException* pep);
