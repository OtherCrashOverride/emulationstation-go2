#if defined(USE_OPENGLES_10)

#include "renderers/Renderer.h"
#include "math/Transform4x4f.h"
#include "Log.h"
#include "Settings.h"

#include <GLES/gl.h>
#include <SDL.h>

#include <go2/display.h>
#include <go2/input.h>
#include <go2/audio.h>
#include <drm/drm_fourcc.h>
#include <ctime>
#include "BatteryIcons.h"
#include "VolumeIcons.h"
#include "OdroidImage.h"

bool g_screenshot_requested = false;

static go2_input_t* input = nullptr;
static go2_surface_t* titlebarSurface = nullptr;
static unsigned int frame = 0;

namespace Renderer
{
	//static SDL_GLContext sdlContext = nullptr;
	static go2_context_t* context = nullptr;
	static go2_presenter_t* presenter = nullptr;

	static GLenum convertBlendFactor(const Blend::Factor _blendFactor)
	{
		switch(_blendFactor)
		{
			case Blend::ZERO:                { return GL_ZERO;                } break;
			case Blend::ONE:                 { return GL_ONE;                 } break;
			case Blend::SRC_COLOR:           { return GL_SRC_COLOR;           } break;
			case Blend::ONE_MINUS_SRC_COLOR: { return GL_ONE_MINUS_SRC_COLOR; } break;
			case Blend::SRC_ALPHA:           { return GL_SRC_ALPHA;           } break;
			case Blend::ONE_MINUS_SRC_ALPHA: { return GL_ONE_MINUS_SRC_ALPHA; } break;
			case Blend::DST_COLOR:           { return GL_DST_COLOR;           } break;
			case Blend::ONE_MINUS_DST_COLOR: { return GL_ONE_MINUS_DST_COLOR; } break;
			case Blend::DST_ALPHA:           { return GL_DST_ALPHA;           } break;
			case Blend::ONE_MINUS_DST_ALPHA: { return GL_ONE_MINUS_DST_ALPHA; } break;
			default:                         { return GL_ZERO;                }
		}

	} // convertBlendFactor

	static GLenum convertTextureType(const Texture::Type _type)
	{
		switch(_type)
		{
			case Texture::RGBA:  { return GL_RGBA;  } break;
			case Texture::ALPHA: { return GL_ALPHA; } break;
			default:             { return GL_ZERO;  }
		}

	} // convertTextureType

	unsigned int convertColor(const unsigned int _color)
	{
		// convert from rgba to abgr
		unsigned char r = ((_color & 0xff000000) >> 24) & 255;
		unsigned char g = ((_color & 0x00ff0000) >> 16) & 255;
		unsigned char b = ((_color & 0x0000ff00) >>  8) & 255;
		unsigned char a = ((_color & 0x000000ff)      ) & 255;

		return ((a << 24) | (b << 16) | (g << 8) | (r));

	} // convertColor

	unsigned int getWindowFlags()
	{
		return SDL_WINDOW_OPENGL;

	} // getWindowFlags

