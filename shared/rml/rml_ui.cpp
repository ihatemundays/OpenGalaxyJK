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

#include "rml/rml_ui.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// Everything is drawn with DrawStretchPic, the one call every renderer shares,
// and shaders stay in charge of art. That limits geometry to axis-aligned quads
// and textures to anything the shader system can load.

static const rmlHost_t *host;
static Rml::Context *context;
static std::map<std::string, Rml::ElementDocument *> documents;
static std::set<std::string> synced;	// opened by the game, the rest are previews
static std::set<std::string> failed;
static int mouseX, mouseY;
static int cursorShader;
// Events from closing or updating a menu can fire in the middle of another console
// command, and running a command there would overwrite its arguments.
static bool inputEvent;
static Rml::Element *dragItem;	// the slider held by the mouse

// A document ends with an image named rmlpaint/<menu>. It isn't art: reaching it while
// drawing tells the ui code to paint that menu's items, in the menu's place in the stack.
static const char *const PAINT_PREFIX = "rmlpaint/";
static const int PAINT_HANDLE_BASE = 1 << 24;	// above any shader handle

static const int VIRTUAL_WIDTH = 640;
static const int VIRTUAL_HEIGHT = 480;

// Menus are laid out in 4:3 and stretched to the window, as the legacy ui does,
// which keeps them lined up with the items it still paints.
static int ContextWidth( int height ) {
	return height * VIRTUAL_WIDTH / VIRTUAL_HEIGHT;
}

/*
================
Render interface
================
*/

class RmlRenderInterface : public Rml::RenderInterface {
	struct Quad {
		float x1, y1, x2, y2, s1, t1, s2, t2;
		float rgba[4];
	};
	struct Geometry {
		std::vector<Quad> quads;
	};

public:
	Rml::CompiledGeometryHandle CompileGeometry( Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices ) override {
		Geometry *geometry = new Geometry;

		for ( size_t i = 0; i + 6 <= indices.size(); i += 6 ) {
			Quad quad;
			const Rml::Vertex *first = &vertices[indices[i]];
			const Rml::Vertex *last = first;
			quad.x1 = quad.x2 = first->position.x;
			quad.y1 = quad.y2 = first->position.y;

			for ( size_t j = 1; j < 6; j++ ) {
				const Rml::Vertex *v = &vertices[indices[i + j]];
				if ( v->position.x + v->position.y < first->position.x + first->position.y ) first = v;
				if ( v->position.x + v->position.y > last->position.x + last->position.y ) last = v;
				quad.x1 = std::min( quad.x1, v->position.x );
				quad.y1 = std::min( quad.y1, v->position.y );
				quad.x2 = std::max( quad.x2, v->position.x );
				quad.y2 = std::max( quad.y2, v->position.y );
			}

			quad.s1 = first->tex_coord.x;
			quad.t1 = first->tex_coord.y;
			quad.s2 = last->tex_coord.x;
			quad.t2 = last->tex_coord.y;

			// RmlUi premultiplies, the shaders expect plain color
			const Rml::ColourbPremultiplied color = first->colour;
			const float alpha = color.alpha / 255.0f;
			quad.rgba[3] = alpha;
			for ( int c = 0; c < 3; c++ ) {
				const float value = ( c == 0 ? color.red : c == 1 ? color.green : color.blue ) / 255.0f;
				quad.rgba[c] = alpha > 0.0f ? std::min( value / alpha, 1.0f ) : 0.0f;
			}
			geometry->quads.push_back( quad );
		}

		return reinterpret_cast<Rml::CompiledGeometryHandle>( geometry );
	}

