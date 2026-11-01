/*
===========================================================================
Copyright (C) 2013 - 2026, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// Client side of the RmlUi menus. Included by code/client/cl_rml.cpp and
// codemp/client/cl_rml.cpp, which define RML_RE and RML_MENU_DIR.

#include "rml/rml_ui.h"

static cvar_t *ui_rmlUi;

static void Host_Print( const char *text ) {
	Com_Printf( "%s", text );
}

static int Host_Milliseconds( void ) {
	return Sys_Milliseconds();
}

static void Host_VidSize( int *width, int *height ) {
	*width = cls.glconfig.vidWidth;
	*height = cls.glconfig.vidHeight;
}

static int Host_RegisterShader( const char *name ) {
	return RML_RE RegisterShaderNoMip( name );
}

static void Host_SetColor( const float *rgba ) {
	RML_RE SetColor( rgba );
}

static void Host_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader ) {
	RML_RE DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
}

static const char *Host_GetString( const char *reference ) {
	return SE_GetString( reference );
}

static const char *Host_CvarString( const char *name ) {
	return Cvar_VariableString( name );
}

// This can run in the middle of another command, such as while one loads a map,
// so that command's arguments are put back afterwards.
static void Host_Run( const char *command ) {
	char saved[MAX_STRING_CHARS] = "";
	for ( int i = 0; i < Cmd_Argc(); i++ ) {
		Q_strcat( saved, sizeof( saved ), va( "\"%s\" ", Cmd_Argv( i ) ) );
	}

	Cmd_ExecuteString( command );
	Cmd_TokenizeString( saved );
}

static const rmlHost_t rmlHost = {
	RML_MENU_DIR,
	Host_Print,
	FS_ReadFile,
	FS_FreeFile,
	Host_Milliseconds,
	Host_VidSize,
	Host_RegisterShader,
	Host_SetColor,
	Host_DrawStretchPic,
	Host_GetString,
	Host_CvarString,
	Host_Run,
};

static bool CL_RmlAvailable( void ) {
	if ( !ui_rmlUi->integer ) {
		Com_Printf( "RmlUi menus are off, use \"set ui_rmlUi 1\" first\n" );
		return false;
	}
	if ( !cls.rendererStarted ) {
		Com_Printf( "The renderer isn't running yet\n" );
		return false;
	}
	return true;
}

static void CL_RmlOpen_f( void ) {
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: rml_open <menu>, rml_list shows the menus\n" );
		return;
	}
	if ( CL_RmlAvailable() ) {
		RML_Open( &rmlHost, Cmd_Argv( 1 ) );
	}
}

static void CL_RmlClose_f( void ) {
	if ( Cmd_Argc() == 2 ) {
		RML_Close( Cmd_Argv( 1 ) );
	} else {
		RML_CloseAll();
	}
}

static void CL_RmlReload_f( void ) {
	if ( CL_RmlAvailable() ) {
		RML_Reload( &rmlHost );
	}
}

// The ui code's list of open menus.
static void CL_RmlSync_f( void ) {
	const char *names[64];
	int count = 0;

	if ( !ui_rmlUi->integer || !cls.rendererStarted ) {
		return;
	}
	for ( int i = 1; i < Cmd_Argc() && count < 64; i++ ) {
		names[count++] = Cmd_Argv( i );
	}
	RML_Sync( &rmlHost, names, count );
}

// Clicks a menu item by name, for testing menus from a script. The optional
// position is a fraction of the item's size, and the middle is the default.
static void CL_RmlClick_f( void ) {
	const float fractionX = Cmd_Argc() > 2 ? atof( Cmd_Argv( 2 ) ) : 0.5f;
	const float fractionY = Cmd_Argc() > 3 ? atof( Cmd_Argv( 3 ) ) : 0.5f;

	if ( Cmd_Argc() < 2 ) {
		Com_Printf( "usage: rml_click <item> [x fraction] [y fraction]\n" );
	} else if ( !RML_IsActive() || !RML_Click( Cmd_Argv( 1 ), fractionX, fractionY ) ) {
		Com_Printf( "No clickable item named %s\n", Cmd_Argv( 1 ) );
	}
}

// Drags a slider between two fractions of its width, for testing from a script.
static void CL_RmlDrag_f( void ) {
	if ( Cmd_Argc() != 4 ) {
		Com_Printf( "usage: rml_drag <item> <from> <to>\n" );
	} else if ( !RML_IsActive() || !RML_Drag( Cmd_Argv( 1 ), atof( Cmd_Argv( 2 ) ), atof( Cmd_Argv( 3 ) ) ) ) {
		Com_Printf( "No clickable item named %s\n", Cmd_Argv( 1 ) );
	}
}

static void CL_RmlList_f( void ) {
	int count;
	char **files = FS_ListFiles( RML_MENU_DIR, ".rml", &count );

	for ( int i = 0; i < count; i++ ) {
		Com_Printf( "%s\n", files[i] );
	}
	Com_Printf( "%i menus in %s\n", count, RML_MENU_DIR );
	FS_FreeFileList( files );
}

void CL_RmlInit( void ) {
	ui_rmlUi = Cvar_Get( "ui_rmlUi", "1", CVAR_ARCHIVE );

	Cmd_AddCommand( "rml_open", CL_RmlOpen_f );
	Cmd_AddCommand( "rml_close", CL_RmlClose_f );
	Cmd_AddCommand( "rml_reload", CL_RmlReload_f );
	Cmd_AddCommand( "rml_list", CL_RmlList_f );
	Cmd_AddCommand( "rml_sync", CL_RmlSync_f );
	Cmd_AddCommand( "rml_click", CL_RmlClick_f );
	Cmd_AddCommand( "rml_drag", CL_RmlDrag_f );
}

void CL_RmlShutdown( void ) {
	RML_Shutdown();
}

void CL_RmlFrame( void ) {
	// the ui code stops reporting once no menu has the keyboard
	if ( !ui_rmlUi->integer || !( Key_GetCatcher() & KEYCATCH_UI ) ) {
		RML_CloseSynced();
	}
	RML_Frame();
}

// Returns true when an open menu used the key, which keeps it from the game.
qboolean CL_RmlKeyEvent( int key, qboolean down ) {
	if ( !RML_IsActive() || ( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return qfalse;
	}

	switch ( key ) {
	case A_MOUSE1: RML_MouseButton( 0, down != qfalse ); return qtrue;
	case A_MOUSE2: RML_MouseButton( 1, down != qfalse ); return qtrue;
	case A_MWHEELUP: if ( down ) RML_MouseWheel( -1 ); return qtrue;
	case A_MWHEELDOWN: if ( down ) RML_MouseWheel( 1 ); return qtrue;
	default: break;
	}

	if ( key == A_ESCAPE && down && RML_ClosePreview() ) {
		return qtrue;
	}

	// the keyboard stays with the ui code, which runs onESC and fills in bindings and text fields
	return RML_HasSynced() ? qfalse : qtrue;
}

qboolean CL_RmlCharEvent( int key ) {
	if ( !RML_IsActive() || ( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return qfalse;
	}
	if ( RML_HasSynced() ) {
		return qfalse;
	}
	RML_Char( key );
	return qtrue;
}

qboolean CL_RmlMouseEvent( int dx, int dy ) {
	if ( !RML_IsActive() ) {
		return qfalse;
	}
	RML_MouseMove( dx, dy );
	return qtrue;
}
