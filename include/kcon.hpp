/*
	This file is part of Fennix Kernel.

	Fennix Kernel is free software: you can redistribute it and/or
	modify it under the terms of the GNU General Public License as
	published by the Free Software Foundation, either version 3 of
	the License, or (at your option) any later version.

	Fennix Kernel is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Fennix Kernel. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef __FENNIX_KERNEL_KERNEL_CONSOLE_H__
#define __FENNIX_KERNEL_KERNEL_CONSOLE_H__

#include <display.hpp>
#include <termios.h>
#include <atomic>

namespace KernelConsole
{
	enum TerminalColor
	{
		BLACK,
		RED,
		GREEN,
		YELLOW,
		BLUE,
		MAGENTA,
		CYAN,
		GREY
	};

	struct ANSIArgument
	{
		int Value = 0;
		bool Empty = true;
	};

	struct ANSIParser
	{
		enum ParserState
		{
			Escape,
			Bracket,
			Attribute,
			EndValue
		} State = Escape;

		ANSIArgument Stack[8] = {};
		int Index = 0;
	};

	struct TerminalAttribute
	{
		bool Bright = false;
		TerminalColor Background = BLACK;
		TerminalColor Foreground = GREY;
	};

	struct TerminalCell
	{
		char c = '\0';
		TerminalAttribute attr{};
	};

	struct TerminalCursor
	{
		long X = 0;
		long Y = 0;
	};

	typedef void (*PaintCallback)(TerminalCell *Cell, long X, long Y);
	typedef void (*CursorCallback)(TerminalCursor *Cursor);

	class FontRenderer
	{
	public:
		Video::Font *CurrentFont = nullptr;
		TerminalCursor Cursor = {0, 0};
		char Paint(long CellX, long CellY, char Char, uint32_t Foreground, uint32_t Background);
	};

	class VirtualTerminal
	{
	private:
		ANSIParser Parser{};
		TerminalAttribute Attribute{};

		TerminalCell *Cells = nullptr;
		TerminalCursor Cursor{};

		PaintCallback PaintCB = nullptr;
		CursorCallback CursorCB = nullptr;

	public:
		termios term;
		winsize termSize;

		void Clear(unsigned short StartX, unsigned short StartY, unsigned short EndX, unsigned short EndY);
		void Scroll(unsigned short Lines);
		void NewLine();
		void Append(char c);
		void csi_cup(ANSIArgument *Args, int ArgsCount);
		void csi_ed(ANSIArgument *Args, int ArgsCount);
		void csi_el(ANSIArgument *Args, int ArgsCount);
		void csi_sgr(ANSIArgument *Args, int ArgsCount);
		void csi_cuu(ANSIArgument *Args, int ArgsCount);
		void csi_cud(ANSIArgument *Args, int ArgsCount);
		void csi_cuf(ANSIArgument *Args, int ArgsCount);
		void csi_cub(ANSIArgument *Args, int ArgsCount);
		void csi_cnl(ANSIArgument *Args, int ArgsCount);
		void csi_cpl(ANSIArgument *Args, int ArgsCount);
		void csi_cha(ANSIArgument *Args, int ArgsCount);
		void Process(char c);

		VirtualTerminal(unsigned short Rows, unsigned short Columns,
						unsigned short XPixels, unsigned short YPixels,
						PaintCallback Paint, CursorCallback Print);
		~VirtualTerminal();
	};

	/**
	 * 0 - Default
	 * 1...11 - User
	 * ...
	 * 15 - Panic
	 */
	extern VirtualTerminal *Terminals[16];
	extern std::atomic<VirtualTerminal *> CurrentTerminal;
	extern int TermColors[];
	extern int TermBrightColors[];

	bool SetTheme(std::string Theme);

	/* Limited in functionality */
	void EarlyInit();

	/* Full working terminal */
	void LateInit();
}

#endif // !__FENNIX_KERNEL_KERNEL_CONSOLE_H__