	void RenderGeometry( Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture ) override {
		const Geometry *geometry = reinterpret_cast<const Geometry *>( handle );

		auto paint = paintMenus.find( (int)texture );
		if ( paint != paintMenus.end() ) {
			host->SetColor( NULL );
			host->Run( ( "rml_paint " + paint->second + "\n" ).c_str() );
			return;
		}

		int width, height;
		host->VidSize( &width, &height );
		const float scaleX = (float)VIRTUAL_WIDTH / ContextWidth( height );
		const float scaleY = (float)VIRTUAL_HEIGHT / height;

		for ( const Quad &quad : geometry->quads ) {
			float x1 = quad.x1 + translation.x, y1 = quad.y1 + translation.y;
			float x2 = quad.x2 + translation.x, y2 = quad.y2 + translation.y;
			float s1 = quad.s1, t1 = quad.t1, s2 = quad.s2, t2 = quad.t2;

			if ( scissorEnabled ) {
				const float w = x2 - x1, h = y2 - y1;
				if ( w <= 0.0f || h <= 0.0f ) continue;
				const float left = std::max( x1, (float)scissor.Left() ), right = std::min( x2, (float)scissor.Right() );
				const float top = std::max( y1, (float)scissor.Top() ), bottom = std::min( y2, (float)scissor.Bottom() );
				if ( left >= right || top >= bottom ) continue;
				const float ds = s2 - s1, dt = t2 - t1;
				s2 = s1 + ds * ( right - x1 ) / w;
				s1 = s1 + ds * ( left - x1 ) / w;
				t2 = t1 + dt * ( bottom - y1 ) / h;
				t1 = t1 + dt * ( top - y1 ) / h;
				x1 = left; x2 = right; y1 = top; y2 = bottom;
			}

			host->SetColor( quad.rgba );
			host->DrawStretchPic( x1 * scaleX, y1 * scaleY, ( x2 - x1 ) * scaleX, ( y2 - y1 ) * scaleY,
				s1, t1, s2, t2, (int)texture ? (int)texture : whiteShader() );
		}
		host->SetColor( NULL );
	}

	void ReleaseGeometry( Rml::CompiledGeometryHandle handle ) override {
		delete reinterpret_cast<Geometry *>( handle );
	}

	Rml::TextureHandle LoadTexture( Rml::Vector2i &dimensions, const Rml::String &source ) override {
		// the shader system finds the art, including shaders with no image of the same
		// name; menus always give images a size, so the real dimensions aren't needed
		dimensions = Rml::Vector2i( 256, 256 );

		if ( source.compare( 0, strlen( PAINT_PREFIX ), PAINT_PREFIX ) == 0 ) {
			const int id = PAINT_HANDLE_BASE + nextPaintId++;
			paintMenus[id] = source.substr( strlen( PAINT_PREFIX ) );
			return (Rml::TextureHandle)id;
		}

		const int shader = host->RegisterShader( source.c_str() );
		if ( !shader ) {
			host->Print( va_print( "RmlUi: no image or shader named %s\n", source.c_str() ).c_str() );
		}
		return (Rml::TextureHandle)shader;
	}

	Rml::TextureHandle GenerateTexture( Rml::Span<const Rml::byte>, Rml::Vector2i ) override {
		return 0;	// no renderer call uploads raw pixels yet
	}

	void ReleaseTexture( Rml::TextureHandle texture ) override {
		paintMenus.erase( (int)texture );
	}

	void EnableScissorRegion( bool enable ) override {
		scissorEnabled = enable;
	}

	void SetScissorRegion( Rml::Rectanglei region ) override {
		scissor = region;
	}

private:
	static std::string va_print( const char *format, const char *value ) {
		char text[512];
		snprintf( text, sizeof( text ), format, value );
		return text;
	}

	int whiteShader() {
		if ( !white ) {
			white = host->RegisterShader( "white" );
		}
		return white;
	}

	std::map<int, std::string> paintMenus;
	int nextPaintId = 0;
	bool scissorEnabled = false;
	Rml::Rectanglei scissor;
	int white = 0;

public:
	void Reset() { white = 0; scissorEnabled = false; }
};

/*
==============
File interface
==============
*/

class RmlFileInterface : public Rml::FileInterface {
	struct File {
		void *data;
		size_t length;
		size_t position;
	};

public:
	Rml::FileHandle Open( const Rml::String &path ) override {
		void *data = NULL;
		const long length = host->ReadFile( path.c_str(), &data );
		if ( length < 0 || !data ) {
			return 0;
		}
		return reinterpret_cast<Rml::FileHandle>( new File{ data, (size_t)length, 0 } );
	}

	void Close( Rml::FileHandle handle ) override {
		File *file = reinterpret_cast<File *>( handle );
		host->FreeFile( file->data );
		delete file;
	}

	size_t Read( void *buffer, size_t size, Rml::FileHandle handle ) override {
		File *file = reinterpret_cast<File *>( handle );
		const size_t count = std::min( size, file->length - file->position );
		memcpy( buffer, (const char *)file->data + file->position, count );
		file->position += count;
		return count;
	}

	bool Seek( Rml::FileHandle handle, long offset, int origin ) override {
		File *file = reinterpret_cast<File *>( handle );
		const long base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? (long)file->position : (long)file->length;
		const long target = base + offset;
		if ( target < 0 || target > (long)file->length ) {
			return false;
		}
		file->position = target;
		return true;
	}

