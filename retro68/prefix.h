/****************************************************************************
 *  Retro68 build prefix for MiniVNC.
 *
 *  CodeWarrior compiled MiniVNC with its "MacHeaders" precompiled prefix,
 *  which pulled in the whole Toolbox implicitly (OSErr, BitMap, wdsEntry,
 *  FindFolder, UnloadSeg, ...). GCC/Retro68 has no such umbrella, so we
 *  force-include this file (-include) to reproduce that environment.
 ****************************************************************************/
#pragma once

#include <MacTypes.h>
#include <Quickdraw.h>
#include <QuickdrawText.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Controls.h>
#include <Menus.h>
#include <Events.h>
#include <Files.h>
#include <Folders.h>
#include <MacMemory.h>
#include <Devices.h>
#include <Gestalt.h>
#include <Traps.h>
#include <Retrace.h>
#include <ToolUtils.h>
#include <Resources.h>
#include <SegLoad.h>
#include <Sound.h>
#include <Timer.h>
#include <LowMem.h>
#include <Errors.h>
#include <Scrap.h>
