/*
  SDL_image:  An example image loading library for use with SDL
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#if !(defined(__APPLE__) || defined(SDL_IMAGE_USE_WIC_BACKEND)) || defined(SDL_IMAGE_USE_COMMON_BACKEND)

/* This is a TIFF image file loading framework */

#include <SDL3_image/SDL_image.h>

#ifdef LOAD_TIF

#include <tiffio.h>

static struct {
    int loaded;
    void *handle;
    TIFF* (*TIFFClientOpen)(const char*, const char*, thandle_t, TIFFReadWriteProc, TIFFReadWriteProc, TIFFSeekProc, TIFFCloseProc, TIFFSizeProc, TIFFMapFileProc, TIFFUnmapFileProc);
    void (*TIFFClose)(TIFF*);
    int (*TIFFGetField)(TIFF*, ttag_t, ...);
    int (*TIFFReadRGBAImageOriented)(TIFF*, Uint32, Uint32, Uint32*, int, int);
    TIFFErrorHandler (*TIFFSetErrorHandler)(TIFFErrorHandler);
} lib;

#ifdef LOAD_TIF_DYNAMIC
#define FUNCTION_LOADER(FUNC, SIG) \
    lib.FUNC = (SIG) SDL_LoadFunction(lib.handle, #FUNC); \
    if (lib.FUNC == NULL) { SDL_UnloadObject(lib.handle); return false; }
#else
#define FUNCTION_LOADER(FUNC, SIG) \
    lib.FUNC = FUNC;
#endif

static bool IMG_InitTIF(void)
{
    if ( lib.loaded == 0 ) {
#ifdef LOAD_TIF_DYNAMIC
        lib.handle = SDL_LoadObject(LOAD_TIF_DYNAMIC);
        if ( lib.handle == NULL ) {
            return false;
        }
#endif
        FUNCTION_LOADER(TIFFClientOpen, TIFF * (*)(const char*, const char*, thandle_t, TIFFReadWriteProc, TIFFReadWriteProc, TIFFSeekProc, TIFFCloseProc, TIFFSizeProc, TIFFMapFileProc, TIFFUnmapFileProc))
        FUNCTION_LOADER(TIFFClose, void (*)(TIFF*))
        FUNCTION_LOADER(TIFFGetField, int (*)(TIFF*, ttag_t, ...))
        FUNCTION_LOADER(TIFFReadRGBAImageOriented, int (*)(TIFF*, Uint32, Uint32, Uint32*, int, int))
        FUNCTION_LOADER(TIFFSetErrorHandler, TIFFErrorHandler (*)(TIFFErrorHandler))
    }
    ++lib.loaded;

    return true;
}
#if 0
void IMG_QuitTIF(void)
{
    if ( lib.loaded == 0 ) {
        return;
    }
    if ( lib.loaded == 1 ) {
#ifdef LOAD_TIF_DYNAMIC
        SDL_UnloadObject(lib.handle);
#endif
    }
    --lib.loaded;
}
#endif // 0

/*
 * These are the thunking routine to use the SDL_IOStream* routines from
 * libtiff's internals.
*/

static tsize_t tiff_read(thandle_t fd, tdata_t buf, tsize_t size)
{
    return SDL_ReadIO((SDL_IOStream*)fd, buf, size);
}

static toff_t tiff_seek(thandle_t fd, toff_t offset, int origin)
{
    return SDL_SeekIO((SDL_IOStream*)fd, offset, origin);
}

static tsize_t tiff_write(thandle_t fd, tdata_t buf, tsize_t size)
{
    return SDL_WriteIO((SDL_IOStream*)fd, buf, size);
}

static int tiff_close(thandle_t fd)
{
    (void)fd;
    /*
     * We don't want libtiff closing our SDL_IOStream*, but if it's not given
         * a routine to try, and if the image isn't a TIFF, it'll segfault.
     */
    return 0;
}

static int tiff_map(thandle_t fd, tdata_t* pbase, toff_t* psize)
{
    (void)fd;
    (void)pbase;
    (void)psize;
    return (0);
}

static void tiff_unmap(thandle_t fd, tdata_t base, toff_t size)
{
    (void)fd;
    (void)base;
    (void)size;
    return;
}

static toff_t tiff_size(thandle_t fd)
{
    Sint64 save_pos;
    toff_t size;

    save_pos = SDL_TellIO((SDL_IOStream*)fd);
    SDL_SeekIO((SDL_IOStream*)fd, 0, SDL_IO_SEEK_END);
    size = SDL_TellIO((SDL_IOStream*)fd);
    SDL_SeekIO((SDL_IOStream*)fd, save_pos, SDL_IO_SEEK_SET);
    return size;
}

bool IMG_isTIF(SDL_IOStream * src)
{
    Sint64 start;
    bool is_TIF;
    Uint8 magic[4];

    if (!src) {
        return false;
    }

    start = SDL_TellIO(src);
    is_TIF = false;
    if (SDL_ReadIO(src, magic, sizeof(magic)) == sizeof(magic) ) {
        if ( (magic[0] == 'I' &&
                      magic[1] == 'I' &&
              magic[2] == 0x2a &&
                      magic[3] == 0x00) ||
             (magic[0] == 'M' &&
                      magic[1] == 'M' &&
              magic[2] == 0x00 &&
                      magic[3] == 0x2a) ) {
            is_TIF = true;
        }
    }
    SDL_SeekIO(src, start, SDL_IO_SEEK_SET);
    return is_TIF;
}

SDL_Surface* IMG_LoadTIF_IO(SDL_IOStream * src)
{
    Sint64 start;
    TIFF* tiff = NULL;
    SDL_Surface* surface = NULL;
    Uint32 img_width, img_height;

    if ( !src ) {
        /* The error message has been set in SDL_IOFromFile */
        return NULL;
    }
    start = SDL_TellIO(src);

    if (!IMG_InitTIF()) {
        return NULL;
    }

    /* turn off memory mapped access with the m flag */
    tiff = lib.TIFFClientOpen("SDL_image", "rm", (thandle_t)src,
        tiff_read, tiff_write, tiff_seek, tiff_close, tiff_size, tiff_map, tiff_unmap);
    if(!tiff)
        goto error;

    /* Retrieve the dimensions of the image from the TIFF tags */
    lib.TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &img_width);
    lib.TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &img_height);

    surface = SDL_CreateSurface(img_width, img_height, SDL_PIXELFORMAT_ABGR8888);
    if(!surface)
        goto error;

    if(!lib.TIFFReadRGBAImageOriented(tiff, img_width, img_height, (Uint32 *)surface->pixels, ORIENTATION_TOPLEFT, 0))
        goto error;

    lib.TIFFClose(tiff);

    return surface;

error:
    SDL_SeekIO(src, start, SDL_IO_SEEK_SET);
    if (surface) {
        SDL_DestroySurface(surface);
    }
    if (tiff) {
        lib.TIFFClose(tiff);
    }
    return NULL;
}

#else

/* See if an image is contained in a data source */
bool IMG_isTIF(SDL_IOStream *src)
{
    (void)src;
    return false;
}

/* Load a TIFF type image from an SDL datasource */
SDL_Surface *IMG_LoadTIF_IO(SDL_IOStream *src)
{
    (void)src;
    return NULL;
}

#endif /* LOAD_TIF */

#endif /* !defined(__APPLE__) || defined(SDL_IMAGE_USE_COMMON_BACKEND) */
