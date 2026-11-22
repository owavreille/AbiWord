/* ut_process.cpp
 *
 * Copyright (C) 2002 AbiWord developers (see CREDITS.TXT for list).
 * Initially written by Kenneth J. Davis, I disclaim any copyright.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
 * 02110-1301 USA.
 */

#include <string>

#include "ut_process.h"

// returns true if process is still alive
bool isProcessStillAlive(ProcessInfo &pI)
{
  DWORD status;

  return ((GetExitCodeProcess(pI.hProcess, &status))?(status == STILL_ACTIVE):false);
}


// our equivalent of fork() & exec
BOOL CreateChildProcess(char * appName, char *cmdline,
				PROCESS_INFORMATION *procInfo,
				STARTUPINFO *startInfo) 
{
	//initialize structures used to return info
	ZeroMemory( procInfo, sizeof(PROCESS_INFORMATION) ); 
	ZeroMemory( startInfo, sizeof(STARTUPINFO) ); 
	startInfo->cb = sizeof(STARTUPINFO); 

	// Create the child process. 
	return CreateProcess(
			appName,   // application module to execute
			cmdline,   // command line 
			nullptr,      // process security attributes
			nullptr,      // primary thread security attributes
			FALSE,     // handles not are inherited 
			0,         // creation flags 
			nullptr,      // use parent's environment
			nullptr,      // use parent's current directory
			startInfo, // STARTUPINFO pointer 
			procInfo   // receives PROCESS_INFORMATION 
	);
} 

static std::wstring utf8_to_wide(const char * text)
{
	if (!text || !*text)
		return std::wstring();
	const int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
	if (len <= 0)
		return std::wstring();
	std::wstring wide(static_cast<size_t>(len), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text, -1, &wide[0], len);
	if (!wide.empty() && wide.back() == L'\0')
		wide.pop_back();
	return wide;
}

// returns true if process successfully started, false otherwise
bool createChildProcess(const char *app, const char *args, ProcessInfo *pI)
{
  if (!app || !*app || !pI)
    return false;
  STARTUPINFOW startInfo;
  ZeroMemory(pI, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&startInfo, sizeof(STARTUPINFOW));
  startInfo.cb = sizeof(STARTUPINFOW);

  std::string appStr(app);
  std::string cmdline;
  if (appStr.front() != '"')
  {
    cmdline += '"';
    cmdline += appStr;
    cmdline += '"';
  }
  else
  {
    cmdline += appStr;
  }
  if (args && *args)
  {
    cmdline += ' ';
    cmdline += args;
  }

  std::wstring wApp = utf8_to_wide(app);
  std::wstring wCmd = utf8_to_wide(cmdline.c_str());
  if (wApp.empty() || wCmd.empty())
    return false;

  return CreateProcessW(
    wApp.c_str(),
    &wCmd[0],
    nullptr,
    nullptr,
    FALSE,
    0,
    nullptr,
    nullptr,
    &startInfo,
    pI) != FALSE;
}

// will [try to] kill the process if it is still running
void endProcess(ProcessInfo &pI)
{
  if (pI.hProcess)
    TerminateProcess(pI.hProcess, -1);

  pI.hProcess = 0;
}