	void setupWindow()
	{
#if 0
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE,     8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,   8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,    8);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,  24);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 0);
#endif
	} // setupWindow

	void createContext()
	{
		// sdlContext = SDL_GL_CreateContext(getSDLWindow());
		// SDL_GL_MakeCurrent(getSDLWindow(), sdlContext);
		input = go2_input_create();

		go2_context_attributes_t attr;
		attr.major = 1;
		attr.minor = 0;
		attr.red_bits = 8;
		attr.green_bits = 8;
		attr.blue_bits = 8;
		attr.alpha_bits = 8;
		attr.depth_bits = 24;
		attr.stencil_bits = 0;
		
		go2_display_t* display = getDisplay();
		int w = go2_display_height_get(display);
		int h = go2_display_width_get(display);

		titlebarSurface = go2_surface_create(display, w, 16, DRM_FORMAT_RGB565);

		context = go2_context_create(display, w, h, &attr);
		go2_context_make_current(context);

		presenter = go2_presenter_create(display, DRM_FORMAT_RGB565, 0xff080808);

		glClearColor(0.5f, 0.5f, 0.5f, 0.0f);

		std::string glExts = (const char*)glGetString(GL_EXTENSIONS);
		LOG(LogInfo) << "Checking available OpenGL extensions...";
		LOG(LogInfo) << " ARB_texture_non_power_of_two: " << (glExts.find("ARB_texture_non_power_of_two") != std::string::npos ? "ok" : "MISSING");

	} // createContext

	void destroyContext()
	{
		//SDL_GL_DeleteContext(sdlContext);
		//sdlContext = nullptr;
		go2_context_destroy(context);
		context = nullptr;

		go2_presenter_destroy(presenter);
		presenter = nullptr;

		go2_surface_destroy(titlebarSurface);
		titlebarSurface = nullptr;

		go2_input_destroy(input);
		input = nullptr;
	} // destroyContext

	unsigned int createTexture(const Texture::Type _type, const bool _linear, const bool _repeat, const unsigned int _width, const unsigned int _height, void* _data)
	{
		const GLenum type = convertTextureType(_type);
		unsigned int texture;

		glGenTextures(1, &texture);
		bindTexture(texture);

		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, _repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, _repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);

		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, _linear ? GL_LINEAR : GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glPixelStorei(GL_PACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		glTexImage2D(GL_TEXTURE_2D, 0, type, _width, _height, 0, type, GL_UNSIGNED_BYTE, _data);

		return texture;

	} // createTexture

	void destroyTexture(const unsigned int _texture)
	{
		glDeleteTextures(1, &_texture);

	} // destroyTexture

	void updateTexture(const unsigned int _texture, const Texture::Type _type, const unsigned int _x, const unsigned _y, const unsigned int _width, const unsigned int _height, void* _data)
	{
		bindTexture(_texture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, _x, _y, _width, _height, convertTextureType(_type), GL_UNSIGNED_BYTE, _data);
		bindTexture(0);

	} // updateTexture

	void bindTexture(const unsigned int _texture)
	{
		glBindTexture(GL_TEXTURE_2D, _texture);

		if(_texture == 0) glDisable(GL_TEXTURE_2D);
		else              glEnable(GL_TEXTURE_2D);

	} // bindTexture

	void drawLines(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		glEnable(GL_BLEND);
		glBlendFunc(convertBlendFactor(_srcBlendFactor), convertBlendFactor(_dstBlendFactor));

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);

		glVertexPointer(  2, GL_FLOAT,         sizeof(Vertex), &_vertices[0].pos);
		glTexCoordPointer(2, GL_FLOAT,         sizeof(Vertex), &_vertices[0].tex);
		glColorPointer(   4, GL_UNSIGNED_BYTE, sizeof(Vertex), &_vertices[0].col);

		glDrawArrays(GL_LINES, 0, _numVertices);

		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);

		glDisable(GL_BLEND);

	} // drawLines

	void drawTriangleStrips(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		glEnable(GL_BLEND);
		glBlendFunc(convertBlendFactor(_srcBlendFactor), convertBlendFactor(_dstBlendFactor));

		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);

		glVertexPointer(  2, GL_FLOAT,         sizeof(Vertex), &_vertices[0].pos);
		glTexCoordPointer(2, GL_FLOAT,         sizeof(Vertex), &_vertices[0].tex);
		glColorPointer(   4, GL_UNSIGNED_BYTE, sizeof(Vertex), &_vertices[0].col);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, _numVertices);

		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);

		glDisable(GL_BLEND);

	} // drawTriangleStrips

	void setProjection(const Transform4x4f& _projection)
	{
		glMatrixMode(GL_PROJECTION);
		glLoadMatrixf((GLfloat*)&_projection);

	} // setProjection

	void setMatrix(const Transform4x4f& _matrix)
	{
		Transform4x4f matrix = _matrix;
		matrix.round();
		glMatrixMode(GL_MODELVIEW);
		glLoadMatrixf((GLfloat*)&matrix);

	} // setMatrix

	void setViewport(const Rect& _viewport)
	{
		// glViewport starts at the bottom left of the window
		glViewport( _viewport.x, getWindowHeight() - _viewport.y - _viewport.h, _viewport.w, _viewport.h);

	} // setViewport

	void setScissor(const Rect& _scissor)
	{
		if((_scissor.x == 0) && (_scissor.y == 0) && (_scissor.w == 0) && (_scissor.h == 0))
		{
			glDisable(GL_SCISSOR_TEST);
		}
		else
		{
			// glScissor starts at the bottom left of the window
			glScissor(_scissor.x, getWindowHeight() - _scissor.y - _scissor.h, _scissor.w, _scissor.h);
			glEnable(GL_SCISSOR_TEST);
		}

	} // setScissor

	void setSwapInterval()
	{
#if 0
		// vsync
		if(Settings::getInstance()->getBool("VSync"))
		{
			// SDL_GL_SetSwapInterval(0) for immediate updates (no vsync, default),
			// 1 for updates synchronized with the vertical retrace,
			// or -1 for late swap tearing.
			// SDL_GL_SetSwapInterval returns 0 on success, -1 on error.
			// if vsync is requested, try normal vsync; if that doesn't work, try late swap tearing
			// if that doesn't work, report an error
			if(SDL_GL_SetSwapInterval(1) != 0 && SDL_GL_SetSwapInterval(-1) != 0)
				LOG(LogWarning) << "Tried to enable vsync, but failed! (" << SDL_GetError() << ")";
		}
		else
			SDL_GL_SetSwapInterval(0);
#endif
	} // setSwapInterval

	void swapBuffers()
	{
		//SDL_GL_SwapWindow(getSDLWindow());

		if (context)
		{
			go2_display_t* display = getDisplay();
			int w = go2_display_height_get(display);
			int h = go2_display_width_get(display);

			{
				// Battery level
				const uint8_t* src = battery_image.pixel_data;
				int src_stride = 32 * sizeof(short);

				uint8_t* dst = (uint8_t*)go2_surface_map(titlebarSurface);
				int dst_stride = go2_surface_stride_get(titlebarSurface);

				go2_battery_state_t batteryState;
				go2_input_battery_read(input, &batteryState);

				int batteryIndex;
				if (batteryState.level <= 5)
				{
					batteryIndex = 0;
				}
				else if (batteryState.level <= 25)
				{
					batteryIndex = 1;
				}
				else if (batteryState.level <= 50)
				{
					batteryIndex = 2;
				}
				else if (batteryState.level <= 75)
				{
					batteryIndex = 3;
				}
				else
				{
					batteryIndex = 4;
				}
				
				src += (batteryIndex * 16 * src_stride);
				dst += (w - 32) * sizeof(short);

				for (int y = 0; y < 16; ++y)
				{
					memcpy(dst, src, 32 * sizeof(short));

					src += src_stride;
					dst += dst_stride;
				}
			}
			
			{
				// Volume level
				const uint8_t* src = volume_image.pixel_data;
				int src_stride = 32 * sizeof(short);

				uint8_t* dst = (uint8_t*)go2_surface_map(titlebarSurface);
				int dst_stride = go2_surface_stride_get(titlebarSurface);

				uint32_t volume = go2_audio_volume_get(NULL);

				int volumeIndex;
				if (volume == 0)
				{
					volumeIndex = 0;
				}
				else if (volume <= 20)
				{
					volumeIndex = 1;
				}
				else if (volume <= 40)
				{
					volumeIndex = 2;
				}
				else if (volume <= 60)
				{
					volumeIndex = 3;
				}
				else if (volume <= 80)
				{
					volumeIndex = 4;
				}
				else
				{
					volumeIndex = 5;
				}
				
				src += (volumeIndex * 16 * src_stride);
				//dst += (480 - 32) * sizeof(short);

				for (int y = 0; y < 16; ++y)
				{
					memcpy(dst, src, 32 * sizeof(short));

					src += src_stride;
					dst += dst_stride;
				}
			}

			{
				// Title
				const uint8_t* src = odroid_image.pixel_data;
				int src_stride = odroid_image.width * sizeof(short);

				uint8_t* dst = (uint8_t*)go2_surface_map(titlebarSurface);
				int dst_stride = go2_surface_stride_get(titlebarSurface);

				dst += ((w / 2) - (odroid_image.width / 2)) * sizeof(short);

				for (int y = 0; y < 16; ++y)
				{
					memcpy(dst, src, src_stride);

					src += src_stride;
					dst += dst_stride;
				}
			}


			go2_context_swap_buffers(context);
			go2_surface_t* surface = go2_context_surface_lock(context);

			go2_surface_blit(titlebarSurface, 0, 0, w, 16,
							 surface, 0, 0, w, 16,
							 GO2_ROTATION_DEGREES_0);

			if (g_screenshot_requested)
			{
				go2_display_t* display = getDisplay();
				int ss_w = go2_surface_width_get(surface);
				int ss_h = go2_surface_height_get(surface);

				go2_surface_t* screenshot = go2_surface_create(display, ss_w, ss_h, DRM_FORMAT_RGB888);
				if (!screenshot)
				{
					printf("go2_surface_create failed.\n");
					throw std::exception();
				}

				go2_surface_blit(surface, 0, 0, ss_w, ss_h,
								screenshot, 0, 0, ss_w, ss_h,
								GO2_ROTATION_DEGREES_0);

				time_t now = time(0);
				tm* local = localtime(&now);

				const size_t TIME_LEN_MAX = 128;
				char filename[TIME_LEN_MAX];
				strftime (filename, TIME_LEN_MAX, "EmulationStation_%F_%H-%M-%S.png", local);

				go2_surface_save_as_png(screenshot, filename);

				go2_surface_destroy(screenshot);
				
				g_screenshot_requested = false;
			}

			go2_presenter_post(presenter,
						surface,
						0, 0, w, h,
						0, 0, h, w,
						GO2_ROTATION_DEGREES_270);
			go2_context_surface_unlock(context, surface);
		}

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		//printf("Frame %d\n", frame++);
	} // swapBuffers

} // Renderer::

#endif // USE_OPENGLES_10