	size_t Tell( Rml::FileHandle handle ) override {
		return reinterpret_cast<File *>( handle )->position;
	}
};

/*
================
System interface
================
*/

static std::string LatinToUtf8( const char *text );

class RmlSystemInterface : public Rml::SystemInterface {
public:
	double GetElapsedTime() override {
		return host->Milliseconds() / 1000.0;
	}

	// text beginning with @ is a string reference, as in .menu files
	int TranslateString( Rml::String &translated, const Rml::String &input ) override {
		if ( input.size() > 1 && input[0] == '@' ) {
			translated = LatinToUtf8( host->GetString( input.c_str() + 1 ) );
			return 1;
		}
		translated = input;
		return 0;
	}

	// every path in a document is a game path, there are no relative ones
	void JoinPath( Rml::String &translated, const Rml::String &, const Rml::String &path ) override {
		translated = path;
	}

	bool LogMessage( Rml::Log::Type type, const Rml::String &message ) override {
		if ( type <= Rml::Log::LT_WARNING ) {
			host->Print( ( "RmlUi: " + message + "\n" ).c_str() );
		}
		return true;
	}

};

/*
===========
Font engine

Reads the game's .fontdat files, so glyph placement matches RE_Font_DrawString
and the font art stays the retail bitmap atlas.
===========
*/

#pragma pack(push, 1)
struct fontGlyph_t {
	short width, height, horizAdvance, horizOffset;
	int baseline;
	float s, t, s2, t2;
};
#pragma pack(pop)

static const int FONT_GLYPHS = 256;
static const size_t FONT_DAT_SIZE = 7180;

class RmlFontEngine : public Rml::FontEngineInterface {
	struct Face {
		std::string atlas;
		fontGlyph_t glyphs[FONT_GLYPHS];
		float scale;
		Rml::FontMetrics metrics;
	};

public:
	Rml::FontFaceHandle GetFontFaceHandle( const Rml::String &family, Rml::Style::FontStyle, Rml::Style::FontWeight, int size ) override {
		const std::string name = Rml::StringUtilities::ToLower( family );
		const std::string key = name + "/" + std::to_string( size );
		auto found = faces.find( key );
		if ( found != faces.end() ) {
			return reinterpret_cast<Rml::FontFaceHandle>( found->second );
		}

		void *data = NULL;
		const std::string path = "fonts/" + name + ".fontdat";
		const long length = host->ReadFile( path.c_str(), &data );
		if ( !data || (size_t)length < FONT_DAT_SIZE ) {
			if ( data ) host->FreeFile( data );
			return 0;
		}

		Face *face = new Face;
		face->atlas = "fonts/" + name;
		memcpy( face->glyphs, data, sizeof( face->glyphs ) );

		short sizes[4];	// point size, height, ascender, descender
		memcpy( sizes, (const char *)data + sizeof( face->glyphs ), sizeof( sizes ) );
		host->FreeFile( data );

		int pointSize = sizes[0], height = sizes[1], descender = sizes[3];
		if ( pointSize <= 0 ) pointSize = 16;
		if ( height == 0 ) {	// same fallback as CFontInfo
			height = pointSize;
			const int ascender = pointSize - (int)std::lround( pointSize / 10.0f + 2.0f );
			descender = height - ascender;
		}

		face->scale = (float)size / pointSize;
		face->metrics = Rml::FontMetrics();
		face->metrics.size = size;
		face->metrics.ascent = ( height - ( descender >> 1 ) ) * face->scale;
		face->metrics.descent = ( descender >> 1 ) * face->scale;
		face->metrics.line_spacing = height * face->scale;
		face->metrics.x_height = height * face->scale * 0.5f;
		face->metrics.underline_position = 1.0f;
		face->metrics.underline_thickness = 1.0f;

		faces[key] = face;
		return reinterpret_cast<Rml::FontFaceHandle>( face );
	}

	const Rml::FontMetrics &GetFontMetrics( Rml::FontFaceHandle handle ) override {
		return reinterpret_cast<Face *>( handle )->metrics;
	}

	int GetStringWidth( Rml::FontFaceHandle handle, Rml::StringView string, const Rml::TextShapingContext &, Rml::Character ) override {
		const Face *face = reinterpret_cast<Face *>( handle );
		float width = 0.0f;
		for ( auto it = Rml::StringIteratorU8( string.begin(), string.begin(), string.end() ); it; ++it ) {
			width += Glyph( face, it )->horizAdvance * face->scale;
		}
		return (int)width;
	}

