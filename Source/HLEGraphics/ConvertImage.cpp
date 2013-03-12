/*
Copyright (C) 2001 StrmnNrmn

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "ConvertImage.h"
#include "TextureInfo.h"

#include "DLDebug.h"
#include "Core/Memory.h"

#include "N64PixelFormat.h"
#include "Graphics/NativePixelFormat.h"

#include "Math/MathUtil.h"

#include "RDP.h"

#include "OSHLE/ultra_gbi.h"

namespace
{

static const u8 OneToEight[2] =
{
	0x00,		// 0 -> 00 00 00 00
	0xff		// 1 -> 11 11 11 11
};

static const u8 ThreeToEight[8] =
{
	0x00,		// 000 -> 00 00 00 00
	0x24,		// 001 -> 00 10 01 00
	0x49,		// 010 -> 01 00 10 01
	0x6d,       // 011 -> 01 10 11 01
	0x92,       // 100 -> 10 01 00 10
	0xb6,		// 101 -> 10 11 01 10
	0xdb,		// 110 -> 11 01 10 11
	0xff		// 111 -> 11 11 11 11
};


static const u8 FourToEight[16] =
{
	0x00, 0x11, 0x22, 0x33,
	0x44, 0x55, 0x66, 0x77,
	0x88, 0x99, 0xaa, 0xbb,
	0xcc, 0xdd, 0xee, 0xff
};


template< u32 Size >
struct SByteswapInfo;

template<> struct SByteswapInfo< 1 >
{
	enum { Fiddle = 3 };
};
template<> struct SByteswapInfo< 2 >
{
	enum { Fiddle = 2 };
};
template<> struct SByteswapInfo< 4 >
{
	enum { Fiddle = 0 };
};

template< u32 Size >
struct SSwizzleInfo;

template<> struct SSwizzleInfo< 1 >
{
	enum { Swizzle = 4 };
};
template<> struct SSwizzleInfo< 2 >
{
	enum { Swizzle = 2 };
};
template<> struct SSwizzleInfo< 4 >
{
	enum { Swizzle = 2 };
};

template < typename OutT >
struct SConvertGeneric
{
typedef void (*ConvertRowFunction)( OutT * p_dst, const u8 * p_src_base, u32 offset, u32 width );


static void ConvertGeneric( const TextureDestInfo & dst,
							const TextureInfo & ti,
							ConvertRowFunction swapped_fn,
							ConvertRowFunction unswapped_fn )
{
	OutT *		p_dst       = reinterpret_cast< OutT * >( dst.pSurface );
	const u8 *	p_src_base  = g_pu8RamBase;
	u32			base_offset = ti.GetLoadAddress();
	u32			src_pitch   = ti.GetPitch();

	if ( ti.IsSwapped())
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			if ((y&1) == 0)
			{
				unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );
			}
			else
			{
				swapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );
			}

			base_offset += src_pitch;
			p_dst = reinterpret_cast< OutT * >( (u8*)p_dst + dst.Pitch );
		}
	}
	else
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );

			base_offset += src_pitch;
			p_dst = reinterpret_cast< OutT * >( (u8*)p_dst + dst.Pitch );
		}
	}
}

};

typedef void (*ConvertPalettisedRowFunction)( NativePf8888 * p_dst, const u8 * p_src_base, u32 offset, u32 width, const u16 * p_palette );

static void ConvertGenericPalettised( const TextureDestInfo & dst, const TextureInfo & ti, ConvertPalettisedRowFunction swapped_fn, ConvertPalettisedRowFunction unswapped_fn )
{
	NativePf8888 *	p_dst       = reinterpret_cast< NativePf8888 * >( dst.pSurface );
	const u8 *		p_src_base  = g_pu8RamBase;
	u32				base_offset = ti.GetLoadAddress();
	u32				src_pitch   = ti.GetPitch();
	const u16 *		p_palette   = reinterpret_cast< const u16 * >( ti.GetTlutAddress() );

	if (ti.IsSwapped())
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			if ((y&1) == 0)
			{
				unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth(), p_palette );
			}
			else
			{
				swapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth(), p_palette );
			}

			base_offset += src_pitch;
			p_dst = reinterpret_cast< NativePf8888 * >( (u8*)p_dst + dst.Pitch );
		}
	}
	else
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth(), p_palette );

			base_offset += src_pitch;
			p_dst = reinterpret_cast< NativePf8888 * >( (u8*)p_dst + dst.Pitch );
		}
	}
}


typedef void (*ConvertPaletteFn)( NativePf8888 * p_dst, const u8 * p_palette, u32 entries );
typedef void (*ConvertPalettisedCI4RowFunction)( NativePfCI44 * p_dst, const u8 * p_src_base, u32 offset, u32 width );

static void ConvertGenericPalettisedCI4( const TextureDestInfo & dst, const TextureInfo & ti, ConvertPalettisedCI4RowFunction swapped_fn, ConvertPalettisedCI4RowFunction unswapped_fn, ConvertPaletteFn palette_fn )
{
	NativePfCI44 *	p_dst       = reinterpret_cast< NativePfCI44 * >( dst.pSurface );
	const u8 *		p_src_base  = g_pu8RamBase;
	u32				base_offset = ti.GetLoadAddress();
	u32				src_pitch   = ti.GetPitch();

	if (ti.IsSwapped())
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			if ((y&1) == 0)
			{
				unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );
			}
			else
			{
				swapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );
			}

			base_offset += src_pitch;
			p_dst = reinterpret_cast< NativePfCI44 * >( (u8*)p_dst + dst.Pitch );
		}
	}
	else
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );

			base_offset += src_pitch;
			p_dst = reinterpret_cast< NativePfCI44 * >( (u8*)p_dst + dst.Pitch );
		}
	}


	NativePf8888 *	p_dst_palette = reinterpret_cast< NativePf8888 * >( dst.Palette );
	const u8 *		p_palette     = reinterpret_cast< const u8 * >( ti.GetTlutAddress() );

	palette_fn( p_dst_palette, p_palette, 16 );
}

typedef void (*ConvertPalettisedCI8RowFunction)( NativePfCI8 * p_dst, const u8 * p_src_base, u32 offset, u32 width );

static void ConvertGenericPalettisedCI8( const TextureDestInfo & dst, const TextureInfo & ti, ConvertPalettisedCI8RowFunction swapped_fn, ConvertPalettisedCI8RowFunction unswapped_fn, ConvertPaletteFn palette_fn )
{
	NativePfCI8 *	p_dst       = reinterpret_cast< NativePfCI8 * >( dst.pSurface );
	const u8 *		p_src_base  = g_pu8RamBase;
	u32				base_offset = ti.GetLoadAddress();
	u32				src_pitch   = ti.GetPitch();

	if (ti.IsSwapped())
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			if ((y&1) == 0)
			{
				unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );
			}
			else
			{
				swapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );
			}

			base_offset += src_pitch;
			p_dst = reinterpret_cast< NativePfCI8 * >( (u8*)p_dst + dst.Pitch );
		}
	}
	else
	{
		for (u32 y = 0; y < ti.GetHeight(); y++)
		{
			unswapped_fn( p_dst, p_src_base, base_offset, ti.GetWidth() );

			base_offset += src_pitch;
			p_dst = reinterpret_cast< NativePfCI8 * >( (u8*)p_dst + dst.Pitch );
		}
	}


	NativePf8888 *	p_dst_palette = reinterpret_cast< NativePf8888 * >( dst.Palette );
	const u8 *		p_palette     = reinterpret_cast< const u8 * >( ti.GetTlutAddress() );

	palette_fn( p_dst_palette, p_palette, 256 );
}

template < typename InT >
struct SConvert
{
	enum { Fiddle = SByteswapInfo< sizeof( InT ) >::Fiddle };
	enum { Swizzle = SSwizzleInfo< sizeof( InT ) >::Swizzle };

	//
	//	This routine converts from any format which is > 1 byte to any Psp format.
	//
	template < typename OutT, u32 InFiddle, u32 OutFiddle >
	static inline void ConvertRow( OutT * p_dst, const u8 * p_src_base, u32 offset, u32 width )
	{
		DAEDALUS_DL_ASSERT( IsAligned( offset, sizeof( InT ) ), "Offset should be correctly aligned" );

		//
		//	Need to be careful of this - ensure that it's doing the right thing in all cases and not overflowing rows.
		//	This is to ensure that we correctly convert all the texels in a row, even when we're fiddling.
		//	If we have a fiddle of 2 for instance, and the row is not a multiple of the fiddle amount
		//	then we don't convert enough pixels (we actually poke some values in past the end of the row)
		//	and get some random noise at the end instead.
		//
		//	There may well be an easier (and less gross)  way of doing this if we move the OutFiddle calculation
		//	into the source pixel lookup, and just have p_dst[x] = ...
		//
		width = AlignPow2( width, 1<<OutFiddle );

		for (u32 x = 0; x < width; x++)
		{
			InT	colour( *reinterpret_cast< const InT * >( &p_src_base[offset ^ InFiddle] ) );

			p_dst[x ^ OutFiddle] = ConvertPixelFormat< OutT, InT >( colour );

			offset += sizeof( InT );
		}
	}

	template < typename OutT >
	static inline void ConvertTextureT( const TextureDestInfo & dst, const TextureInfo & ti )
	{
		SConvertGeneric< OutT >::ConvertGeneric( dst, ti, ConvertRow< OutT, Fiddle, Swizzle >, ConvertRow< OutT, Fiddle, 0 > );
	}

	static void ConvertTexture( const TextureDestInfo & dst, const TextureInfo & ti )
	{
		switch( dst.Format )
		{
		case TexFmt_5650:	ConvertTextureT< NativePf5650 >( dst, ti ); return;
		case TexFmt_5551:	ConvertTextureT< NativePf5551 >( dst, ti ); return;
		case TexFmt_4444:	ConvertTextureT< NativePf4444 >( dst, ti ); return;
		case TexFmt_8888:	ConvertTextureT< NativePf8888 >( dst, ti ); return;

		case TexFmt_CI4_8888: break;
		case TexFmt_CI8_8888: break;

		}

		DAEDALUS_DL_ERROR( "Unhandled format" );
	}
};

struct SConvertIA4
{
	enum { Fiddle = 0x3 };

	template < typename OutT, u32 F >
	static inline void ConvertRow( OutT * p_dst, const u8 * p_src_base, u32 offset, u32 width )
	{
		// Do two pixels at a time
		for (u32 x = 0; x < width; x+=2)
		{
			u8 b = p_src_base[offset ^ F];

			// Even
			p_dst[x + 0] = OutT( ThreeToEight[(b & 0xE0) >> 5],
								 ThreeToEight[(b & 0xE0) >> 5],
								 ThreeToEight[(b & 0xE0) >> 5],
								 OneToEight[(b & 0x10)   >> 4]);
			// Odd
			p_dst[x + 1] = OutT( ThreeToEight[(b & 0x0E) >> 1],
								 ThreeToEight[(b & 0x0E) >> 1],
								 ThreeToEight[(b & 0x0E) >> 1],
								 OneToEight[(b & 0x01)     ] );
			offset++;
		}

		if(width & 1)
		{
			u8 b = p_src_base[offset ^ F];

			// Even
			p_dst[width-1] = OutT( ThreeToEight[(b & 0xE0) >> 5],
								   ThreeToEight[(b & 0xE0) >> 5],
								   ThreeToEight[(b & 0xE0) >> 5],
								   OneToEight[(b & 0x10)   >> 4]);
		}
	}

	template < typename OutT >
	static inline void ConvertTextureT( const TextureDestInfo & dst, const TextureInfo & ti )
	{
		SConvertGeneric< OutT >::ConvertGeneric( dst, ti, ConvertRow< OutT, 0x4 | Fiddle >, ConvertRow< OutT, Fiddle > );
	}

	static void ConvertTexture( const TextureDestInfo & dst, const TextureInfo & ti )
	{
		switch( dst.Format )
		{
		case TexFmt_5650:	ConvertTextureT< NativePf5650 >( dst, ti ); return;
		case TexFmt_5551:	ConvertTextureT< NativePf5551 >( dst, ti ); return;
		case TexFmt_4444:	ConvertTextureT< NativePf4444 >( dst, ti ); return;
		case TexFmt_8888:	ConvertTextureT< NativePf8888 >( dst, ti ); return;

		case TexFmt_CI4_8888: break;
		case TexFmt_CI8_8888: break;

		}

		DAEDALUS_DL_ERROR( "Unhandled format" );
	}
};

struct SConvertI4
{
	enum { Fiddle = 0x3 };

	template< typename OutT, u32 F >
	static inline void ConvertRow( OutT * p_dst, const u8 * p_src_base, u32 offset, u32 width )
	{
		// Do two pixels at a time
		for ( u32 x = 0; x+1 < width; x+=2 )
		{
			u8 b = p_src_base[offset ^ F];

			// Even
			p_dst[x + 0] = OutT( FourToEight[(b & 0xF0)>>4],
								 FourToEight[(b & 0xF0)>>4],
								 FourToEight[(b & 0xF0)>>4],
								 FourToEight[(b & 0xF0)>>4] );
			// Odd
			p_dst[x + 1] = OutT( FourToEight[(b & 0x0F)],
								 FourToEight[(b & 0x0F)],
								 FourToEight[(b & 0x0F)],
								 FourToEight[(b & 0x0F)] );

			offset++;
		}

		if(width & 1)
		{
			u8 b = p_src_base[offset ^ F];

			// Even
			p_dst[width-1] = OutT( FourToEight[(b & 0xF0)>>4],
								   FourToEight[(b & 0xF0)>>4],
								   FourToEight[(b & 0xF0)>>4],
								   FourToEight[(b & 0xF0)>>4] );

		}
	}

	template < typename OutT >
	static inline void ConvertTextureT( const TextureDestInfo & dst, const TextureInfo & ti )
	{
		SConvertGeneric< OutT >::ConvertGeneric( dst, ti, ConvertRow< OutT, 0x4 | Fiddle >, ConvertRow< OutT, Fiddle > );
	}

	static void ConvertTexture( const TextureDestInfo & dst, const TextureInfo & ti )
	{
		switch( dst.Format )
		{
		case TexFmt_5650:	ConvertTextureT< NativePf5650 >( dst, ti ); return;
		case TexFmt_5551:	ConvertTextureT< NativePf5551 >( dst, ti ); return;
		case TexFmt_4444:	ConvertTextureT< NativePf4444 >( dst, ti ); return;
		case TexFmt_8888:	ConvertTextureT< NativePf8888 >( dst, ti ); return;

		case TexFmt_CI4_8888: break;
		case TexFmt_CI8_8888: break;

		}

		DAEDALUS_DL_ERROR( "Unhandled format" );
	}
};

template< typename PalT, u32 F >
static void ConvertPalette( NativePf8888 * p_dst, const u8 * p_palette, u32 entries )
{
	const PalT * p_n64pal = reinterpret_cast< const PalT * >( p_palette );

	for( u32 i = 0; i < entries; ++i )
	{
		p_dst[ i ] = NativePf8888::Make( p_n64pal[ i ^ F ] );
	}
}

template< u32 F >
static void ConvertCI4_Row( NativePfCI44 * p_dst, const u8 * p_src_base, u32 offset, u32 width )
{
	for (u32 x = 0; x+1 < width; x+=2)
	{
		u8 b = p_src_base[offset ^ F];

		p_dst[ x/2 ].Bits = (b >> 4) | (b << 4);

		offset++;
	}

	// Handle any remaining odd pixels
	if( width & 1 )
	{
		u8 b = p_src_base[offset ^ F];

		p_dst[ width/2 ].Bits = (b >> 4) | 0;
	}
}

template< typename PalT, u32 F >
static void ConvertCI4_Row_To_8888( NativePf8888 * p_dst, const u8 * p_src_base, u32 offset, u32 width, const u16 * p_palette )
{
	const PalT * p_n64pal = reinterpret_cast< const PalT * >( p_palette );

	for (u32 x = 0; x < width; x+=2)
	{
		u8 b = p_src_base[offset ^ F];

		u8 bhi = (b&0xf0)>>4;
		u8 blo = (b&0x0f);

		p_dst[ x + 0 ] = NativePf8888::Make( p_n64pal[ bhi ^ 0x1 ] );	// Remember palette is in different endian order!
		p_dst[ x + 1 ] = NativePf8888::Make( p_n64pal[ blo ^ 0x1 ] );

		offset++;
	}

	// Handle any remaining odd pixels
	if(width & 1)
	{
		u8 b = p_src_base[offset ^ F];

		u8 bhi = (b&0xf0)>>4;

		p_dst[width-1] = NativePf8888::Make( p_n64pal[ bhi ^ 0x1 ] );	// Remember palette is in different endian order!
	}
}

template< u32 F >
static void ConvertCI8_Row( NativePfCI8 * p_dst, const u8 * p_src_base, u32 offset, u32 width )
{
	for (u32 x = 0; x < width; x++)
	{
		u8 b = p_src_base[offset ^ F];

		p_dst[ x ].Bits = b;

		offset++;
	}
}

template< typename PalT, u32 F >
static  void ConvertCI8_Row_To_8888( NativePf8888 * p_dst, const u8 * p_src_base, u32 offset, u32 width, const u16 * p_palette )
{
	const PalT * p_n64pal = reinterpret_cast< const PalT * >( p_palette );

	for (u32 x = 0; x < width; x++)
	{
		u8 b = p_src_base[offset ^ F];

		p_dst[ x ] = NativePf8888::Make( p_n64pal[ b ^ 0x1 ] );	// Remember palette is in different endian order!

		offset++;
	}
}

static void ConvertRGBA16(const TextureDestInfo & dst, const TextureInfo & ti)
{
	SConvert< N64Pf5551 >::ConvertTexture( dst, ti );
}

static void ConvertRGBA32(const TextureDestInfo & dst, const TextureInfo & ti)
{
	// Did have Fiddle of 8 here, pretty sure this was wrong (should have been 4)
	SConvert< N64Pf8888 >::ConvertTexture( dst, ti );
}

static void ConvertIA4(const TextureDestInfo & dst, const TextureInfo & ti)
{
	SConvertIA4::ConvertTexture( dst, ti );
}

static void ConvertIA8(const TextureDestInfo & dst, const TextureInfo & ti)
{
	SConvert< N64PfIA8 >::ConvertTexture( dst, ti );
}

static void ConvertIA16(const TextureDestInfo & dst, const TextureInfo & ti)
{
	SConvert< N64PfIA16 >::ConvertTexture( dst, ti );
}

static void ConvertI4(const TextureDestInfo & dst, const TextureInfo & ti)
{
	SConvertI4::ConvertTexture( dst, ti );
}

static void ConvertI8(const TextureDestInfo & dst, const TextureInfo & ti)
{
	SConvert< N64PfI8 >::ConvertTexture( dst, ti );
}

static void ConvertCI8(const TextureDestInfo & dst, const TextureInfo & ti)
{
	ETLutFmt tlut_format = ti.GetTLutFormat();
	if( tlut_format == kTT_RGBA16 )
	{
		switch( dst.Format )
		{
		case TexFmt_8888:
			ConvertGenericPalettised( dst, ti, ConvertCI8_Row_To_8888< N64Pf5551, 0x4 | 0x3 >, ConvertCI8_Row_To_8888< N64Pf5551, 0x3 > );
			break;

		case TexFmt_CI8_8888:
			ConvertGenericPalettisedCI8( dst, ti, ConvertCI8_Row< 0x4 | 0x3 >, ConvertCI8_Row< 0x3 >, ConvertPalette< N64Pf5551, 0x1 > );
			break;

		default:
			DAEDALUS_ERROR( "Unhandled format for CI8RGB16 textures" );
			break;
		}
	}
	else if( tlut_format == kTT_IA16 )
	{
		switch( dst.Format )
		{
		case TexFmt_8888:
			ConvertGenericPalettised( dst, ti, ConvertCI8_Row_To_8888< N64PfIA16, 0x4 | 0x3 >, ConvertCI8_Row_To_8888< N64PfIA16, 0x3 > );
			break;

		case TexFmt_CI8_8888:
			ConvertGenericPalettisedCI8( dst, ti, ConvertCI8_Row< 0x4 | 0x3 >, ConvertCI8_Row< 0x3 >, ConvertPalette< N64PfIA16, 0x1 > );
			break;

		default:
			DAEDALUS_ERROR( "Unhandled format for CI8IA16 textures" );
			break;
		}
	}
}

static void ConvertCI4(const TextureDestInfo & dst, const TextureInfo & ti)
{
	ETLutFmt tlut_format = ti.GetTLutFormat();
	if( tlut_format == kTT_RGBA16 )
	{
		switch( dst.Format )
		{
		case TexFmt_8888:
			ConvertGenericPalettised( dst, ti, ConvertCI4_Row_To_8888< N64Pf5551, 0x4 | 0x3 >, ConvertCI4_Row_To_8888< N64Pf5551, 0x3 > );
			break;

		case TexFmt_CI4_8888:
			ConvertGenericPalettisedCI4( dst, ti, ConvertCI4_Row< 0x4 | 0x3 >, ConvertCI4_Row< 0x3 >, ConvertPalette< N64Pf5551, 0x1 > );
			break;

		default:
			DAEDALUS_ERROR( "Unhandled format for CI4RGB16 textures" );
			break;
		}
	}
	else if( tlut_format == kTT_IA16 )
	{
		switch( dst.Format )
		{
		case TexFmt_8888:
			ConvertGenericPalettised( dst, ti, ConvertCI4_Row_To_8888< N64PfIA16, 0x4 | 0x3 >, ConvertCI4_Row_To_8888< N64PfIA16, 0x3 > );
			break;

		case TexFmt_CI4_8888:
			ConvertGenericPalettisedCI4( dst, ti, ConvertCI4_Row< 0x4 | 0x3 >, ConvertCI4_Row< 0x3 >, ConvertPalette< N64PfIA16, 0x1 > );
			break;

		default:
			DAEDALUS_ERROR( "Unhandled format for CI4IA16 textures" );
			break;
		}
	}
}

} // anonymous namespace

//*****************************************************************************
//
//*****************************************************************************
typedef void ( *ConvertFunction )( const TextureDestInfo & dst, const TextureInfo & ti);
static const ConvertFunction gConvertFunctions[ 32 ] =
{
	// 4bpp				8bpp			16bpp				32bpp
	NULL,			NULL,			ConvertRGBA16,		ConvertRGBA32,			// RGBA
	NULL,			NULL,			NULL,				NULL,					// YUV
	ConvertCI4,		ConvertCI8,		NULL,				NULL,					// CI
	ConvertIA4,		ConvertIA8,		ConvertIA16,		NULL,					// IA
	ConvertI4,		ConvertI8,		NULL,				NULL,					// I
	NULL,			NULL,			NULL,				NULL,					// ?
	NULL,			NULL,			NULL,				NULL,					// ?
	NULL,			NULL,			NULL,				NULL					// ?
};

bool DoConversion(TextureDestInfo & dst, const TextureInfo & texture_info)
{
	const ConvertFunction fn = gConvertFunctions[ (texture_info.GetFormat() << 2) | texture_info.GetSize() ];
	if( fn )
	{
		fn( dst, texture_info );
		return true;
	}

	return false;
}

