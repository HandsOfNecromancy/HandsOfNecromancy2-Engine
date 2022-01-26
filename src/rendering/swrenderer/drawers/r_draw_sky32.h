/*
**  Drawer commands for spans
**  Copyright (c) 2016 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#pragma once

#include "swrenderer/drawers/r_draw_rgba.h"
#include "swrenderer/viewport/r_skydrawer.h"

namespace swrenderer
{
	class DrawSkySingle32Command
	{
	public:
		static void DrawColumn(const SkyDrawerArgs& args)
		{
			uint32_t *dest = (uint32_t *)args.Dest();
			int pitch = args.Viewport()->RenderTarget->GetPitch();
			const uint32_t *source0 = (const uint32_t *)args.FrontTexturePixels();
			int textureheight0 = args.FrontTextureHeight();

			int32_t frac = args.TextureVPos();
			int32_t fracstep = args.TextureVStep();
			
			uint32_t solid_top = args.SolidTopColor();
			uint32_t solid_bottom = args.SolidBottomColor();
			bool fadeSky = args.FadeSky();

			int count = args.Count();

			// Find bands for top solid color, top fade, center textured, bottom fade, bottom solid color:
			int start_fade = 2; // How fast it should fade out
			int fade_length = (1 << (24 - start_fade));
			int start_fadetop_y = (-frac) / fracstep;
			int end_fadetop_y = (fade_length - frac) / fracstep;
			int start_fadebottom_y = ((2 << 24) - fade_length - frac) / fracstep;
			int end_fadebottom_y = ((2 << 24) - frac) / fracstep;
			start_fadetop_y = clamp(start_fadetop_y, 0, count);
			end_fadetop_y = clamp(end_fadetop_y, 0, count);
			start_fadebottom_y = clamp(start_fadebottom_y, 0, count);
			end_fadebottom_y = clamp(end_fadebottom_y, 0, count);

			if (!fadeSky)
			{
				for (int index = 0; index < count; index++)
				{
					uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
					*dest = source0[sample_index];
					dest += pitch;
					frac += fracstep;
				}

				return;
			}

			BgraColor solid_top_fill = solid_top;
			BgraColor solid_bottom_fill = solid_bottom;

			int index = 0;

			// Top solid color:
			while (index < start_fadetop_y)
			{
				*dest = solid_top;
				dest += pitch;
				frac += fracstep;
				index++;
			}

			// Top fade:
			while (index < end_fadetop_y)
			{
				uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
				uint32_t fg = source0[sample_index];

				uint32_t alpha = max(min(frac >> (16 - start_fade), 256), 0);
				uint32_t inv_alpha = 256 - alpha;
				
				BgraColor c = fg;
				c.r = (c.r * alpha + solid_top_fill.r * inv_alpha) >> 8;
				c.g = (c.g * alpha + solid_top_fill.g * inv_alpha) >> 8;
				c.b = (c.b * alpha + solid_top_fill.b * inv_alpha) >> 8;
				*dest = c;

				frac += fracstep;
				dest += pitch;
				index++;
			}

			// Textured center:
			while (index < start_fadebottom_y)
			{
				uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
				*dest = source0[sample_index];

				frac += fracstep;
				dest += pitch;
				index++;
			}

			// Fade bottom:
			while (index < end_fadebottom_y)
			{
				uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
				uint32_t fg = source0[sample_index];

				uint32_t alpha = max(min(((2 << 24) - frac) >> (16 - start_fade), 256), 0);
				uint32_t inv_alpha = 256 - alpha;
				
				BgraColor c = fg;
				c.r = (c.r * alpha + solid_top_fill.r * inv_alpha) >> 8;
				c.g = (c.g * alpha + solid_top_fill.g * inv_alpha) >> 8;
				c.b = (c.b * alpha + solid_top_fill.b * inv_alpha) >> 8;
				*dest = c;

				frac += fracstep;
				dest += pitch;
				index++;
			}

			// Bottom solid color:
			while (index < count)
			{
				*dest = solid_bottom;
				dest += pitch;
				index++;
			}
		}
	};
	
	class DrawSkyDouble32Command
	{
	public:
		static void DrawColumn(const SkyDrawerArgs& args)
		{
			uint32_t *dest = (uint32_t *)args.Dest();
			int pitch = args.Viewport()->RenderTarget->GetPitch();
			const uint32_t *source0 = (const uint32_t *)args.FrontTexturePixels();
			const uint32_t *source1 = (const uint32_t *)args.BackTexturePixels();
			int textureheight0 = args.FrontTextureHeight();
			uint32_t maxtextureheight1 = args.BackTextureHeight() - 1;

			int32_t frac = args.TextureVPos();
			int32_t fracstep = args.TextureVStep();

			int count = args.Count();

			uint32_t solid_top = args.SolidTopColor();
			uint32_t solid_bottom = args.SolidBottomColor();
			bool fadeSky = args.FadeSky();
			
			// Find bands for top solid color, top fade, center textured, bottom fade, bottom solid color:
			int start_fade = 2; // How fast it should fade out
			int fade_length = (1 << (24 - start_fade));
			int start_fadetop_y = (-frac) / fracstep;
			int end_fadetop_y = (fade_length - frac) / fracstep;
			int start_fadebottom_y = ((2 << 24) - fade_length - frac) / fracstep;
			int end_fadebottom_y = ((2 << 24) - frac) / fracstep;
			start_fadetop_y = clamp(start_fadetop_y, 0, count);
			end_fadetop_y = clamp(end_fadetop_y, 0, count);
			start_fadebottom_y = clamp(start_fadebottom_y, 0, count);
			end_fadebottom_y = clamp(end_fadebottom_y, 0, count);

			if (!fadeSky)
			{
				for (int index = 0; index < count; index++)
				{
					uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
					uint32_t fg = source0[sample_index];
					if (fg == 0)
					{
						uint32_t sample_index2 = min(sample_index, maxtextureheight1);
						fg = source1[sample_index2];
					}

					*dest = fg;
					dest += pitch;
					frac += fracstep;
				}

				return;
			}

			BgraColor solid_top_fill = solid_top;
			BgraColor solid_bottom_fill = solid_bottom;

			int index = 0;

			// Top solid color:
			while (index < start_fadetop_y)
			{
				*dest = solid_top;
				dest += pitch;
				frac += fracstep;
				index++;
			}

			// Top fade:
			while (index < end_fadetop_y)
			{
				uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
				uint32_t fg = source0[sample_index];
				if (fg == 0)
				{
					uint32_t sample_index2 = min(sample_index, maxtextureheight1);
					fg = source1[sample_index2];
				}

				uint32_t alpha = max(min(frac >> (16 - start_fade), 256), 0);
				uint32_t inv_alpha = 256 - alpha;
				
				BgraColor c = fg;
				c = (c * alpha + solid_top_fill * inv_alpha) >> 8;
				*dest = c;

				frac += fracstep;
				dest += pitch;
				index++;
			}

			// Textured center:
			while (index < start_fadebottom_y)
			{
				uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
				uint32_t fg = source0[sample_index];
				if (fg == 0)
				{
					uint32_t sample_index2 = min(sample_index, maxtextureheight1);
					fg = source1[sample_index2];
				}
				*dest = fg;

				frac += fracstep;
				dest += pitch;
				index++;
			}

			// Fade bottom:
			while (index < end_fadebottom_y)
			{
				uint32_t sample_index = (((((uint32_t)frac) << 8) >> FRACBITS) * textureheight0) >> FRACBITS;
				uint32_t fg = source0[sample_index];
				if (fg == 0)
				{
					uint32_t sample_index2 = min(sample_index, maxtextureheight1);
					fg = source1[sample_index2];
				}

				uint32_t alpha = max(min(((2 << 24) - frac) >> (16 - start_fade), 256), 0);
				uint32_t inv_alpha = 256 - alpha;
				
				BgraColor c = fg;
				c = (c * alpha + solid_top_fill * inv_alpha) >> 8;
				*dest = c;

				frac += fracstep;
				dest += pitch;
				index++;
			}

			// Bottom solid color:
			while (index < count)
			{
				*dest = solid_bottom;
				dest += pitch;
				index++;
			}
		}
	};
}