	int GenerateString( Rml::RenderManager &render_manager, Rml::FontFaceHandle handle, Rml::FontEffectsHandle, Rml::StringView string,
		Rml::Vector2f position, Rml::ColourbPremultiplied colour, float, const Rml::TextShapingContext &, Rml::TexturedMeshList &mesh_list ) override {
		const Face *face = reinterpret_cast<Face *>( handle );

		mesh_list.resize( 1 );
		Rml::TexturedMesh &output = mesh_list[0];
		output.texture = render_manager.LoadTexture( face->atlas );

		float x = position.x;
		for ( auto it = Rml::StringIteratorU8( string.begin(), string.begin(), string.end() ); it; ++it ) {
			const fontGlyph_t *glyph = Glyph( face, it );
			if ( glyph->width > 0 && *it != (Rml::Character)' ' ) {
				const float x1 = x + glyph->horizOffset * face->scale;
				const float y1 = position.y - glyph->baseline * face->scale;
				const float x2 = x1 + glyph->width * face->scale;
				const float y2 = y1 + glyph->height * face->scale;

				const int base = (int)output.mesh.vertices.size();
				const float corners[4][4] = {
					{ x1, y1, glyph->s, glyph->t }, { x2, y1, glyph->s2, glyph->t },
					{ x2, y2, glyph->s2, glyph->t2 }, { x1, y2, glyph->s, glyph->t2 },
				};
				for ( const float *c : corners ) {
					Rml::Vertex vertex;
					vertex.position = Rml::Vector2f( c[0], c[1] );
					vertex.colour = colour;
					vertex.tex_coord = Rml::Vector2f( c[2], c[3] );
					output.mesh.vertices.push_back( vertex );
				}
				for ( int index : { 0, 1, 2, 0, 2, 3 } ) {
					output.mesh.indices.push_back( base + index );
				}
			}
			x += glyph->horizAdvance * face->scale;
		}
		return (int)( x - position.x );
	}

	void ReleaseFontResources() override {
		for ( auto &entry : faces ) {
			delete entry.second;
		}
		faces.clear();
	}

private:
	static const fontGlyph_t *Glyph( const Face *face, const Rml::StringIteratorU8 &it ) {
		const unsigned int code = (unsigned int)*it;
		const fontGlyph_t *glyph = &face->glyphs[code < FONT_GLYPHS ? code : '.'];
		return glyph->width || code == ' ' ? glyph : &face->glyphs[(int)'.'];
	}

	std::map<std::string, Face *> faces;
};

/*
================
Menu interaction

The legacy ui code keeps owning game logic while menus move over: a click runs
the item's original script there, through rml_run, so every cvar, sound,
uiScript and menu change stays exactly what the game expects. The legacy side
reports its open menus back through rml_sync. Scripts in a document are only
used here for what they change on screen.
================
*/

static std::string LatinToUtf8( const char *text ) {
	std::string out;
	for ( ; *text; text++ ) {
		const unsigned char c = (unsigned char)*text;
		if ( c < 0x80 ) {
			out += (char)c;
		} else {
			out += (char)( 0xC0 | ( c >> 6 ) );
			out += (char)( 0x80 | ( c & 0x3F ) );
		}
	}
	return out;
}

static bool SameText( const Rml::String &a, const Rml::String &b ) {
	return Rml::StringUtilities::ToLower( a ) == Rml::StringUtilities::ToLower( b );
}

static std::string DisplayText( const std::string &text ) {
	return LatinToUtf8( text.size() > 1 && text[0] == '@' ? host->GetString( text.c_str() + 1 ) : text.c_str() );
}

static void SetDp( Rml::Element *element, const char *property, float value ) {
	element->SetProperty( property, std::to_string( value ) + "dp" );
}

static Rml::Element *LastSpan( Rml::Element *element ) {
	for ( int i = element->GetNumChildren() - 1; i >= 0; i-- ) {
		if ( element->GetChild( i )->GetTagName() == "span" ) {
			return element->GetChild( i );
		}
	}
	return NULL;
}

static std::string CssColor( const Rml::StringList &args, size_t first ) {
	char text[32];
	const auto channel = [&]( size_t i ) {
		return (int)std::lround( std::max( 0.0, std::min( 1.0, atof( args[first + i].c_str() ) ) ) * 255.0 );
	};
	snprintf( text, sizeof( text ), "#%02x%02x%02x%02x", channel( 0 ), channel( 1 ), channel( 2 ), channel( 3 ) );
	return text;
}

