/* Copyright (c) 2019-2020 Stuart Steven Calder
 * All rights reserved.
 * See accompanying LICENSE file for licensing information.
 */
#pragma once
#include <shim/macros.h>
#include <shim/operations.h>

#if    defined (SHIM_OS_UNIXLIKE) || defined (SHIM_OS_WINDOWS)
#	ifdef SSC_FEATURE_TERMINAL_UI_F
#		error "SSC_FEATURE_TERMINAL_UI_F already defined"
#	endif
#	define SSC_FEATURE_TERMINAL_UI_F
#	if    defined (SHIM_OS_UNIXLIKE)
#		if    __has_include(<ncurses.h>)
#			include <ncurses.h>
#		elif  __has_include(<ncurses/ncurses.h>)
#			include <ncurses/ncurses.h>
#		else
#			error "No valid ncurses.h header found."
#		endif
#		define NEWLINE "\n"
#	elif  defined (SHIM_OS_WINDOWS)
#		include <shim/errors.h>
#		include <windows.h>
#		include <conio.h>
#		define NEWLINE "\n\r"
#	else
#		error "Unsupported operating system"
#	endif
#	define TEMPLATE_ARGS template <typename Char_t,\
                                       int      Max_Buffer_Size>
#	define CLASS         Terminal_UI_F<Char_t, Max_Buffer_Size>

#	include <utility>
namespace ssc
{
	TEMPLATE_ARGS struct
	Terminal_UI_F
	{
		Terminal_UI_F () = delete;

		static void
		init ();

		static void
		end ();

		[[nodiscard]] static int
		get_sensitive_string (Char_t *     SHIM_RESTRICT buffer,
				      char const * SHIM_RESTRICT prompt);

		static void
		notify (char const *notice);

		[[nodiscard]] static int
		obtain_password (Char_t *     SHIM_RESTRICT password_buffer,
				 char const * SHIM_RESTRICT entry_prompt,
				 int const                  min_pw_size = 1,
				 int const                  max_pw_size = Max_Buffer_Size - 1);

		[[nodiscard]] static int
		obtain_password (Char_t *     SHIM_RESTRICT password_buffer,
				 Char_t *     SHIM_RESTRICT check_buffer,
				 char const * SHIM_RESTRICT entry_prompt,
				 char const * SHIM_RESTRICT reentry_prompt,
				 int const                  min_pw_size = 1,
				 int const                  max_pw_size = Max_Buffer_Size - 1);
#	ifdef SHIM_OS_UNIXLIKE
		using Window_t = WINDOW;
		using Coord_Pair_t = std::pair<int,int>;
		static Coord_Pair_t get_cursor_position (Window_t *window = stdscr);
#	endif
	};

	TEMPLATE_ARGS void
	CLASS::init ()
	{
#	if    defined (SHIM_OS_UNIXLIKE)
		initscr();
		clear();
#	elif  defined (SHIM_OS_WINDOWS)
		system( "cls" );
#	endif
	}

	TEMPLATE_ARGS void
	CLASS::end ()
	{
#	if    defined (SHIM_OS_UNIXLIKE)
		endwin();
#	elif  defined (SHIM_OS_WINDOWS)
		system( "cls" );
#	endif
	}

