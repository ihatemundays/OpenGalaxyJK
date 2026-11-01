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

#pragma once

// RmlUi menus, hosted by the client exe in both SP and MP so the MP ui module
// needs no API change. This header stays free of engine and RmlUi types:
// each client fills in rmlHost_t from shared/rml/cl_rml.inl.

typedef struct rmlHost_s {
	const char *menuDir;	// "ui/rml/sp" or "ui/rml/mp"

	void (*Print)( const char *text );

	long (*ReadFile)( const char *path, void **buffer );
	void (*FreeFile)( void *buffer );

	int (*Milliseconds)( void );
	void (*VidSize)( int *width, int *height );

	int (*RegisterShader)( const char *name );
	void (*SetColor)( const float *rgba );
	void (*DrawStretchPic)( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader );

	const char *(*GetString)( const char *reference );
	const char *(*CvarString)( const char *name );
	void (*Run)( const char *command );	// executes immediately
} rmlHost_t;

bool RML_Open( const rmlHost_t *host, const char *name );
void RML_Close( const char *name );
void RML_CloseAll( void );
void RML_Reload( const rmlHost_t *host );
void RML_Shutdown( void );

bool RML_IsActive( void );
void RML_Frame( void );
void RML_MouseMove( int dx, int dy );
void RML_MouseButton( int button, bool down );
void RML_MouseWheel( int direction );
void RML_Sync( const rmlHost_t *host, const char **names, int count );
void RML_CloseSynced( void );
bool RML_HasSynced( void );
bool RML_ClosePreview( void );
bool RML_Click( const char *name, float fractionX, float fractionY );
bool RML_Drag( const char *name, float fromX, float toX );
void RML_Char( int character );