// Applies one script command that has a visible effect to every matching item.
static void ApplyVisual( Rml::ElementDocument *document, const Rml::StringList &args ) {
	const Rml::String &verb = args[0];
	if ( args.size() < 2 ) {
		return;
	}

	const float originX = document->GetAttribute<float>( "data-x", 0.0f );
	const float originY = document->GetAttribute<float>( "data-y", 0.0f );

	Rml::ElementList items;
	document->GetElementsByClassName( items, "item" );
	for ( Rml::Element *item : items ) {
		if ( !SameText( item->GetAttribute<Rml::String>( "data-name", "" ), args[1] )
			&& !SameText( item->GetAttribute<Rml::String>( "data-group", "" ), args[1] ) ) {
			continue;
		}

		if ( verb == "show" || verb == "fadein" ) {
			item->SetClass( "hidden", false );
		} else if ( verb == "hide" || verb == "fadeout" ) {
			item->SetClass( "hidden", true );
		} else if ( verb == "disable" && args.size() > 2 ) {
			item->SetClass( "disabled", atoi( args[2].c_str() ) != 0 );
		} else if ( verb == "setitemtext" && args.size() > 2 ) {
			Rml::Element *span = LastSpan( item );
			if ( span ) {
				span->SetInnerRML( Rml::StringUtilities::EncodeRml( DisplayText( args[2] ) ) );
			}
		} else if ( verb == "setitembackground" && args.size() > 2 ) {
			item->SetProperty( "decorator", "image(\"" + args[2] + "\")" );
		} else if ( verb == "setitemcolor" && args.size() > 6 ) {
			const std::string color = CssColor( args, 3 );
			if ( args[2] == "backcolor" ) {
				item->SetProperty( "background-color", color );
			} else if ( args[2] == "bordercolor" ) {
				item->SetProperty( "border-color", color );
			} else {
				Rml::Element *span = LastSpan( item );
				if ( span ) {
					span->SetProperty( "color", color );
				}
				item->SetProperty( "image-color", color );
			}
		} else if ( verb == "setitemrect" || verb == "transition2" || verb == "transition" ) {
			// .menu rects in scripts are screen positions, and a transition's last rect is its goal
			const size_t first = verb == "transition" ? 6 : 2;
			if ( args.size() >= first + 4 ) {
				SetDp( item, "left", (float)atof( args[first].c_str() ) - originX );
				SetDp( item, "top", (float)atof( args[first + 1].c_str() ) - originY );
				SetDp( item, "width", (float)atof( args[first + 2].c_str() ) );
				SetDp( item, "height", (float)atof( args[first + 3].c_str() ) );
			}
		}
	}
}

static void ApplyScript( Rml::ElementDocument *document, const Rml::String &script ) {
	Rml::StringList commands;
	Rml::StringUtilities::ExpandString( commands, script, ';' );

	for ( const Rml::String &command : commands ) {
		Rml::StringList args;
		Rml::StringUtilities::ExpandString( args, command, ' ' );
		if ( !args.empty() ) {
			ApplyVisual( document, args );
		}
	}
}

static bool CvarMatches( Rml::Element *element ) {
	const Rml::String value = host->CvarString( element->GetAttribute<Rml::String>( "data-cvartest", "" ).c_str() );
	Rml::StringList values;
	Rml::StringUtilities::ExpandString( values, element->GetAttribute<Rml::String>( "data-cvarvalues", "" ), ';' );
	for ( const Rml::String &candidate : values ) {
		if ( SameText( candidate, value ) ) {
			return true;
		}
	}
	return false;
}