	TEMPLATE_ARGS int
	CLASS::get_sensitive_string (Char_t *     SHIM_RESTRICT buffer,
				     char const * SHIM_RESTRICT prompt)
	{
		using namespace std;
		static_assert (Max_Buffer_Size >= 2);
		static constexpr int Max_Password_Size = Max_Buffer_Size - 1;
#	if    defined (SHIM_OS_UNIXLIKE)
		cbreak(); // Disable line buffering.
		noecho(); // Disables echoing input.
		keypad( stdscr, TRUE ); // Enable keypad of the user's terminal.
		int index = 0; // Track the index of the current character.
		Window_t *w = newwin( 5, Max_Password_Size + 10, 0, 0 ); // Create a new window with 5 lines and (Max_Password_Size + 1) columns, start at the top left (0,0).
		keypad( w, TRUE ); // Enable special characters in the new window `w`.
		bool outer, inner; // Two bools to track the following input loops.
		outer = true;
		while (outer) {
			memset( buffer, 0, Max_Buffer_Size ); // Zero out the buffer to start.
			wclear( w ); // Screen `w` is cleared completely.
			wmove( w, 1, 0 ); // Move the cursor of `w` to line 1, column 0.
			waddstr( w, prompt ); // Add the C string `prompt` to `w`.
			inner = true;
			while (inner) {
				int ch = wgetch( w ); // Read a character from the window `w`.
				switch (ch) {
					// If a delete character was input...
					case (127):
					case (KEY_DC):
					case (KEY_LEFT):
					case (KEY_BACKSPACE):
						// ...and we're not already pointing at the first character...
						if (index > 0) {
							int y, x;
							getyx( w, y, x ); // Get the cursor position into y, x.
							wdelch( w ); // Delete the character at the current cursor position.
							wmove( w, y, x - 1 ); // Move the cursor back one column by 1.
							wrefresh( w ); // Update the visuals of the terminal.
							buffer[ --index ] = static_cast<Char_t>('\0'); // Move the index back 1, and null the character at that position.
							// `index` always points to an "unoccupied" space.
						}
						break;
					// Else if return was input...
					case ('\n'):
					case (KEY_ENTER):
						inner = false; // The user is done inputting. Kill the inner loop.
						break;
					// For all other inputs...
					default:
						// ...given that the index points to a position within allowed limits...
						if (index < Max_Password_Size) {
							waddch( w, '*' ); // Add an asterisk at the current cursor position and advance the cursor.
							wrefresh( w ); // Update the visuals of the terminal.
								buffer[ index++ ] = static_cast<Char_t>(ch); // Set the current indexed position to the input character and advance the index.
						}
				} /* switch (ch) */
			} /* while (inner) */
			outer = false; // Kill the outer loop.
		} /* while (outer) */
		// The buffer should now contain a null-terminated C-string.
		int const password_size = strlen( reinterpret_cast<char*>(buffer) ); // Get the size of the null-terminated C-string in the buffer.
		delwin( w ); // Delete the window `w`.
		return password_size; // Return the number of non-null characters of the C-string in the buffer.
#	elif  defined (SHIM_OS_WINDOWS)
		int index = 0;
		bool repeat_ui, repeat_input;
		repeat_ui = true;
		while (repeat_ui) {
			memset( buffer, 0, Max_Buffer_Size );
			system( "cls" );
			if (_cputs( prompt ) != 0)
				errx( "Error: Failed to _cputs()\n" );
			repeat_input = true;
			while (repeat_input) {
				int ch = _getch();
				switch (ch) {
					// A password character wkey was pushed.
					default:
						if ((index < Max_Buffer_Size) && (ch >= 32) && (ch <= 126)) {
							if (_putch( '*' ) == EOF)
								errx( "Error: Failed to _putch()\n" );
							buffer[ index++ ] = static_cast<Char_t>(ch);
						}
						break;
					// Backspace was pushed.
					case ('\b'):
						if (index > 0) {
							if (_cputs( "\b \b" ) != 0)
								errx( "Error: Failed to _cputs()\n" );
							buffer[ --index ] = static_cast<Char_t>('\0');
						}
						break;
					// Enter was pushed.
					case ('\r'):
						repeat_input = false;
						break;
				} /* switch (ch) */
			} /* while (repeat_input) */
			repeat_ui = false;
		} /* while (repeat_ui) */
		int const password_size = strlen( reinterpret_cast<char*>(buffer) );
		system( "cls" );
		return password_size;
#	else
#		error 'Unuspported OS'
#	endif
	}/* ~ get_sensitive_string(...) */
	TEMPLATE_ARGS
	void CLASS::notify (char const *notice)
	{
		using namespace std;
#	if    defined (SHIM_OS_UNIXLIKE)
		Window_t *w = newwin( 1, strlen( notice ) + 1, 0, 0 );
		wclear( w );
		wmove( w, 0, 0 );
		waddstr( w, notice );
		wrefresh( w );
		wgetch( w );
		delwin( w );
#	elif  defined (SHIM_OS_WINDOWS)
		system( "cls" );
		if( _cputs( notice ) != 0 )
			errx( "Error: Failed to _cputs()\n" );
		system( "pause" );
		system( "cls" );
#	else
#		error 'Unsupported OS'
#	endif
	}
	TEMPLATE_ARGS int
	CLASS::obtain_password (Char_t *     SHIM_RESTRICT password_buffer,
				char const * SHIM_RESTRICT entry_prompt,
				int const                  min_pw_size,
				int const                  max_pw_size)
	{
		int size;
		while( true ) {
			size = get_sensitive_string( password_buffer, entry_prompt );
			if( size < min_pw_size )
				notify( "Password is not long enough." NEWLINE );
			else if( size > max_pw_size )
				notify( "Password is too long." NEWLINE );
			else
				break;
		}
		return size;
	}
	TEMPLATE_ARGS int
	CLASS::obtain_password (Char_t *     SHIM_RESTRICT password_buffer,
				Char_t *     SHIM_RESTRICT check_buffer,
				char const * SHIM_RESTRICT entry_prompt,
				char const * SHIM_RESTRICT reentry_prompt,
				int const                  min_pw_size,
				int const                  max_pw_size)
	{
		int size;
		while( true ) {
			size = get_sensitive_string( password_buffer, entry_prompt );
			if( size < min_pw_size ) {
				notify( "Password is not long enough." NEWLINE );
				continue;
			} else if( size > max_pw_size ) {
				notify( "Password is too long." NEWLINE );
				continue;
			} else if( get_sensitive_string( check_buffer, reentry_prompt ) != size ) {
				notify( "Second password not the same size as the first." NEWLINE );
				continue;
			}
			if( shim_ctime_memcmp( password_buffer, check_buffer, Max_Buffer_Size ) == 0 )
				break;
			notify( "Passwords do not match." NEWLINE );
		}
		return size;
	}
#	ifdef SHIM_OS_UNIXLIKE
	TEMPLATE_ARGS typename CLASS::Coord_Pair_t
	CLASS::get_cursor_position (Window_t *window)
	{
		Coord_Pair_t coordinates; // Declare a coordinates object.
		getmaxyx (window,coordinates.second,coordinates.first); // Use the getmaxyx macro to get the coordinates.
		return coordinates; // Return the coordinates.
	}
#	endif // ~ #ifdef SSC_OS_UNIXLIKE
}// ~ namespace ssc
#	undef NEWLINE
#	undef CLASS
#	undef TEMPLATE_ARGS
#endif// ~ #if defined (SSC_OS_UNIXLIKE) || defined (SSC_OS_WIN64)