// Same rule as Item_EnableShowViaCvar: show and enable need a match, hide and disable forbid one.
static void UpdateCvarState( Rml::ElementDocument *document ) {
	Rml::ElementList elements;
	document->GetElementsByClassName( elements, "cvarstate" );
	for ( Rml::Element *element : elements ) {
		const Rml::String rule = element->GetAttribute<Rml::String>( "data-cvarrule", "" );
		const bool match = CvarMatches( element );
		if ( rule == "show" || rule == "hide" ) {
			element->SetClass( "cvarhidden", match != ( rule == "show" ) );
		} else {
			element->SetClass( "disabled", match != ( rule == "enable" ) );
		}
	}

	elements.clear();
	document->GetElementsByClassName( elements, "cvartext" );
	for ( Rml::Element *element : elements ) {
		const Rml::String cvar = element->GetAttribute<Rml::String>( "data-cvar", "" );
		const Rml::String value = host->CvarString( cvar.c_str() );
		Rml::String text = value;

		const Rml::String yesno = element->GetAttribute<Rml::String>( "data-yesno", "" );
		if ( !yesno.empty() ) {
			const bool on = ( atof( value.c_str() ) != 0.0 ) != ( yesno == "invert" );
			text = on ? "@MENUS_YES" : "@MENUS_NO";
		}

		// feeder lists are only known at runtime, this is how ItemParse_cvarStrList names them
		const Rml::String prefix = element->GetAttribute<Rml::String>( "data-multiprefix", "" );
		if ( !prefix.empty() ) {
			text = prefix + Rml::StringUtilities::ToUpper( value );
		}
		const Rml::String fixedLabel = element->GetAttribute<Rml::String>( "data-multilabel", "" );
		if ( !fixedLabel.empty() ) {
			text = fixedLabel;
		}

		Rml::StringList choices;
		Rml::StringUtilities::ExpandString( choices, element->GetAttribute<Rml::String>( "data-multi", "" ), ';' );
		for ( size_t i = 0; i + 1 < choices.size(); i += 2 ) {
			const bool numeric = !value.empty() && ( isdigit( (unsigned char)value[0] ) || value[0] == '-' );
			if ( choices[i + 1] == value || ( numeric && atof( choices[i + 1].c_str() ) == atof( value.c_str() ) ) ) {
				text = choices[i];
				break;
			}
		}

		const std::string encoded = Rml::StringUtilities::EncodeRml( DisplayText( text ) );
		if ( element->GetInnerRML() != encoded ) {
			element->SetInnerRML( encoded );
		}
	}
}

static void RunItemEvent( Rml::Element *item, const char *event ) {
	int width, height;
	host->VidSize( &width, &height );
	const std::string command = "rml_run " + item->GetOwnerDocument()->GetAttribute<Rml::String>( "data-menu", "" ) + " "
		+ item->GetAttribute<Rml::String>( "data-index", "-1" ) + " " + event + " "
		+ std::to_string( mouseX * VIRTUAL_WIDTH / ContextWidth( height ) ) + " "
		+ std::to_string( mouseY * VIRTUAL_HEIGHT / height ) + "\n";
	host->Run( command.c_str() );
}

class RmlMenuListener : public Rml::EventListener {
public:
	void ProcessEvent( Rml::Event &event ) override {
		if ( !inputEvent ) {
			return;
		}

		const bool click = event == Rml::EventId::Click;
		const char *eventName = click ? "click" : event == Rml::EventId::Mouseover ? "mouseenter" : "mouseexit";
		if ( event == Rml::EventId::Mousedown && event.GetParameter<int>( "button", 0 ) != 0 ) {
			return;
		}
		const bool over = event == Rml::EventId::Mouseover;
		const bool press = event == Rml::EventId::Mousedown;
		const char *attribute = click ? "data-action" : over ? "data-mouseenter" : "data-mouseexit";

		for ( Rml::Element *element = event.GetTargetElement(); element; element = element->GetParentNode() ) {
			if ( !element->IsClassSet( "item" ) ) {
				continue;
			}
			if ( element->IsClassSet( "disabled" ) ) {
				return;
			}

			// a slider works from the press, so its click has nothing left to do
			const bool slider = element->IsClassSet( "slider" );
			if ( press ) {
				if ( slider ) {
					dragItem = element;
					RunItemEvent( element, "press" );
				}
				return;
			}
			if ( click && slider ) {
				return;
			}

			const Rml::String description = element->GetAttribute<Rml::String>( "data-desc", "" );
			Rml::Element *line = element->GetOwnerDocument()->GetElementById( "rmldesc" );
			if ( !click && line && !description.empty() ) {
				line->SetInnerRML( over ? Rml::StringUtilities::EncodeRml( DisplayText( description ) ) : "" );
			}

			const Rml::String script = element->GetAttribute<Rml::String>( attribute, "" );
			if ( !click && script.empty() ) {
				return;
			}

			Rml::ElementDocument *document = element->GetOwnerDocument();
			RunItemEvent( element, eventName );
			ApplyScript( document, script );
			return;
		}
	}
};

static RmlMenuListener menuListener;

/*
=======
Runtime
=======
*/

// These must outlive RmlUi, and an abnormal exit can skip RML_Shutdown, so they are never destroyed.
static RmlRenderInterface &renderInterface = *new RmlRenderInterface;
static RmlFileInterface &fileInterface = *new RmlFileInterface;
static RmlSystemInterface &systemInterface = *new RmlSystemInterface;
static RmlFontEngine &fontEngine = *new RmlFontEngine;

static bool Start( const rmlHost_t *newHost ) {
	host = newHost;
	if ( context ) {
		return true;
	}

	Rml::SetRenderInterface( &renderInterface );
	Rml::SetFileInterface( &fileInterface );
	Rml::SetSystemInterface( &systemInterface );
	Rml::SetFontEngineInterface( &fontEngine );
	if ( !Rml::Initialise() ) {
		host->Print( "RmlUi: failed to initialise\n" );
		return false;
	}

	int width, height;
	host->VidSize( &width, &height );
	context = Rml::CreateContext( "menus", Rml::Vector2i( ContextWidth( height ), height ) );
	if ( !context ) {
		Rml::Shutdown();
		return false;
	}
	context->SetDensityIndependentPixelRatio( height / (float)VIRTUAL_HEIGHT );

	mouseX = ContextWidth( height ) / 2;
	mouseY = height / 2;
	cursorShader = host->RegisterShader( "cursor" );
	return true;
}

static std::string Lower( const char *text ) {
	return Rml::StringUtilities::ToLower( Rml::String( text ) );
}

bool RML_Open( const rmlHost_t *newHost, const char *name ) {
	if ( !Start( newHost ) ) {
		return false;
	}

	const std::string key = Lower( name );
	if ( documents.count( key ) ) {
		documents[key]->PullToFront();
		return true;
	}

	const std::string path = std::string( host->menuDir ) + "/" + key + ".rml";
	Rml::ElementDocument *document = context->LoadDocument( path );
	if ( !document ) {
		host->Print( ( "RmlUi: couldn't load " + path + "\n" ).c_str() );
		return false;
	}

	document->AddEventListener( Rml::EventId::Click, &menuListener );
	document->AddEventListener( Rml::EventId::Mousedown, &menuListener );
	document->AddEventListener( Rml::EventId::Mouseover, &menuListener );
	document->AddEventListener( Rml::EventId::Mouseout, &menuListener );
	ApplyScript( document, document->GetAttribute<Rml::String>( "data-onopen", "" ) );
	UpdateCvarState( document );
	document->Show();
	documents[key] = document;
	return true;
}

void RML_Close( const char *name ) {
	auto found = documents.find( Lower( name ) );
	if ( found != documents.end() ) {
		dragItem = NULL;
		found->second->Close();
		synced.erase( found->first );
		documents.erase( found );
	}
}

void RML_CloseAll( void ) {
	dragItem = NULL;
	if ( context ) {
		context->UnloadAllDocuments();
		context->Update();
	}
	documents.clear();
	synced.clear();
}

void RML_Sync( const rmlHost_t *newHost, const char **names, int count ) {
	// closing a menu can run commands, which reuse the buffer names points into
	std::set<std::string> wanted;
	std::vector<std::string> ordered;
	for ( int i = 0; i < count; i++ ) {
		ordered.push_back( Lower( names[i] ) );
		wanted.insert( ordered.back() );
	}

	const std::set<std::string> current = synced;
	for ( const std::string &name : current ) {
		if ( !wanted.count( name ) ) {
			RML_Close( name.c_str() );
		}
	}

	for ( const std::string &key : ordered ) {
		if ( synced.count( key ) || failed.count( key ) ) {
			continue;
		}
		if ( RML_Open( newHost, key.c_str() ) ) {
			synced.insert( key );
		} else {
			failed.insert( key );	// the game resends its list, once is enough to report this
		}
	}
}

void RML_CloseSynced( void ) {
	const std::set<std::string> current = synced;
	for ( const std::string &name : current ) {
		RML_Close( name.c_str() );
	}
}

bool RML_HasSynced( void ) {
	return !synced.empty();
}

bool RML_ClosePreview( void ) {
	for ( auto it = documents.rbegin(); it != documents.rend(); ++it ) {
		if ( !synced.count( it->first ) ) {
			const std::string name = it->first;
			RML_Close( name.c_str() );
			return true;
		}
	}
	return false;
}

static void MoveMouse( int x, int y );

static Rml::Element *FindItem( const char *name ) {
	for ( const auto &entry : documents ) {
		Rml::ElementList items;
		entry.second->GetElementsByClassName( items, "item" );
		for ( Rml::Element *item : items ) {
			if ( !item->IsClassSet( "decoration" ) && !item->IsClassSet( "hidden" ) && !item->IsClassSet( "cvarhidden" )
				&& SameText( item->GetAttribute<Rml::String>( "data-name", "" ), name ) ) {
				return item;
			}
		}
	}
	return NULL;
}

static void MoveMouseOver( Rml::Element *item, float fractionX, float fractionY ) {
	const Rml::Vector2f size = item->GetBox().GetSize( Rml::BoxArea::Border );
	const Rml::Vector2f origin = item->GetAbsoluteOffset( Rml::BoxArea::Border );
	MoveMouse( (int)( origin.x + size.x * fractionX ), (int)( origin.y + size.y * fractionY ) );
}

bool RML_Click( const char *name, float fractionX, float fractionY ) {
	context->Update();
	Rml::Element *item = FindItem( name );
	if ( !item ) {
		return false;
	}
	MoveMouseOver( item, fractionX, fractionY );
	RML_MouseButton( 0, true );
	RML_MouseButton( 0, false );
	return true;
}

bool RML_Drag( const char *name, float fromX, float toX ) {
	context->Update();
	Rml::Element *item = FindItem( name );
	if ( !item ) {
		return false;
	}
	MoveMouseOver( item, fromX, 0.5f );
	RML_MouseButton( 0, true );
	MoveMouseOver( item, ( fromX + toX ) / 2.0f, 0.5f );
	MoveMouseOver( item, toX, 0.5f );
	RML_MouseButton( 0, false );
	return true;
}

void RML_Reload( const rmlHost_t *newHost ) {
	std::vector<std::string> open;
	for ( const auto &entry : documents ) {
		open.push_back( entry.first );
	}

	RML_CloseAll();
	if ( !Start( newHost ) ) {
		return;
	}
	failed.clear();
	Rml::Factory::ClearStyleSheetCache();
	Rml::Factory::ClearTemplateCache();

	for ( const std::string &name : open ) {
		RML_Open( newHost, name.c_str() );
	}
}

void RML_Shutdown( void ) {
	if ( !context ) {
		return;
	}
	RML_CloseAll();
	Rml::Shutdown();
	renderInterface.Reset();
	context = NULL;
}

bool RML_IsActive( void ) {
	return !documents.empty();
}

void RML_Frame( void ) {
	if ( !RML_IsActive() ) {
		return;
	}

	int width, height;
	host->VidSize( &width, &height );
	context->SetDimensions( Rml::Vector2i( ContextWidth( height ), height ) );
	context->SetDensityIndependentPixelRatio( height / (float)VIRTUAL_HEIGHT );
	for ( const auto &entry : documents ) {
		UpdateCvarState( entry.second );
	}
	context->Update();
	context->Render();

	if ( cursorShader ) {
		host->DrawStretchPic( mouseX * (float)VIRTUAL_WIDTH / ContextWidth( height ), mouseY * (float)VIRTUAL_HEIGHT / height, 48, 48, 0, 0, 1, 1, cursorShader );
	}
}

static void MoveMouse( int x, int y ) {
	mouseX = x;
	mouseY = y;
	inputEvent = true;
	context->ProcessMouseMove( mouseX, mouseY, 0 );
	if ( dragItem ) {
		RunItemEvent( dragItem, "drag" );
	}
	inputEvent = false;
}

void RML_MouseMove( int dx, int dy ) {
	int width, height;
	host->VidSize( &width, &height );
	const int contextWidth = ContextWidth( height );
	mouseX = std::max( 0, std::min( mouseX + dx * contextWidth / width, contextWidth - 1 ) );
	mouseY = std::max( 0, std::min( mouseY + dy, height - 1 ) );
	MoveMouse( mouseX, mouseY );
}

void RML_MouseButton( int button, bool down ) {
	inputEvent = true;
	if ( down ) {
		context->ProcessMouseButtonDown( button, 0 );
	} else {
		if ( dragItem && button == 0 ) {
			RunItemEvent( dragItem, "release" );
			dragItem = NULL;
		}
		context->ProcessMouseButtonUp( button, 0 );
	}
	inputEvent = false;
}

void RML_MouseWheel( int direction ) {
	for ( Rml::Element *element = context->GetHoverElement(); element; element = element->GetParentNode() ) {
		if ( element->IsClassSet( "item" ) ) {
			RunItemEvent( element, direction < 0 ? "wheelup" : "wheeldown" );
			return;
		}
	}
}

void RML_Char( int character ) {
	if ( character >= ' ' ) {
		context->ProcessTextInput( (Rml::Character)character );
	}
}
